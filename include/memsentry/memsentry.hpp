#pragma once

#include "memsentry/config.hpp"
#include "memsentry/types.hpp"
#include "memsentry/profiler/scope_tag.hpp"
#include "memsentry/profiler/snapshot.hpp"
#include "memsentry/profiler/histogram.hpp"
#include "memsentry/reporter/console_reporter.hpp"
#include "memsentry/reporter/json_reporter.hpp"
#include "memsentry/reporter/html_reporter.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace memsentry {

void init(const Config& config = {});
void shutdown();

[[nodiscard]] bool is_initialized() noexcept;
[[nodiscard]] MemoryStatsSnapshot get_stats() noexcept;
[[nodiscard]] bool has_leaks() noexcept;
[[nodiscard]] std::vector<AllocationRecord> get_active_allocations();

void dump_leaks(std::ostream& os = std::cout);
bool export_json(const std::string& filepath);
bool export_html(const std::string& filepath);

profiler::HeapSnapshot take_snapshot(const std::string& label);

}
