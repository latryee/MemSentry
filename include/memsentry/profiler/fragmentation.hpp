#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>
#include <cmath>
#include "memsentry/profiler/histogram.hpp"
#include "memsentry/types.hpp"

namespace memsentry::profiler {

class FreeBlockHistogram {
public:
    FreeBlockHistogram() = default;

    void record(size_t size) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        hist_.record(size);
    }

    [[nodiscard]] std::vector<SizeBucket> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return hist_.buckets();
    }

    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        hist_ = AllocationHistogram();
    }

private:
    mutable std::mutex mutex_;
    AllocationHistogram hist_;
};

struct FragmentationReport {
    double external_fragmentation_ratio{0.0};
    uint64_t peak_allocated_bytes{0};
    uint64_t current_allocated_bytes{0};
    uint64_t total_freed_bytes{0};
    uint64_t total_freed_blocks{0};
    double avg_freed_block_size{0.0};
    std::vector<SizeBucket> active_buckets;
    std::vector<SizeBucket> freed_buckets;
};

class FragmentationAnalyzer {
public:
    static FragmentationReport analyze(
        const MemoryStatsSnapshot& stats,
        const std::vector<AllocationRecord>& active_records,
        const FreeBlockHistogram& free_hist)
    {
        FragmentationReport report;
        report.peak_allocated_bytes = stats.peak_allocated_bytes;
        report.current_allocated_bytes = stats.current_allocated_bytes;
        report.total_freed_bytes = stats.total_freed_bytes;
        report.total_freed_blocks = stats.total_free_count;

        if (stats.total_free_count > 0) {
            report.avg_freed_block_size = static_cast<double>(stats.total_freed_bytes) / static_cast<double>(stats.total_free_count);
        }

        if (stats.peak_allocated_bytes > 0) {
            report.external_fragmentation_ratio = 1.0 - (static_cast<double>(stats.current_allocated_bytes) / static_cast<double>(stats.peak_allocated_bytes));
        }

        AllocationHistogram active_hist;
        active_hist.feed(active_records);
        report.active_buckets = active_hist.buckets();
        report.freed_buckets = free_hist.snapshot();

        return report;
    }
};

}
