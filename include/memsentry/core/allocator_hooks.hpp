#pragma once

#include <cstddef>
#include <new>

namespace memsentry::core {

void* track_alloc(size_t size, size_t alignment = 16, const char* tag = nullptr) noexcept;
void track_free(void* ptr) noexcept;
void* track_realloc(void* ptr, size_t new_size) noexcept;

}
