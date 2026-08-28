#pragma once

namespace memsentry::core {

#if defined(_MSC_VER)
extern __declspec(thread) int g_recursion_depth;
#else
extern thread_local int g_recursion_depth;
#endif

class RecursionGuard {
public:
    RecursionGuard() noexcept { ++g_recursion_depth; }

    ~RecursionGuard() noexcept { --g_recursion_depth; }

    RecursionGuard(const RecursionGuard&) = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;

    [[nodiscard]] static bool is_active() noexcept { return g_recursion_depth > 0; }
};

}  // namespace memsentry::core
