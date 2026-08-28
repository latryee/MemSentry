#pragma once

#include <cstddef>
#include <new>

namespace memsentry::core {

extern int g_hooks_anchor;
inline int ensure_hooks_linked() noexcept {
    return g_hooks_anchor;
}

void* track_alloc(size_t size, size_t alignment = 16, const char* tag = nullptr) noexcept;
void track_free(void* ptr) noexcept;
void* track_realloc(void* ptr, size_t new_size) noexcept;

}
