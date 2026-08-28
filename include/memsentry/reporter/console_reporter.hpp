#pragma once

#include "memsentry/profiler/snapshot.hpp"
#include "memsentry/types.hpp"

#include <iostream>
#include <vector>

namespace memsentry::reporter {

class ConsoleReporter {
public:
    static void print_summary(std::ostream& os, const MemoryStatsSnapshot& stats, size_t leak_count, size_t leak_bytes);
    static void print_leaks(std::ostream& os, const std::vector<AllocationRecord>& leaks);
    static void print_diff(std::ostream& os, const profiler::SnapshotDiff& diff);
    static void print_corruption_alert(std::ostream& os, const void* ptr, CorruptionType type);
};

}  // namespace memsentry::reporter
