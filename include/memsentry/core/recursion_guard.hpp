#pragma once

#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace memsentry::core {

#if defined(_WIN32)
extern DWORD g_tls_guard_index;
inline bool is_guard_active() noexcept {
    if (g_tls_guard_index == TLS_OUT_OF_INDEXES)
        return false;
    return TlsGetValue(g_tls_guard_index) != nullptr;
}
inline void inc_guard() noexcept {
    if (g_tls_guard_index != TLS_OUT_OF_INDEXES) {
        uintptr_t d = reinterpret_cast<uintptr_t>(TlsGetValue(g_tls_guard_index));
        TlsSetValue(g_tls_guard_index, reinterpret_cast<LPVOID>(d + 1));
    }
}
inline void dec_guard() noexcept {
    if (g_tls_guard_index != TLS_OUT_OF_INDEXES) {
        uintptr_t d = reinterpret_cast<uintptr_t>(TlsGetValue(g_tls_guard_index));
        if (d > 0) {
            TlsSetValue(g_tls_guard_index, reinterpret_cast<LPVOID>(d - 1));
        }
    }
}
#else
extern thread_local int g_recursion_depth;
inline bool is_guard_active() noexcept {
    return g_recursion_depth > 0;
}
inline void inc_guard() noexcept {
    ++g_recursion_depth;
}
inline void dec_guard() noexcept {
    --g_recursion_depth;
}
#endif

class RecursionGuard {
public:
    RecursionGuard() noexcept { inc_guard(); }
    ~RecursionGuard() noexcept { dec_guard(); }

    RecursionGuard(const RecursionGuard&) = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;

    [[nodiscard]] static bool is_active() noexcept { return is_guard_active(); }
};

}  // namespace memsentry::core
