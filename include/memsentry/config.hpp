#pragma once

#include <cstddef>
#include <cstdint>

namespace memsentry {

struct Config {
    bool enable_canary = true;
    bool enable_stacktrace = true;
    bool auto_report_on_exit = true;
    bool exit_with_code_on_leak = false;
    uint32_t max_stack_depth = 32;
    uint32_t stack_skip_frames = 2;
    size_t canary_footer_size = 16;
    size_t alignment = 16;
    const char* default_tag = "General";
};

inline constexpr uint64_t CANARY_HEADER_MAGIC = 0xDEADBEEFCAFEBABEULL;
inline constexpr uint64_t CANARY_FOOTER_MAGIC = 0xBAADF00D5EADC0DEULL;
inline constexpr size_t SHARD_COUNT = 64;
inline constexpr size_t DEFAULT_ALIGNMENT = 16;

}
