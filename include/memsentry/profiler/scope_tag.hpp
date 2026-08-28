#pragma once

namespace memsentry::profiler {

#if defined(_MSC_VER)
extern __declspec(thread) const char* g_active_scope_tag;
#else
extern thread_local const char* g_active_scope_tag;
#endif

class ScopedTag {
public:
    explicit ScopedTag(const char* tag) noexcept : prev_(g_active_scope_tag) {
        g_active_scope_tag = tag;
    }

    ~ScopedTag() noexcept {
        g_active_scope_tag = prev_;
    }

    ScopedTag(const ScopedTag&) = delete;
    ScopedTag& operator=(const ScopedTag&) = delete;

    [[nodiscard]] static const char* current() noexcept {
        return g_active_scope_tag ? g_active_scope_tag : "General";
    }

private:
    const char* prev_{nullptr};
};

}

#define MEMSENTRY_CONCAT_INNER(a, b) a##b
#define MEMSENTRY_CONCAT(a, b) MEMSENTRY_CONCAT_INNER(a, b)
#define MEMSENTRY_SCOPE_TAG(tag_name) ::memsentry::profiler::ScopedTag MEMSENTRY_CONCAT(_memsentry_tag_scope_, __LINE__)(tag_name)
