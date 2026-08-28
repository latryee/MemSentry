#include "memsentry/core/allocator_hooks.hpp"

#include "memsentry/core/recursion_guard.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace memsentry::core {
int g_hooks_anchor = 42;

void* raw_system_alloc(size_t size) noexcept {
#if defined(_WIN32)
    return HeapAlloc(GetProcessHeap(), 0, size);
#else
    typedef void* (*real_malloc_t)(size_t);
    static real_malloc_t real_malloc = (real_malloc_t)dlsym(RTLD_NEXT, "malloc");
    return real_malloc ? real_malloc(size) : std::malloc(size);
#endif
}

void raw_system_free(void* ptr) noexcept {
    if (!ptr)
        return;
#if defined(_WIN32)
    HeapFree(GetProcessHeap(), 0, ptr);
#else
    typedef void (*real_free_t)(void*);
    static real_free_t real_free = (real_free_t)dlsym(RTLD_NEXT, "free");
    if (real_free)
        real_free(ptr);
    else
        std::free(ptr);
#endif
}

void* raw_system_realloc(void* ptr, size_t new_size) noexcept {
#if defined(_WIN32)
    if (!ptr)
        return raw_system_alloc(new_size);
    if (new_size == 0) {
        raw_system_free(ptr);
        return nullptr;
    }
    return HeapReAlloc(GetProcessHeap(), 0, ptr, new_size);
#else
    typedef void* (*real_realloc_t)(void*, size_t);
    static real_realloc_t real_realloc = (real_realloc_t)dlsym(RTLD_NEXT, "realloc");
    return real_realloc ? real_realloc(ptr, new_size) : std::realloc(ptr, new_size);
#endif
}

}  // namespace memsentry::core

extern "C" {

void* memsentry_malloc(size_t size) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return memsentry::core::raw_system_alloc(size);
    }
    return memsentry::core::track_alloc(size, 16, "malloc");
}

void* memsentry_calloc(size_t num, size_t size) noexcept {
    size_t total = num * size;
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = memsentry::core::raw_system_alloc(total);
        if (p)
            std::memset(p, 0, total);
        return p;
    }
    void* ptr = memsentry::core::track_alloc(total, 16, "calloc");
    if (ptr) {
        std::memset(ptr, 0, total);
    }
    return ptr;
}

void* memsentry_realloc(void* ptr, size_t new_size) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return memsentry::core::raw_system_realloc(ptr, new_size);
    }
    return memsentry::core::track_realloc(ptr, new_size);
}

void memsentry_free(void* ptr) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void* memsentry_aligned_alloc(size_t alignment, size_t size) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return memsentry::core::raw_system_alloc(size);
    }
    return memsentry::core::track_alloc(size, alignment, "aligned_alloc");
}

int memsentry_posix_memalign(void** memptr, size_t alignment, size_t size) noexcept {
    if (!memptr)
        return 22;  // EINVAL
    if ((alignment & (alignment - 1)) != 0 || (alignment % sizeof(void*)) != 0) {
        return 22;  // EINVAL
    }
    void* p = memsentry_aligned_alloc(alignment, size);
    if (!p)
        return 12;  // ENOMEM
    *memptr = p;
    return 0;
}

#if defined(_WIN32)
void* memsentry_aligned_malloc(size_t size, size_t alignment) noexcept {
    return memsentry_aligned_alloc(alignment, size);
}

void memsentry_aligned_free(void* ptr) noexcept {
    memsentry_free(ptr);
}
#endif
}

void* operator new(std::size_t size) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = memsentry::core::raw_system_alloc(size);
        if (!p)
            throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = memsentry::core::raw_system_alloc(size);
        if (!p)
            throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return memsentry::core::raw_system_alloc(size);
    }
    return memsentry::core::track_alloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return memsentry::core::raw_system_alloc(size);
    }
    return memsentry::core::track_alloc(size);
}

void* operator new(std::size_t size, std::align_val_t al) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = memsentry::core::raw_system_alloc(size);
        if (!p)
            throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size, std::align_val_t al) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = memsentry::core::raw_system_alloc(size);
        if (!p)
            throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return memsentry::core::raw_system_alloc(size);
    }
    return memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
}

void* operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return memsentry::core::raw_system_alloc(size);
    }
    return memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
}

void operator delete(void* ptr) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

#if defined(_MSC_VER)
void* operator new(std::size_t size, int, const char*, int) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = memsentry::core::raw_system_alloc(size);
        if (!p)
            throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size, int, const char*, int) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = memsentry::core::raw_system_alloc(size);
        if (!p)
            throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr, int, const char*, int) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, int, const char*, int) noexcept {
    if (!ptr)
        return;
    if (memsentry::core::RecursionGuard::is_active()) {
        memsentry::core::raw_system_free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}
#endif
