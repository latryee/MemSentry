#pragma once

#include <string>
#include <vector>
#include "memsentry/types.hpp"
#include "memsentry/profiler/snapshot.hpp"

namespace memsentry::reporter {

class JsonReporter {
public:
    static std::string serialize(const MemoryStatsSnapshot& stats, const std::vector<AllocationRecord>& records);
    static bool write_file(const std::string& filepath, const MemoryStatsSnapshot& stats, const std::vector<AllocationRecord>& records);
};

}
