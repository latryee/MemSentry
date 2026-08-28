#pragma once

namespace memsentry::core {

extern thread_local int g_recursion_depth;

class RecursionGuard {
public:
    RecursionGuard() noexcept {
        ++g_recursion_depth;
    }

    ~RecursionGuard() noexcept {
        --g_recursion_depth;
    }

    RecursionGuard(const RecursionGuard&) = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;

    [[nodiscard]] static bool is_active() noexcept {
        return g_recursion_depth > 0;
    }
};

}
