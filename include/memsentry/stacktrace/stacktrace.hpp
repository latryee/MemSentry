#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <mutex>
#include <unordered_map>
#include "memsentry/types.hpp"

namespace memsentry::stacktrace {

class StackTraceProvider {
public:
    static StackTraceProvider& instance() noexcept;

    uint16_t capture(void** out_frames, uint32_t max_depth, uint32_t skip_frames) noexcept;
    std::vector<StackFrame> resolve(const void* const* frames, uint16_t count);
    StackFrame resolve_frame(uintptr_t address);

    void initialize();
    void cleanup();

private:
    StackTraceProvider();
    ~StackTraceProvider();

    StackTraceProvider(const StackTraceProvider&) = delete;
    StackTraceProvider& operator=(const StackTraceProvider&) = delete;

    bool initialized_ = false;
    std::mutex symbol_mutex_;
    std::unordered_map<uintptr_t, StackFrame> symbol_cache_;
};

}
