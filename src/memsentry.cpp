#include "memsentry/memsentry.hpp"

#include "memsentry/core/allocator_hooks.hpp"
#include "memsentry/core/header.hpp"
#include "memsentry/core/recursion_guard.hpp"
#include "memsentry/core/sharded_tracker.hpp"
#include "memsentry/core/suppression_engine.hpp"
#include "memsentry/profiler/flamegraph.hpp"
#include "memsentry/stacktrace/stacktrace.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

static LONG WINAPI MemsentryCrashHandler(PEXCEPTION_POINTERS pExceptionInfo) {
    if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord || !pExceptionInfo->ContextRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
    if (code == 0xE06D7363 /* C++ exception */ || code == EXCEPTION_BREAKPOINT) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr == INVALID_HANDLE_VALUE || hErr == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* pCtx = pExceptionInfo->ContextRecord;
    uintptr_t* stack = reinterpret_cast<uintptr_t*>(pCtx->Rsp);

    char msg[512];
    int mlen = snprintf(msg, sizeof(msg), "\n[FATAL CRASH] ExceptionCode=0x%08lX RIP=0x%p RSP=0x%p\nSTACK DUMP:\n",
                        static_cast<unsigned long>(code), reinterpret_cast<void*>(pCtx->Rip),
                        reinterpret_cast<void*>(pCtx->Rsp));
    DWORD wr = 0;
    WriteFile(hErr, msg, static_cast<DWORD>(mlen), &wr, nullptr);

    for (int i = 0; i < 20; ++i) {
        char line[128];
        int llen = snprintf(line, sizeof(line), "  [%d] 0x%p\n", i, reinterpret_cast<void*>(stack[i]));
        WriteFile(hErr, line, static_cast<DWORD>(llen), &wr, nullptr);
    }
    FlushFileBuffers(hErr);
    return EXCEPTION_CONTINUE_SEARCH;
}

namespace {
struct CrashHandlerInstaller {
    CrashHandlerInstaller() { AddVectoredExceptionHandler(1, MemsentryCrashHandler); }
};
static CrashHandlerInstaller g_crash_installer;
}  // namespace
#elif defined(__linux__)
#include <unistd.h>
#if defined(__has_include)
#if __has_include(<sys/syscall.h>)
#include <sys/syscall.h>
#endif
#endif
#elif defined(__APPLE__)
#include <pthread.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace memsentry::core {
#if defined(_WIN32)
DWORD g_tls_guard_index = TlsAlloc();
#else
thread_local int g_recursion_depth = 0;
#endif
}  // namespace memsentry::core

namespace memsentry::profiler {
#if defined(_WIN32)
DWORD g_tls_tag_index = TlsAlloc();
#else
thread_local const char* g_active_scope_tag = nullptr;
#endif
}  // namespace memsentry::profiler

namespace {
// Fast zero-allocation thread-local PRNG for Poisson / Geometric Sampling
#if defined(_WIN32)
DWORD g_tls_rng_index = TlsAlloc();
#else
thread_local uint64_t tl_rng_state = 0;
thread_local int64_t tl_bytes_until_sample = 0;
#endif

#if defined(_WIN32)
DWORD g_tls_sample_index = TlsAlloc();
#endif

inline int64_t get_bytes_until_sample() noexcept {
#if defined(_WIN32)
    return reinterpret_cast<intptr_t>(TlsGetValue(g_tls_sample_index));
#else
    return tl_bytes_until_sample;
#endif
}

inline void set_bytes_until_sample(int64_t val) noexcept {
#if defined(_WIN32)
    TlsSetValue(g_tls_sample_index, reinterpret_cast<LPVOID>(static_cast<intptr_t>(val)));
#else
    tl_bytes_until_sample = val;
#endif
}

inline double next_uniform_double() noexcept {
#if defined(_WIN32)
    uintptr_t state = reinterpret_cast<uintptr_t>(TlsGetValue(g_tls_rng_index));
    if (state == 0) {
        state = 0x853c49e6748fea9bULL ^ static_cast<uint64_t>(GetCurrentThreadId());
    }
    state ^= (state << 13);
    state ^= (state >> 7);
    state ^= (state << 17);
    TlsSetValue(g_tls_rng_index, reinterpret_cast<LPVOID>(state));
    return static_cast<double>(state & 0x1FFFFFFFFFFFFFULL) / 9007199254740992.0;
#else
    if (tl_rng_state == 0) {
        tl_rng_state = 0x853c49e6748fea9bULL ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&tl_rng_state));
    }
    tl_rng_state ^= (tl_rng_state << 13);
    tl_rng_state ^= (tl_rng_state >> 7);
    tl_rng_state ^= (tl_rng_state << 17);
    return static_cast<double>(tl_rng_state & 0x1FFFFFFFFFFFFFULL) / 9007199254740992.0;
