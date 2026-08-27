#include "memsentry/memsentry.hpp"
#include "memsentry/core/recursion_guard.hpp"
#include "memsentry/core/header.hpp"
#include "memsentry/core/sharded_tracker.hpp"
#include "memsentry/core/allocator_hooks.hpp"
#include "memsentry/stacktrace/stacktrace.hpp"
#include <cstdlib>
#include <atomic>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

namespace memsentry {

class Manager {
public:
    static Manager& instance() noexcept {
        static Manager inst;
        return inst;
    }

    void initialize(const Config& config) {
        if (initialized_.exchange(true)) return;
        config_ = config;
        stacktrace::StackTraceProvider::instance().initialize();

        if (config_.auto_report_on_exit) {
            std::atexit([]() {
                Manager::instance().on_exit();
            });
        }
    }

    void shutdown() {
        if (!initialized_.exchange(false)) return;
        stacktrace::StackTraceProvider::instance().cleanup();
    }

    [[nodiscard]] bool is_initialized() const noexcept {
        return initialized_.load(std::memory_order_relaxed);
    }

    void* allocate(size_t size, size_t alignment, const char* tag) noexcept {
        if (core::RecursionGuard::active || !initialized_.load(std::memory_order_relaxed)) {
            return std::malloc(size);
        }

        core::RecursionGuard guard;

        size_t total_size = core::calculate_total_size(size, alignment, config_.canary_footer_size);
        void* raw_ptr = std::malloc(total_size);
        if (!raw_ptr) return nullptr;

        uint64_t alloc_id = alloc_counter_.fetch_add(1, std::memory_order_relaxed);
        const char* final_tag = tag ? tag : (profiler::g_active_tag ? profiler::g_active_tag : config_.default_tag);

        void* user_ptr = core::init_block(raw_ptr, size, alignment, config_.canary_footer_size, alloc_id, final_tag, config_.enable_canary);

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
            record.frame_count = stacktrace::StackTraceProvider::instance().capture(
                record.callstack.data(),
                config_.max_stack_depth,
                config_.stack_skip_frames
            );
        }

        stats_.update_on_alloc(size);
        tracker_.insert(user_ptr, std::move(record));

        return user_ptr;
    }

    void deallocate(void* user_ptr) noexcept {
        if (!user_ptr) return;

        if (core::RecursionGuard::active || !initialized_.load(std::memory_order_relaxed)) {
            std::free(user_ptr);
            return;
        }

        core::RecursionGuard guard;

        AllocationRecord record;
        if (tracker_.erase(user_ptr, &record)) {
            if (config_.enable_canary) {
                auto* header = core::get_header_from_raw_ptr(record.raw_ptr);
                CorruptionType status = core::verify_canary(header, config_.canary_footer_size);
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

        if (core::RecursionGuard::active || !initialized_.load(std::memory_order_relaxed)) {
            return std::realloc(ptr, new_size);
        }

        core::RecursionGuard guard;
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
    Manager() = default;
    ~Manager() = default;

    void on_exit() {
        if (!initialized_.load(std::memory_order_relaxed)) return;
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
    if (!Manager::instance().is_initialized()) {
        Manager::instance().initialize({});
    }
    return Manager::instance().allocate(size, alignment, tag);
}

void track_free(void* ptr) noexcept {
    Manager::instance().deallocate(ptr);
}

void* track_realloc(void* ptr, size_t new_size) noexcept {
    if (!Manager::instance().is_initialized()) {
        Manager::instance().initialize({});
    }
    return Manager::instance().reallocate(ptr, new_size);
}

}
