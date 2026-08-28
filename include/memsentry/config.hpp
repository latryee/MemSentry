#pragma once

#include "memsentry/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace memsentry {

struct Config {
    bool enable_stacktrace{true};
    uint32_t max_stack_depth{32};
    uint32_t stack_skip_frames{3};
    bool enable_canary{true};
    size_t canary_footer_size{16};
    size_t alignment{16};
    const char* default_tag{"General"};
    bool auto_report_on_exit{false};
    bool exit_with_code_on_leak{false};
    ReportFormat report_format{ReportFormat::CONSOLE_ANSI};
    std::string export_path{""};

    // Sampling Mode (%N Sampling for Production Profiling)
    uint32_t sampling_percentage{100};
    uint32_t sample_every_n{1};

    // Memory Limit Watchdog
    uint64_t max_heap_bytes{0};
    void (*on_limit_exceeded)(uint64_t current_bytes, uint64_t limit_bytes){nullptr};
};

}  // namespace memsentry
