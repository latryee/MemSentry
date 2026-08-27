#pragma once

#include <string>
#include <vector>
#include "memsentry/types.hpp"

namespace memsentry::reporter {

class HtmlReporter {
public:
    static std::string generate(const MemoryStatsSnapshot& stats, const std::vector<AllocationRecord>& records);
    static bool write_file(const std::string& filepath, const MemoryStatsSnapshot& stats, const std::vector<AllocationRecord>& records);
};

}