#endif
}

inline int64_t next_geometric_interval(uint64_t mean_bytes) noexcept {
    if (mean_bytes == 0)
        return 0;
    double u = next_uniform_double();
    if (u <= 0.0)
        u = 1e-10;
    if (u >= 1.0)
        u = 1.0 - 1e-10;
    return static_cast<int64_t>(-static_cast<double>(mean_bytes) * std::log(1.0 - u)) + 1;
}
}  // namespace

namespace memsentry {

class Manager {
public:
    static Manager& instance() noexcept;

    void initialize(const Config& config) {
        core::RecursionGuard guard;
#if defined(_MSC_VER) && defined(_DEBUG)
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        config_ = config;
        if (initialized_.exchange(true))
            return;

        volatile int anchor = core::ensure_hooks_linked();
        (void)anchor;

        stacktrace::StackTraceProvider::instance().initialize();

        if (config_.auto_report_on_exit) {
            std::atexit([]() { Manager::instance().on_exit(); });
        }
    }

    void shutdown() {
        if (!initialized_.exchange(false))
            return;
        core::RecursionGuard guard;
        stacktrace::StackTraceProvider::instance().cleanup();
    }

    [[nodiscard]] bool is_initialized() const noexcept { return initialized_.load(std::memory_order_relaxed); }

    void* allocate(size_t size, size_t alignment, const char* tag) noexcept {
        if (core::RecursionGuard::is_active() || !initialized_.load(std::memory_order_relaxed)) {
            return core::raw_system_alloc(size);
        }

        core::RecursionGuard guard;

        uint64_t alloc_id = alloc_counter_.fetch_add(1, std::memory_order_relaxed);

        // 1. Poisson / Geometric Byte-Interval Sampling (TCMalloc style)
        if (config_.sampling_rate_bytes > 0) {
            int64_t bytes_until = get_bytes_until_sample();
            if (bytes_until <= 0) {
                bytes_until = next_geometric_interval(config_.sampling_rate_bytes);
            }
            if (static_cast<int64_t>(size) < bytes_until) {
                set_bytes_until_sample(bytes_until - static_cast<int64_t>(size));
                return core::raw_system_alloc(size);
            }
            set_bytes_until_sample(next_geometric_interval(config_.sampling_rate_bytes));
        } else if (config_.sample_every_n > 1) {
            // 2. Deterministic 1-in-N Sampling
            if ((alloc_id % config_.sample_every_n) != 0) {
                return core::raw_system_alloc(size);
            }
        } else if (config_.sampling_percentage < 100) {
            // 3. Percentage Sampling
            if ((alloc_id % 100) >= config_.sampling_percentage) {
                return core::raw_system_alloc(size);
            }
        }

        size_t footer_size = config_.enable_canary ? config_.canary_footer_size : 0;
        size_t eff_align = (alignment > alignof(std::max_align_t)) ? alignment : alignof(std::max_align_t);
        size_t total_size = core::calculate_total_size(size, eff_align, footer_size);
        if (total_size == 0)
            return nullptr;

        void* raw_ptr = core::raw_system_alloc(total_size);
        if (!raw_ptr)
            return nullptr;

        const char* active_tag = profiler::ScopedTag::current();
        const char* final_tag = tag ? tag : (active_tag ? active_tag : config_.default_tag);

        uint32_t tid = current_thread_id();
        void* user_ptr = core::init_block(raw_ptr, size, eff_align, footer_size, alloc_id, final_tag,
                                          config_.enable_canary, tid);

        AllocationRecord record;
        record.allocation_id = alloc_id;
        record.user_ptr = user_ptr;
        record.raw_ptr = raw_ptr;
        record.requested_size = size;
        record.total_size = total_size;
        record.alignment = eff_align;
        record.tag = final_tag;
        record.timestamp = std::chrono::system_clock::now();
        record.thread_id = tid;

        if (config_.enable_stacktrace) {
            uint32_t depth = std::min(config_.max_stack_depth, static_cast<uint32_t>(record.callstack.size()));
            record.frame_count = stacktrace::StackTraceProvider::instance().capture(record.callstack.data(), depth,
                                                                                    config_.stack_skip_frames);
        }

        stats_.update_on_alloc(size);
        tracker_.insert(user_ptr, std::move(record));

        // Memory Limit Watchdog Evaluation
        if (config_.max_heap_bytes > 0 && config_.on_limit_exceeded != nullptr) {
            uint64_t current_allocated = stats_.current_allocated_bytes.load(std::memory_order_relaxed);
            if (current_allocated > config_.max_heap_bytes) {
                config_.on_limit_exceeded(current_allocated, config_.max_heap_bytes);
            }
        }

        return user_ptr;
    }

