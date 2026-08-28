#pragma once

#include "memsentry/types.hpp"

#include <string>
#include <vector>

namespace memsentry::reporter {

class HtmlReporter {
public:
    static std::string generate(const MemoryStatsSnapshot& stats, const std::vector<AllocationRecord>& records);
    static bool write_file(const std::string& filepath, const MemoryStatsSnapshot& stats,
                           const std::vector<AllocationRecord>& records);
};

}  // namespace memsentry::reporter
