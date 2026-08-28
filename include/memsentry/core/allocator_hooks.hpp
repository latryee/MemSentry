#pragma once

#include "memsentry/core/msvc_debug_guard.hpp"
#include "memsentry/types.hpp"

#include <cstddef>
#include <new>

namespace memsentry::core {

extern int g_hooks_anchor;
inline int ensure_hooks_linked() noexcept {
    return g_hooks_anchor;
}

MEMSENTRY_NO_SANITIZE void* raw_system_alloc(size_t size) noexcept;
MEMSENTRY_NO_SANITIZE void raw_system_free(void* ptr) noexcept;
MEMSENTRY_NO_SANITIZE void* raw_system_realloc(void* ptr, size_t new_size) noexcept;

MEMSENTRY_NO_SANITIZE void* track_alloc(size_t size, size_t alignment = 16, const char* tag = nullptr) noexcept;
MEMSENTRY_NO_SANITIZE void track_free(void* ptr) noexcept;
MEMSENTRY_NO_SANITIZE void* track_realloc(void* ptr, size_t new_size) noexcept;

}  // namespace memsentry::core

extern "C" {
MEMSENTRY_NO_SANITIZE void* memsentry_malloc(size_t size) noexcept;
MEMSENTRY_NO_SANITIZE void* memsentry_calloc(size_t num, size_t size) noexcept;
MEMSENTRY_NO_SANITIZE void* memsentry_realloc(void* ptr, size_t new_size) noexcept;
MEMSENTRY_NO_SANITIZE void memsentry_free(void* ptr) noexcept;
MEMSENTRY_NO_SANITIZE void* memsentry_aligned_alloc(size_t alignment, size_t size) noexcept;
MEMSENTRY_NO_SANITIZE int memsentry_posix_memalign(void** memptr, size_t alignment, size_t size) noexcept;

#if defined(_WIN32)
MEMSENTRY_NO_SANITIZE void* memsentry_aligned_malloc(size_t size, size_t alignment) noexcept;
MEMSENTRY_NO_SANITIZE void memsentry_aligned_free(void* ptr) noexcept;
#endif
}