    void deallocate(void* user_ptr) noexcept {
        if (!user_ptr)
            return;

        if (core::RecursionGuard::is_active()) {
            auto* candidate = core::get_header_from_user_ptr(user_ptr);
            if (candidate) {
                if (candidate->magic == CANARY_FREED_MAGIC) {
                    return;
                }
                core::raw_system_free(candidate);
            } else {
                core::raw_system_free(user_ptr);
            }
            return;
        }

        core::RecursionGuard guard;

        AllocationRecord record;
        auto status = tracker_.erase(user_ptr, &record);
        if (status == core::TrackerEraseStatus::SUCCESS) {
            size_t footer_size = config_.enable_canary ? config_.canary_footer_size : 0;
            auto* header = core::get_header_from_raw_ptr(record.raw_ptr);
            CorruptionType cstatus = core::verify_canary(header, footer_size);

            if (cstatus != CorruptionType::NONE) {
                reporter::ConsoleReporter::print_corruption_alert(std::cerr, user_ptr, cstatus);
            }

            stats_.update_on_free(record.requested_size);
            free_hist_.record(record.requested_size);

            if (cstatus == CorruptionType::NONE) {
                core::poison_block(header, footer_size);
                core::raw_system_free(record.raw_ptr);
            }
        } else if (status == core::TrackerEraseStatus::DOUBLE_FREE_DETECTED) {
            reporter::ConsoleReporter::print_corruption_alert(std::cerr, user_ptr, CorruptionType::DOUBLE_FREE);
        } else {
            // Check if user_ptr is an already-freed or untracked tracked block
            auto* candidate = core::get_header_from_user_ptr(user_ptr);
            if (candidate) {
                if (candidate->magic == CANARY_FREED_MAGIC) {
                    reporter::ConsoleReporter::print_corruption_alert(std::cerr, user_ptr, CorruptionType::DOUBLE_FREE);
                    return;
                }
                if (candidate->magic == CANARY_HEADER_MAGIC) {
                    core::poison_block(candidate, config_.canary_footer_size);
                    core::raw_system_free(candidate);
                    return;
                }
            }
            // Truly a raw untracked system allocation
            core::raw_system_free(user_ptr);
        }
    }

