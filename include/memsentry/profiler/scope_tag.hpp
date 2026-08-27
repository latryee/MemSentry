#pragma once

namespace memsentry::profiler {

#if defined(_MSC_VER)
inline __declspec(thread) const char* g_active_tag = nullptr;
#elif defined(__GNUC__) || defined(__clang__)
inline __thread const char* g_active_tag = nullptr;
#else
inline thread_local const char* g_active_tag = nullptr;
#endif

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

}

#define MEMSENTRY_CONCAT_INNER(a, b) a##b
#define MEMSENTRY_CONCAT(a, b) MEMSENTRY_CONCAT_INNER(a, b)
#define MEMSENTRY_SCOPE_TAG(tag_name) ::memsentry::profiler::ScopedTag MEMSENTRY_CONCAT(_memsentry_tag_scope_, __LINE__)(tag_name)
