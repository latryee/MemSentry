#include "memsentry/memsentry.hpp"
#include "memsentry/core/recursion_guard.hpp"
#include "memsentry/core/header.hpp"
#include "memsentry/core/sharded_tracker.hpp"
#include "memsentry/core/allocator_hooks.hpp"
#include "memsentry/stacktrace/stacktrace.hpp"
#include <cstdlib>
#include <atomic>
#include <thread>
#include <iostream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

static LONG WINAPI MemsentryCrashHandler(PEXCEPTION_POINTERS pExceptionInfo) {
    if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
    if (code == 0xE06D7363 /* C++ exception */ || code == EXCEPTION_BREAKPOINT) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char buffer[256];
    int len = snprintf(buffer, sizeof(buffer),
        "\n[FATAL CRASH] ExceptionCode=0x%08lX Address=0x%p\n",
        static_cast<unsigned long>(code),
        pExceptionInfo->ExceptionRecord->ExceptionAddress
    );
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE && hErr != nullptr && len > 0) {
        DWORD written = 0;
        WriteFile(hErr, buffer, static_cast<DWORD>(len), &written, nullptr);
    }

    HANDLE hProcess = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(hProcess, nullptr, TRUE);

    CONTEXT ctx = *pExceptionInfo->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 16; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, GetCurrentThread(), &frame, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) break;

        alignas(SYMBOL_INFO) uint8_t sym_buf[sizeof(SYMBOL_INFO) + 256 * sizeof(TCHAR)]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 disp = 0;

        char line_buf[256];
        if (SymFromAddr(hProcess, frame.AddrPC.Offset, &disp, symbol)) {
            IMAGEHLP_LINE64 line_info{};
            line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD line_disp = 0;
            if (SymGetLineFromAddr64(hProcess, frame.AddrPC.Offset, &line_disp, &line_info)) {
                snprintf(line_buf, sizeof(line_buf), "  #%d 0x%llX %s (%s:%lu)\n", i, frame.AddrPC.Offset, symbol->Name, line_info.FileName, line_info.LineNumber);
            } else {
                snprintf(line_buf, sizeof(line_buf), "  #%d 0x%llX %s\n", i, frame.AddrPC.Offset, symbol->Name);
            }
        } else {
            snprintf(line_buf, sizeof(line_buf), "  #%d 0x%llX\n", i, frame.AddrPC.Offset);
        }
        DWORD wr = 0;
        WriteFile(hErr, line_buf, static_cast<DWORD>(strlen(line_buf)), &wr, nullptr);
    }
    FlushFileBuffers(hErr);
    return EXCEPTION_CONTINUE_SEARCH;
}

namespace {
struct CrashHandlerInstaller {
    CrashHandlerInstaller() {
        AddVectoredExceptionHandler(1, MemsentryCrashHandler);
    }
};
static CrashHandlerInstaller g_crash_installer;
}
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
#if defined(_MSC_VER)
__declspec(thread) int g_recursion_depth = 0;
#else
thread_local int g_recursion_depth = 0;
#endif
}