    void* reallocate(void* ptr, size_t new_size) noexcept {
        if (!ptr)
            return allocate(new_size, config_.alignment, nullptr);
        if (new_size == 0) {
            deallocate(ptr);
            return nullptr;
        }

        if (core::RecursionGuard::is_active() || !initialized_.load(std::memory_order_relaxed)) {
            return core::raw_system_realloc(ptr, new_size);
        }

        AllocationRecord old_record;
        if (!tracker_.find(ptr, &old_record)) {
            return core::raw_system_realloc(ptr, new_size);
        }

        if (config_.enable_canary) {
            size_t footer_size = config_.canary_footer_size;
            auto* header = core::get_header_from_raw_ptr(old_record.raw_ptr);
            CorruptionType cstatus = core::verify_canary(header, footer_size);
            if (cstatus != CorruptionType::NONE) {
                reporter::ConsoleReporter::print_corruption_alert(std::cerr, ptr, cstatus);
            }
        }

        void* new_ptr = allocate(new_size, old_record.alignment, old_record.tag);
        if (!new_ptr)
            return nullptr;

        size_t copy_bytes = (old_record.requested_size < new_size) ? old_record.requested_size : new_size;
        std::memcpy(new_ptr, ptr, copy_bytes);
        deallocate(ptr);
        return new_ptr;
    }

    [[nodiscard]] MemoryStatsSnapshot get_stats() const noexcept { return MemoryStatsSnapshot::from(stats_); }

    [[nodiscard]] std::vector<AllocationRecord> get_active_allocations() const {
        core::RecursionGuard guard;
        return tracker_.snapshot_all();
    }

    [[nodiscard]] bool has_leaks() const noexcept { return tracker_.size() > 0; }

