#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace memsentry::profiler {

#if defined(_WIN32)
extern DWORD g_tls_tag_index;

class ScopedTag {
public:
    explicit ScopedTag(const char* tag) noexcept {
        if (g_tls_tag_index != TLS_OUT_OF_INDEXES) {
            prev_ = static_cast<const char*>(TlsGetValue(g_tls_tag_index));
            TlsSetValue(g_tls_tag_index, const_cast<char*>(tag));
        }
    }

    ~ScopedTag() noexcept {
        if (g_tls_tag_index != TLS_OUT_OF_INDEXES) {
            TlsSetValue(g_tls_tag_index, const_cast<char*>(prev_));
        }
    }

    ScopedTag(const ScopedTag&) = delete;
    ScopedTag& operator=(const ScopedTag&) = delete;

    [[nodiscard]] static const char* current() noexcept {
        if (g_tls_tag_index != TLS_OUT_OF_INDEXES) {
            auto* tag = static_cast<const char*>(TlsGetValue(g_tls_tag_index));
            return tag ? tag : "General";
        }
        return "General";
    }

private:
    const char* prev_ = nullptr;
};

#else

extern __thread const char* g_active_tag;

class ScopedTag {
public:
    explicit ScopedTag(const char* tag) noexcept : prev_(g_active_tag) {
        g_active_tag = tag;
    }

    ~ScopedTag() noexcept {
        g_active_tag = prev_;
    }

    ScopedTag(const ScopedTag&) = delete;
    ScopedTag& operator=(const ScopedTag&) = delete;

    [[nodiscard]] static const char* current() noexcept {
        return g_active_tag ? g_active_tag : "General";
    }

private:
    const char* prev_;
};

#endif

}

#define MEMSENTRY_CONCAT_INNER(a, b) a##b
#define MEMSENTRY_CONCAT(a, b) MEMSENTRY_CONCAT_INNER(a, b)
#define MEMSENTRY_SCOPE_TAG(tag_name) ::memsentry::profiler::ScopedTag MEMSENTRY_CONCAT(_memsentry_tag_scope_, __LINE__)(tag_name)
