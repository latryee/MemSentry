#pragma once

#include "memsentry/types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace memsentry::core {

/**
 * @brief BlockHeader metadata placed at raw allocation start.
 * Guarantees strict alignof(std::max_align_t) and AVX-512 (64-byte) alignment for user pointers.
 */
struct alignas(16) BlockHeader {
    uint64_t magic;          // CANARY_HEADER_MAGIC
    uint64_t allocation_id;  // Unique sequential allocation ID
    size_t requested_size;   // User requested payload size in bytes
    size_t total_size;       // Full allocation footprint in bytes
    size_t user_offset;      // Byte offset from raw_ptr to user_ptr
    size_t alignment;        // Effective hardware alignment (e.g. 16 or 64 for AVX-512)
    const char* tag;         // Active scope tag
    bool has_canary;         // Canary footer enabled flag
    uint8_t pad[3];          // Struct padding
    uint32_t thread_id;      // Allocating thread ID
};

static_assert(sizeof(BlockHeader) == 64, "BlockHeader must be 64 bytes for cacheline alignment");

inline size_t calculate_total_size(size_t requested_size, size_t alignment, size_t footer_size) noexcept {
    size_t eff_align = (alignment > alignof(std::max_align_t)) ? alignment : alignof(std::max_align_t);
    size_t max_offset = sizeof(BlockHeader) + sizeof(uint32_t) + eff_align;
    if (requested_size > SIZE_MAX - max_offset - footer_size) {
        return 0;
    }
    return max_offset + requested_size + footer_size;
}

inline void* init_block(void* raw_memory, size_t requested_size, size_t alignment, size_t footer_size,
                        uint64_t alloc_id, const char* tag, bool enable_canary, uint32_t thread_id = 0) noexcept {
    if (!raw_memory)
        return nullptr;

    size_t eff_align = (alignment > alignof(std::max_align_t)) ? alignment : alignof(std::max_align_t);

    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_memory);
    uintptr_t min_user_addr = raw_addr + sizeof(BlockHeader) + sizeof(uint32_t);
    uintptr_t user_addr = (min_user_addr + (eff_align - 1)) & ~(static_cast<uintptr_t>(eff_align - 1));
    size_t user_offset = user_addr - raw_addr;
    size_t total_size = user_offset + requested_size + (enable_canary ? footer_size : 0);

    auto* header = reinterpret_cast<BlockHeader*>(raw_memory);
    header->magic = CANARY_HEADER_MAGIC;
    header->allocation_id = alloc_id;
    header->requested_size = requested_size;
    header->total_size = total_size;
    header->user_offset = user_offset;
    header->alignment = eff_align;
    header->tag = tag ? tag : "General";
    header->has_canary = enable_canary;
    header->pad[0] = 0;
    header->pad[1] = 0;
    header->pad[2] = 0;
    header->thread_id = thread_id;

    // Store the 32-bit user_offset immediately before user_ptr for fast O(1) backward header location
    *reinterpret_cast<uint32_t*>(user_addr - sizeof(uint32_t)) = static_cast<uint32_t>(user_offset);

    uint8_t* user_ptr = reinterpret_cast<uint8_t*>(user_addr);

    // Initialize footer red-zone canary
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

inline const BlockHeader* get_header_from_raw_ptr(const void* raw_ptr) noexcept {
    return reinterpret_cast<const BlockHeader*>(raw_ptr);
}

inline BlockHeader* get_header_from_user_ptr(const void* user_ptr) noexcept {
    if (!user_ptr)
        return nullptr;
    uintptr_t user_addr = reinterpret_cast<uintptr_t>(user_ptr);
    if (user_addr < sizeof(BlockHeader) + sizeof(uint32_t))
        return nullptr;

    // Read backward offset
    uint32_t offset = *reinterpret_cast<const uint32_t*>(user_addr - sizeof(uint32_t));
    if (offset < sizeof(BlockHeader) + sizeof(uint32_t) || offset > 4096 + sizeof(BlockHeader)) {
        return nullptr;
    }
    uintptr_t header_addr = user_addr - offset;
    auto* candidate = reinterpret_cast<BlockHeader*>(header_addr);
    if (candidate->magic == CANARY_HEADER_MAGIC) {
        if (candidate->user_offset == offset) {
            return candidate;
        }
    }
    return nullptr;
}

inline CorruptionType verify_canary(const BlockHeader* header, size_t footer_size) noexcept {
    if (!header)
        return CorruptionType::UNTRACKED_POINTER;
    if (header->magic == CANARY_FREED_MAGIC) {
        return CorruptionType::DOUBLE_FREE;
    }
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

inline void poison_block(BlockHeader* header, size_t footer_size) noexcept {
    if (!header)
        return;
    header->magic = CANARY_FREED_MAGIC;
    if (header->user_offset >= sizeof(BlockHeader) + sizeof(uint32_t)) {
        uint8_t* user_ptr = reinterpret_cast<uint8_t*>(header) + header->user_offset;
        *reinterpret_cast<uint32_t*>(user_ptr - sizeof(uint32_t)) = 0;
        if (header->has_canary && footer_size >= sizeof(uint64_t)) {
            uint8_t* footer_ptr = user_ptr + header->requested_size;
            for (size_t i = 0; i < footer_size; i += sizeof(uint64_t)) {
                size_t copy_len = (footer_size - i < sizeof(uint64_t)) ? (footer_size - i) : sizeof(uint64_t);
                std::memcpy(footer_ptr + i, &CANARY_FREED_MAGIC, copy_len);
            }
        }
    }
    header->user_offset = 0;
}

}  // namespace memsentry::core
