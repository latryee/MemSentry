#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "memsentry/types.hpp"

namespace memsentry::core {

struct BlockHeader {
    uint64_t magic;
    uint64_t allocation_id;
    size_t requested_size;
    size_t total_size;
    size_t user_offset;
    size_t alignment;
    const char* tag;
    bool has_canary;
};

inline size_t align_up(size_t value, size_t alignment) noexcept {
    if (alignment == 0) return value;
    return (value + (alignment - 1)) & ~(alignment - 1);
}

inline size_t calculate_total_size(size_t requested_size, size_t alignment, size_t footer_size) noexcept {
    if (alignment == 0) alignment = DEFAULT_ALIGNMENT;
    size_t max_offset = sizeof(BlockHeader) + alignment;
    if (requested_size > SIZE_MAX - max_offset - footer_size) {
        return 0;
    }
    return max_offset + requested_size + footer_size;
}

inline void* init_block(void* raw_memory, size_t requested_size, size_t alignment, size_t footer_size,
                        uint64_t alloc_id, const char* tag, bool enable_canary) noexcept {
    if (!raw_memory) return nullptr;

    if (alignment == 0) alignment = DEFAULT_ALIGNMENT;

    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_memory);
    uintptr_t header_end = raw_addr + sizeof(BlockHeader);
    uintptr_t user_addr = (header_end + (alignment - 1)) & ~(alignment - 1);
    size_t user_offset = user_addr - raw_addr;
    size_t total_size = user_offset + requested_size + (enable_canary ? footer_size : 0);

    auto* header = reinterpret_cast<BlockHeader*>(raw_memory);
    header->magic = CANARY_HEADER_MAGIC;
    header->allocation_id = alloc_id;
    header->requested_size = requested_size;
    header->total_size = total_size;
    header->user_offset = user_offset;
    header->alignment = alignment;
    header->tag = tag ? tag : "General";
    header->has_canary = enable_canary;

    uint8_t* user_ptr = reinterpret_cast<uint8_t*>(user_addr);

    if (enable_canary && footer_size >= sizeof(uint64_t)) {
        uint8_t* footer_ptr = user_ptr + requested_size;
        for (size_t i = 0; i < footer_size; i += sizeof(uint64_t)) {
            size_t copy_len = (footer_size - i < sizeof(uint64_t)) ? (footer_size - i) : sizeof(uint64_t);
            std::memcpy(footer_ptr + i, &CANARY_FOOTER_MAGIC, copy_len);
        }
    }

    return user_ptr;
}

inline BlockHeader* get_header_from_raw_ptr(void* raw_ptr) noexcept {
    return reinterpret_cast<BlockHeader*>(raw_ptr);
}

inline BlockHeader* get_header_from_user_ptr(const void* user_ptr, size_t alignment = DEFAULT_ALIGNMENT) noexcept {
    if (!user_ptr) return nullptr;
    if (alignment == 0) alignment = DEFAULT_ALIGNMENT;
    uintptr_t user_addr = reinterpret_cast<uintptr_t>(user_ptr);
    size_t min_offset = sizeof(BlockHeader);
    uintptr_t candidate_addr = user_addr - align_up(min_offset, alignment);
    auto* candidate = reinterpret_cast<BlockHeader*>(candidate_addr);
    if (candidate && candidate->magic == CANARY_HEADER_MAGIC) {
        return candidate;
    }
    return nullptr;
}

inline CorruptionType verify_canary(const BlockHeader* header, size_t footer_size) noexcept {
    if (!header) return CorruptionType::UNTRACKED_POINTER;
    if (header->magic != CANARY_HEADER_MAGIC) {
        return CorruptionType::HEADER_CORRUPTED;
    }

    if (header->has_canary && footer_size >= sizeof(uint64_t)) {
        const uint8_t* user_ptr = reinterpret_cast<const uint8_t*>(header) + header->user_offset;
        const uint8_t* footer_ptr = user_ptr + header->requested_size;
        for (size_t i = 0; i < footer_size; i += sizeof(uint64_t)) {
            uint64_t val = 0;
            size_t copy_len = (footer_size - i < sizeof(uint64_t)) ? (footer_size - i) : sizeof(uint64_t);
            std::memcpy(&val, footer_ptr + i, copy_len);
            uint64_t expected = CANARY_FOOTER_MAGIC;
            if (std::memcmp(&val, &expected, copy_len) != 0) {
                return CorruptionType::FOOTER_CORRUPTED;
            }
        }
    }

    return CorruptionType::NONE;
}

}
