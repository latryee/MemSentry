#include "memsentry/core/allocator_hooks.hpp"
#include <new>

void* operator new(std::size_t size) {
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size) {
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr) noexcept {
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr) noexcept {
    memsentry::core::track_free(ptr);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return memsentry::core::track_alloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return memsentry::core::track_alloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    memsentry::core::track_free(ptr);
}

#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)

void* operator new(std::size_t size, std::align_val_t al) {
    void* ptr = memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size, std::align_val_t al) {
    void* ptr = memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    memsentry::core::track_free(ptr);
}

#endif
