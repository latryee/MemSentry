#pragma once

namespace memsentry::profiler {

class ScopedTag {
public:
    explicit ScopedTag(const char* tag) noexcept : prev_(active_tag_) {
        active_tag_ = tag;
    }

    ~ScopedTag() noexcept {
        active_tag_ = prev_;
    }

    ScopedTag(const ScopedTag&) = delete;
    ScopedTag& operator=(const ScopedTag&) = delete;

    [[nodiscard]] static const char* current() noexcept {
        return active_tag_ ? active_tag_ : "General";
    }

private:
    static inline thread_local const char* active_tag_ = nullptr;
    const char* prev_{nullptr};
};

}

#define MEMSENTRY_CONCAT_INNER(a, b) a##b
#define MEMSENTRY_CONCAT(a, b) MEMSENTRY_CONCAT_INNER(a, b)
#define MEMSENTRY_SCOPE_TAG(tag_name) ::memsentry::profiler::ScopedTag MEMSENTRY_CONCAT(_memsentry_tag_scope_, __LINE__)(tag_name)
