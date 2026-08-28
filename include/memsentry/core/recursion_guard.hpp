#pragma once

namespace memsentry::core {

class RecursionGuard {
public:
    RecursionGuard() noexcept {
        prev_ = active_;
        active_ = true;
    }

    ~RecursionGuard() noexcept {
        active_ = prev_;
    }

    RecursionGuard(const RecursionGuard&) = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;

    [[nodiscard]] static bool is_active() noexcept {
        return active_;
    }

private:
    static inline thread_local bool active_ = false;
    bool prev_{false};
};

}
