#pragma once

#include "memsentry/profiler/snapshot.hpp"
#include "memsentry/types.hpp"

#include <string>
#include <vector>

namespace memsentry::reporter {

class JsonReporter {
public:
    static std::string serialize(const MemoryStatsSnapshot& stats, const std::vector<AllocationRecord>& records);
    static bool write_file(const std::string& filepath, const MemoryStatsSnapshot& stats,
                           const std::vector<AllocationRecord>& records);
};

}  // namespace memsentry::reporter
