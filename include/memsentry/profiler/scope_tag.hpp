#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace memsentry::profiler {

#if defined(_WIN32)
extern DWORD g_tls_tag_index;
inline const char* get_current_tag() noexcept {
    if (g_tls_tag_index == TLS_OUT_OF_INDEXES)
        return "General";
    auto* t = static_cast<const char*>(TlsGetValue(g_tls_tag_index));
    return t ? t : "General";
}
inline void set_current_tag(const char* tag) noexcept {
    if (g_tls_tag_index != TLS_OUT_OF_INDEXES) {
        TlsSetValue(g_tls_tag_index, const_cast<char*>(tag));
    }
}
#else
extern thread_local const char* g_active_scope_tag;
inline const char* get_current_tag() noexcept {
    return g_active_scope_tag ? g_active_scope_tag : "General";
}
inline void set_current_tag(const char* tag) noexcept {
    g_active_scope_tag = tag;
}
#endif

class ScopedTag {
public:
    explicit ScopedTag(const char* tag) noexcept : prev_(get_current_tag()) { set_current_tag(tag); }
    ~ScopedTag() noexcept { set_current_tag(prev_); }

    ScopedTag(const ScopedTag&) = delete;
    ScopedTag& operator=(const ScopedTag&) = delete;

    [[nodiscard]] static const char* current() noexcept { return get_current_tag(); }
    [[nodiscard]] static const char* get_raw() noexcept { return get_current_tag(); }

private:
    const char* prev_{nullptr};
};

}  // namespace memsentry::profiler

#define MEMSENTRY_CONCAT_INNER(a, b) a##b
#define MEMSENTRY_CONCAT(a, b) MEMSENTRY_CONCAT_INNER(a, b)
#define MEMSENTRY_SCOPE_TAG(tag_name)                                                                                  \
    ::memsentry::profiler::ScopedTag MEMSENTRY_CONCAT(_memsentry_tag_scope_, __LINE__)(tag_name)