    [[nodiscard]] bool has_unsuppressed_leaks() const noexcept {
        core::RecursionGuard guard;
        auto all = tracker_.snapshot_all();
        for (const auto& rec : all) {
            if (!core::SuppressionEngine::instance().is_suppressed(rec)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] size_t get_suppressed_count() const noexcept {
        core::RecursionGuard guard;
        auto all = tracker_.snapshot_all();
        size_t count = 0;
        for (const auto& rec : all) {
            if (core::SuppressionEngine::instance().is_suppressed(rec)) {
                count++;
            }
        }
        return count;
    }

    void dump_leaks(std::ostream& os) {
        core::RecursionGuard guard;
        auto stats = get_stats();
        auto leaks = tracker_.snapshot_all();
        std::vector<AllocationRecord> unsuppressed;
        unsuppressed.reserve(leaks.size());
        for (const auto& l : leaks) {
            if (!core::SuppressionEngine::instance().is_suppressed(l)) {
                unsuppressed.push_back(l);
            }
        }
        size_t leak_bytes = 0;
        for (const auto& l : unsuppressed)
            leak_bytes += l.requested_size;
        reporter::ConsoleReporter::print_summary(os, stats, unsuppressed.size(), leak_bytes);
        reporter::ConsoleReporter::print_leaks(os, unsuppressed);
    }

    bool export_json(const std::string& filepath) {
        core::RecursionGuard guard;
        auto stats = get_stats();
        auto leaks = tracker_.snapshot_all();
        std::vector<AllocationRecord> unsuppressed;
        unsuppressed.reserve(leaks.size());
        for (const auto& l : leaks) {
            if (!core::SuppressionEngine::instance().is_suppressed(l)) {
                unsuppressed.push_back(l);
            }
        }
        return reporter::JsonReporter::write_file(filepath, stats, unsuppressed);
    }

    bool export_html(const std::string& filepath) {
        core::RecursionGuard guard;
        auto stats = get_stats();
        auto leaks = tracker_.snapshot_all();
        std::vector<AllocationRecord> unsuppressed;
        unsuppressed.reserve(leaks.size());
        for (const auto& l : leaks) {
            if (!core::SuppressionEngine::instance().is_suppressed(l)) {
                unsuppressed.push_back(l);
            }
        }
        return reporter::HtmlReporter::write_file(filepath, stats, unsuppressed);
    }

    profiler::HeapSnapshot take_snapshot(const std::string& label) {
        core::RecursionGuard guard;
        profiler::HeapSnapshot snap;
        snap.label = label;
        snap.timestamp = std::chrono::system_clock::now();
        snap.stats = get_stats();
        snap.active_allocations = tracker_.snapshot_all();
        return snap;
    }

    profiler::FragmentationReport get_fragmentation_report() const {
        core::RecursionGuard guard;
        return profiler::FragmentationAnalyzer::analyze(get_stats(), tracker_.snapshot_all(), free_hist_);
    }

    std::string get_flamegraph_svg(double width) const {
        core::RecursionGuard guard;
        auto active = tracker_.snapshot_all();
        auto root = profiler::FlamegraphGenerator::build_tree(active);
        return profiler::FlamegraphGenerator::generate_svg(root, width);
    }

private:
    Manager() { core::RecursionGuard guard; }
    ~Manager() = default;

    void on_exit() {
        if (!initialized_.load(std::memory_order_relaxed))
            return;
        if (!config_.auto_report_on_exit)
            return;
        dump_leaks(std::cout);
        if (config_.exit_with_code_on_leak && has_unsuppressed_leaks()) {
            std::exit(1);
        }
    }

    static uint32_t current_thread_id() noexcept {
#if defined(_WIN32)
        return static_cast<uint32_t>(GetCurrentThreadId());
#elif defined(__linux__) && defined(SYS_gettid)
        return static_cast<uint32_t>(syscall(SYS_gettid));
#elif defined(__APPLE__)
        uint64_t tid = 0;
        pthread_threadid_np(nullptr, &tid);
        return static_cast<uint32_t>(tid);
#else
        return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
    }

    Config config_;
    std::atomic<bool> initialized_{false};
    std::atomic<uint64_t> alloc_counter_{1};
    MemoryStats stats_;
    core::ShardedTracker tracker_;
    profiler::FreeBlockHistogram free_hist_;
};

Manager& Manager::instance() noexcept {
    alignas(Manager) static char storage[sizeof(Manager)];
    static Manager* inst = []() {
        core::RecursionGuard guard;
        return new (storage) Manager();
    }();
    return *inst;
}

void init(const Config& config) {
    Manager::instance().initialize(config);
}

void shutdown() {
    Manager::instance().shutdown();
}

bool is_initialized() noexcept {
    return Manager::instance().is_initialized();
}

MemoryStatsSnapshot get_stats() noexcept {
    return Manager::instance().get_stats();
}

bool has_leaks() noexcept {
    return Manager::instance().has_leaks();
}

bool has_unsuppressed_leaks() {
    return Manager::instance().has_unsuppressed_leaks();
}

std::vector<AllocationRecord> get_active_allocations() {
    return Manager::instance().get_active_allocations();
}

void suppress(const std::string& pattern) {
    core::SuppressionEngine::instance().add_rule(pattern);
}

void clear_suppressions() noexcept {
    core::SuppressionEngine::instance().clear();
}

size_t get_suppressed_count() {
    return Manager::instance().get_suppressed_count();
}

void dump_leaks(std::ostream& os) {
    Manager::instance().dump_leaks(os);
}

bool export_json(const std::string& filepath) {
    return Manager::instance().export_json(filepath);
}

bool export_html(const std::string& filepath) {
    return Manager::instance().export_html(filepath);
}

profiler::HeapSnapshot take_snapshot(const std::string& label) {
    return Manager::instance().take_snapshot(label);
}

profiler::FragmentationReport get_fragmentation_report() {
    return Manager::instance().get_fragmentation_report();
}

std::string get_flamegraph_svg(double width) {
    return Manager::instance().get_flamegraph_svg(width);
}

}  // namespace memsentry

namespace memsentry::core {

void* track_alloc(size_t size, size_t alignment, const char* tag) noexcept {
    if (core::RecursionGuard::is_active() || !Manager::instance().is_initialized()) {
        return core::raw_system_alloc(size);
    }
    return Manager::instance().allocate(size, alignment, tag);
}

void track_free(void* ptr) noexcept {
    if (!ptr)
        return;
    if (core::RecursionGuard::is_active()) {
        core::raw_system_free(ptr);
        return;
    }
    Manager::instance().deallocate(ptr);
}

void* track_realloc(void* ptr, size_t new_size) noexcept {
    if (core::RecursionGuard::is_active() || !Manager::instance().is_initialized()) {
        return core::raw_system_realloc(ptr, new_size);
    }
    return Manager::instance().reallocate(ptr, new_size);
}

}  // namespace memsentry::core
