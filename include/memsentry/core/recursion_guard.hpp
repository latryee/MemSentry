#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace memsentry::core {

#if defined(_WIN32)
extern DWORD g_tls_recursion_index;

class RecursionGuard {
public:
    RecursionGuard() noexcept {
        if (g_tls_recursion_index != TLS_OUT_OF_INDEXES) {
            prev_ = TlsGetValue(g_tls_recursion_index);
            TlsSetValue(g_tls_recursion_index, reinterpret_cast<void*>(static_cast<uintptr_t>(1)));
        }
    }

    ~RecursionGuard() noexcept {
        if (g_tls_recursion_index != TLS_OUT_OF_INDEXES) {
            TlsSetValue(g_tls_recursion_index, prev_);
        }
    }

    RecursionGuard(const RecursionGuard&) = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;

    [[nodiscard]] static bool is_active() noexcept {
        if (g_tls_recursion_index != TLS_OUT_OF_INDEXES) {
            return TlsGetValue(g_tls_recursion_index) != nullptr;
        }
        return false;
    }

private:
    void* prev_ = nullptr;
};

#else

extern __thread bool g_recursion_active;

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

#endif

}
