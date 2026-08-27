#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <chrono>
#include <atomic>
#include "memsentry/config.hpp"

namespace memsentry {

enum class CorruptionType : uint8_t {
    NONE = 0,
    HEADER_CORRUPTED,
    FOOTER_CORRUPTED,
    DOUBLE_FREE,
    UNTRACKED_POINTER
};

struct StackFrame {
    uintptr_t address = 0;
    std::string symbol_name;
    std::string file_name;
    uint32_t line_number = 0;
};

struct AllocationRecord {
    uint64_t allocation_id = 0;
    const void* user_ptr = nullptr;
    void* raw_ptr = nullptr;
    size_t requested_size = 0;
    size_t total_size = 0;
    size_t alignment = 0;
    uint32_t thread_id = 0;
    const char* tag = "General";
    std::chrono::system_clock::time_point timestamp;
    std::array<void*, 32> callstack{};
    uint16_t frame_count = 0;
};

struct MemoryStats {
    std::atomic<uint64_t> total_allocated_bytes{0};
    std::atomic<uint64_t> total_freed_bytes{0};
    std::atomic<uint64_t> current_allocated_bytes{0};
    std::atomic<uint64_t> peak_allocated_bytes{0};
    std::atomic<uint64_t> total_allocation_count{0};
    std::atomic<uint64_t> total_free_count{0};
    std::atomic<uint64_t> active_allocation_count{0};

    void update_on_alloc(size_t bytes) noexcept {
        total_allocated_bytes.fetch_add(bytes, std::memory_order_relaxed);
        total_allocation_count.fetch_add(1, std::memory_order_relaxed);
        active_allocation_count.fetch_add(1, std::memory_order_relaxed);
        
        uint64_t current = current_allocated_bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        uint64_t peak = peak_allocated_bytes.load(std::memory_order_relaxed);
        while (current > peak && !peak_allocated_bytes.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {}
    }

    void update_on_free(size_t bytes) noexcept {
        total_freed_bytes.fetch_add(bytes, std::memory_order_relaxed);
        total_free_count.fetch_add(1, std::memory_order_relaxed);
        active_allocation_count.fetch_sub(1, std::memory_order_relaxed);
        current_allocated_bytes.fetch_sub(bytes, std::memory_order_relaxed);
    }
};

struct MemoryStatsSnapshot {
    uint64_t total_allocated_bytes = 0;
    uint64_t total_freed_bytes = 0;
    uint64_t current_allocated_bytes = 0;
    uint64_t peak_allocated_bytes = 0;
    uint64_t total_allocation_count = 0;
    uint64_t total_free_count = 0;
    uint64_t active_allocation_count = 0;

    static MemoryStatsSnapshot from(const MemoryStats& stats) noexcept {
        return {
            stats.total_allocated_bytes.load(std::memory_order_relaxed),
            stats.total_freed_bytes.load(std::memory_order_relaxed),
            stats.current_allocated_bytes.load(std::memory_order_relaxed),
            stats.peak_allocated_bytes.load(std::memory_order_relaxed),
            stats.total_allocation_count.load(std::memory_order_relaxed),
            stats.total_free_count.load(std::memory_order_relaxed),
            stats.active_allocation_count.load(std::memory_order_relaxed)
        };
    }
};

}
