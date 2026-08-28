#include "memsentry/profiler/snapshot.hpp"

#include <unordered_map>
#include <unordered_set>

namespace memsentry::profiler {

SnapshotDiff compare_snapshots(const HeapSnapshot& before, const HeapSnapshot& after) {
    SnapshotDiff diff;
    diff.before_label = before.label;
    diff.after_label = after.label;
    diff.start_time = before.timestamp;
    diff.end_time = after.timestamp;

    diff.net_bytes_delta = static_cast<int64_t>(after.stats.current_allocated_bytes) -
                           static_cast<int64_t>(before.stats.current_allocated_bytes);
    diff.net_allocations_delta = static_cast<int64_t>(after.stats.active_allocation_count) -
                                 static_cast<int64_t>(before.stats.active_allocation_count);

    std::unordered_map<uint64_t, const AllocationRecord*> before_map;
    for (const auto& rec : before.active_allocations) {
        before_map[rec.allocation_id] = &rec;
    }

    std::unordered_set<uint64_t> matched_ids;

    for (const auto& rec : after.active_allocations) {
        auto it = before_map.find(rec.allocation_id);
        if (it != before_map.end()) {
            diff.persistent_allocations.push_back(rec);
            matched_ids.insert(rec.allocation_id);
        } else {
            diff.new_allocations.push_back(rec);
        }
    }

    for (const auto& rec : before.active_allocations) {
        if (matched_ids.find(rec.allocation_id) == matched_ids.end()) {
            diff.freed_allocations.push_back(rec);
        }
    }

    return diff;
}

}  // namespace memsentry::profiler
