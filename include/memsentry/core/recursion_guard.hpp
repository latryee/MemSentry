#pragma once

namespace memsentry::core {

class RecursionGuard {
public:
    static inline thread_local bool active = false;

    RecursionGuard() noexcept : state_(active) {
        active = true;
    }

    ~RecursionGuard() noexcept {
        active = state_;
    }

    RecursionGuard(const RecursionGuard&) = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;

    [[nodiscard]] bool was_already_active() const noexcept {
        return state_;
    }

private:
    bool state_;
};

}
