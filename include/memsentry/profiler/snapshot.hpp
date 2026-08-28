#pragma once

#include "memsentry/types.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace memsentry::profiler {

struct HeapSnapshot {
    std::string label;
    std::chrono::system_clock::time_point timestamp;
    MemoryStatsSnapshot stats;
    std::vector<AllocationRecord> active_allocations;
};

struct SnapshotDiff {
    std::string before_label;
    std::string after_label;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;

    int64_t net_bytes_delta = 0;
    int64_t net_allocations_delta = 0;

    std::vector<AllocationRecord> new_allocations;
    std::vector<AllocationRecord> freed_allocations;
    std::vector<AllocationRecord> persistent_allocations;

    [[nodiscard]] size_t new_leaked_bytes() const noexcept {
        size_t sum = 0;
        for (const auto& rec : new_allocations) {
            sum += rec.requested_size;
        }
        return sum;
    }
};

SnapshotDiff compare_snapshots(const HeapSnapshot& before, const HeapSnapshot& after);

}  // namespace memsentry::profiler