namespace memsentry::profiler {
#if defined(_MSC_VER)
__declspec(thread) const char* g_active_scope_tag = nullptr;
#else
thread_local const char* g_active_scope_tag = nullptr;
#endif
}

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
#endif
        config_ = config;
        if (initialized_.exchange(true)) return;

        volatile int anchor = core::ensure_hooks_linked();
        (void)anchor;

        stacktrace::StackTraceProvider::instance().initialize();

        if (config_.auto_report_on_exit) {
            std::atexit([]() {
                Manager::instance().on_exit();
            });
        }
    }

    void shutdown() {
        if (!initialized_.exchange(false)) return;
        core::RecursionGuard guard;
        stacktrace::StackTraceProvider::instance().cleanup();
    }

    [[nodiscard]] bool is_initialized() const noexcept {
        return initialized_.load(std::memory_order_relaxed);
    }

    void* allocate(size_t size, size_t alignment, const char* tag) noexcept {
        if (core::RecursionGuard::is_active() || !initialized_.load(std::memory_order_relaxed)) {
            return std::malloc(size);
        }

        core::RecursionGuard guard;

        size_t footer_size = config_.enable_canary ? config_.canary_footer_size : 0;
        size_t align = (alignment > DEFAULT_ALIGNMENT) ? alignment : DEFAULT_ALIGNMENT;
        size_t extra_align = (align > DEFAULT_ALIGNMENT) ? align : 0;
        size_t total_size = size + footer_size + extra_align;
        if (total_size < size) return nullptr;

        void* raw_ptr = std::malloc(total_size);
        if (!raw_ptr) return nullptr;

        void* user_ptr = raw_ptr;
        if (extra_align > 0) {
            uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_ptr);
            uintptr_t user_addr = (raw_addr + (align - 1)) & ~(align - 1);
            user_ptr = reinterpret_cast<void*>(user_addr);
        }

        if (config_.enable_canary && footer_size >= sizeof(uint64_t)) {
            uint8_t* footer_ptr = reinterpret_cast<uint8_t*>(user_ptr) + size;
            for (size_t i = 0; i < footer_size; i += sizeof(uint64_t)) {
                size_t copy_len = (footer_size - i < sizeof(uint64_t)) ? (footer_size - i) : sizeof(uint64_t);
                std::memcpy(footer_ptr + i, &CANARY_FOOTER_MAGIC, copy_len);
            }
        }

        uint64_t alloc_id = alloc_counter_.fetch_add(1, std::memory_order_relaxed);
        const char* active_tag = profiler::ScopedTag::current();
        const char* final_tag = tag ? tag : (active_tag ? active_tag : config_.default_tag);

        AllocationRecord record;
        record.allocation_id = alloc_id;
        record.user_ptr = user_ptr;
        record.raw_ptr = raw_ptr;
        record.requested_size = size;
        record.total_size = total_size;
        record.alignment = alignment;
        record.tag = final_tag;
        record.timestamp = std::chrono::system_clock::now();
        record.thread_id = current_thread_id();

        if (config_.enable_stacktrace) {
            uint32_t depth = std::min(config_.max_stack_depth, static_cast<uint32_t>(record.callstack.size()));
            record.frame_count = stacktrace::StackTraceProvider::instance().capture(
                record.callstack.data(),
                depth,
                config_.stack_skip_frames
            );
        }

        stats_.update_on_alloc(size);
        tracker_.insert(user_ptr, std::move(record));

        return user_ptr;
    }

    void deallocate(void* user_ptr) noexcept {
        if (!user_ptr) return;

        if (core::RecursionGuard::is_active() || !initialized_.load(std::memory_order_relaxed)) {
            std::free(user_ptr);
            return;
        }

        core::RecursionGuard guard;

        AllocationRecord record;
        if (tracker_.erase(user_ptr, &record)) {
            if (config_.enable_canary && config_.canary_footer_size >= sizeof(uint64_t)) {
                const uint8_t* footer_ptr = reinterpret_cast<const uint8_t*>(user_ptr) + record.requested_size;
                CorruptionType status = CorruptionType::NONE;
                for (size_t i = 0; i < config_.canary_footer_size; i += sizeof(uint64_t)) {
                    uint64_t val = 0;
                    size_t copy_len = (config_.canary_footer_size - i < sizeof(uint64_t)) ? (config_.canary_footer_size - i) : sizeof(uint64_t);
                    std::memcpy(&val, footer_ptr + i, copy_len);
                    uint64_t expected = CANARY_FOOTER_MAGIC;
                    if (std::memcmp(&val, &expected, copy_len) != 0) {
                        status = CorruptionType::FOOTER_CORRUPTED;
                        break;
                    }
                }
                if (status != CorruptionType::NONE) {
                    reporter::ConsoleReporter::print_corruption_alert(std::cerr, user_ptr, status);
                }
            }
            stats_.update_on_free(record.requested_size);
            std::free(record.raw_ptr);
        } else {
            std::free(user_ptr);
        }
    }

    void* reallocate(void* ptr, size_t new_size) noexcept {
        if (!ptr) return allocate(new_size, config_.alignment, nullptr);
        if (new_size == 0) {
            deallocate(ptr);
            return nullptr;
        }

        if (core::RecursionGuard::is_active() || !initialized_.load(std::memory_order_relaxed)) {
            return std::realloc(ptr, new_size);
        }

        AllocationRecord old_record;
        if (!tracker_.find(ptr, &old_record)) {
            return std::realloc(ptr, new_size);
        }

        void* new_ptr = allocate(new_size, old_record.alignment, old_record.tag);
        if (!new_ptr) return nullptr;

        size_t copy_bytes = (old_record.requested_size < new_size) ? old_record.requested_size : new_size;
        std::memcpy(new_ptr, ptr, copy_bytes);
        deallocate(ptr);
        return new_ptr;
    }

    [[nodiscard]] MemoryStatsSnapshot get_stats() const noexcept {
        return MemoryStatsSnapshot::from(stats_);
    }

    [[nodiscard]] std::vector<AllocationRecord> get_active_allocations() const {
        core::RecursionGuard guard;
        return tracker_.snapshot_all();
    }

    [[nodiscard]] bool has_leaks() const noexcept {
        return tracker_.size() > 0;
    }

    void dump_leaks(std::ostream& os) {
        core::RecursionGuard guard;
        auto stats = get_stats();
        auto leaks = tracker_.snapshot_all();
        size_t leak_bytes = 0;
        for (const auto& l : leaks) leak_bytes += l.requested_size;
        reporter::ConsoleReporter::print_summary(os, stats, leaks.size(), leak_bytes);
        reporter::ConsoleReporter::print_leaks(os, leaks);
    }

    bool export_json(const std::string& filepath) {
        core::RecursionGuard guard;
        auto stats = get_stats();
        auto leaks = tracker_.snapshot_all();
        return reporter::JsonReporter::write_file(filepath, stats, leaks);
    }

    bool export_html(const std::string& filepath) {
        core::RecursionGuard guard;
        auto stats = get_stats();
        auto leaks = tracker_.snapshot_all();
        return reporter::HtmlReporter::write_file(filepath, stats, leaks);
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

private:
    Manager() {
        core::RecursionGuard guard;
    }
    ~Manager() = default;

    void on_exit() {
        if (!initialized_.load(std::memory_order_relaxed)) return;
        if (!config_.auto_report_on_exit) return;
        dump_leaks(std::cout);
        if (config_.exit_with_code_on_leak && has_leaks()) {
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

std::vector<AllocationRecord> get_active_allocations() {
    return Manager::instance().get_active_allocations();
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

}

namespace memsentry::core {

void* track_alloc(size_t size, size_t alignment, const char* tag) noexcept {
    if (core::RecursionGuard::is_active()) {
        return std::malloc(size);
    }
    if (!Manager::instance().is_initialized()) {
        Manager::instance().initialize({});
    }
    return Manager::instance().allocate(size, alignment, tag);
}

void track_free(void* ptr) noexcept {
    if (!ptr) return;
    if (core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    Manager::instance().deallocate(ptr);
}

void* track_realloc(void* ptr, size_t new_size) noexcept {
    if (core::RecursionGuard::is_active()) {
        return std::realloc(ptr, new_size);
    }
    if (!Manager::instance().is_initialized()) {
        Manager::instance().initialize({});
    }
    return Manager::instance().reallocate(ptr, new_size);
}

}
