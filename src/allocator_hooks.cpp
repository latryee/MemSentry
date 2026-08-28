#include "memsentry/core/allocator_hooks.hpp"
#include "memsentry/core/recursion_guard.hpp"
#include <new>
#include <cstdlib>

namespace memsentry::core {
int g_hooks_anchor = 42;
}

void* operator new(std::size_t size) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = std::malloc(size);
        if (!p) throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = std::malloc(size);
        if (!p) throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return std::malloc(size);
    }
    return memsentry::core::track_alloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return std::malloc(size);
    }
    return memsentry::core::track_alloc(size);
}

void* operator new(std::size_t size, std::align_val_t al) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = std::malloc(size);
        if (!p) throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size, std::align_val_t al) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = std::malloc(size);
        if (!p) throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return std::malloc(size);
    }
    return memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
}

void* operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    if (memsentry::core::RecursionGuard::is_active()) {
        return std::malloc(size);
    }
    return memsentry::core::track_alloc(size, static_cast<std::size_t>(al));
}

void operator delete(void* ptr) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

#if defined(_MSC_VER)
void* operator new(std::size_t size, int, const char*, int) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = std::malloc(size);
        if (!p) throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size, int, const char*, int) {
    if (memsentry::core::RecursionGuard::is_active()) {
        void* p = std::malloc(size);
        if (!p) throw std::bad_alloc();
        return p;
    }
    void* ptr = memsentry::core::track_alloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr, int, const char*, int) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}

void operator delete[](void* ptr, int, const char*, int) noexcept {
    if (!ptr) return;
    if (memsentry::core::RecursionGuard::is_active()) {
        std::free(ptr);
        return;
    }
    memsentry::core::track_free(ptr);
}
#endif
