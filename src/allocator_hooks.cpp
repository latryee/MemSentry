#include "memsentry/core/allocator_hooks.hpp"

#include "memsentry/core/header.hpp"
#include "memsentry/core/recursion_guard.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

namespace memsentry::core {
int g_hooks_anchor = 42;

void* raw_system_alloc(size_t size) noexcept {
    void* p = std::malloc(size);
    return p;
}

void raw_system_free(void* ptr) noexcept {
    if (!ptr)
        return;
    std::free(ptr);
}

void* raw_system_realloc(void* ptr, size_t new_size) noexcept {
    if (!ptr)
        return std::malloc(new_size);
    if (new_size == 0) {
        std::free(ptr);
        return nullptr;
    }
    return std::realloc(ptr, new_size);
}

}  // namespace memsentry::core

extern "C" {

void* memsentry_malloc(size_t size) noexcept {
    return memsentry::core::track_alloc(size, 16, "malloc");
}

void* memsentry_calloc(size_t num, size_t size) noexcept {
    size_t total = num * size;
    void* ptr = memsentry::core::track_alloc(total, 16, "calloc");
    if (ptr) {
        std::memset(ptr, 0, total);
    }
    return ptr;
}

void* memsentry_realloc(void* ptr, size_t new_size) noexcept {
    return memsentry::core::track_realloc(ptr, new_size);
}

void memsentry_free(void* ptr) noexcept {
    memsentry::core::track_free(ptr);
}

void* memsentry_aligned_alloc(size_t alignment, size_t size) noexcept {
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

#if defined(__GNUC__) || defined(__clang__)
#define MEMSENTRY_WEAK __attribute__((weak))
#else
#define MEMSENTRY_WEAK
#endif

MEMSENTRY_WEAK void* operator new(std::size_t size) {
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

MEMSENTRY_WEAK void* operator new[](std::size_t size) {
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

MEMSENTRY_WEAK void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return memsentry::core::track_alloc(size);
}

MEMSENTRY_WEAK void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return memsentry::core::track_alloc(size);
}

MEMSENTRY_WEAK void* operator new(std::size_t size, std::align_val_t al) {
    void* ptr = memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

MEMSENTRY_WEAK void* operator new[](std::size_t size, std::align_val_t al) {
    void* ptr = memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

MEMSENTRY_WEAK void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    return memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
}

MEMSENTRY_WEAK void* operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    return memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
}

MEMSENTRY_WEAK void operator delete(void* ptr) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete[](void* ptr) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete(void* ptr, std::size_t) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete[](void* ptr, std::size_t) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete(void* ptr, std::align_val_t) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete[](void* ptr, std::align_val_t) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    memsentry::core::track_free(ptr);
}

MEMSENTRY_WEAK void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    memsentry::core::track_free(ptr);
}

#if defined(_MSC_VER)
void* operator new(std::size_t size, int, const char*, int) {
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size, int, const char*, int) {
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr, int, const char*, int) noexcept {
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, int, const char*, int) noexcept {
    memsentry::core::track_free(ptr);
}
#endif
