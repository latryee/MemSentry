#pragma once

namespace memsentry::core {

#if defined(_MSC_VER)
extern __declspec(thread) bool g_recursion_active;
#else
extern thread_local bool g_recursion_active;
#endif

class RecursionGuard {
public:
    RecursionGuard() noexcept : state_(g_recursion_active) {
        g_recursion_active = true;
    }

    ~RecursionGuard() noexcept {
        g_recursion_active = state_;
    }

    RecursionGuard(const RecursionGuard&) = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;

    [[nodiscard]] static bool is_active() noexcept {
        return g_recursion_active;
    }

private:
    bool state_;
};

}
