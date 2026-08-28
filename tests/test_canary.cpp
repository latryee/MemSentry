#include "memsentry/core/header.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>

int main() {
    using namespace memsentry::core;

    constexpr size_t REQ_SIZE = 64;
    constexpr size_t ALIGN = 16;
    constexpr size_t FOOTER_SIZE = 16;

    alignas(16) uint8_t memory[512] = {0};

    void* user_ptr = init_block(memory, REQ_SIZE, ALIGN, FOOTER_SIZE, 1, "UnitTest", true);
    if (!user_ptr)
        return 1;

    auto* header = get_header_from_raw_ptr(memory);
    if (!header || header->magic != memsentry::CANARY_HEADER_MAGIC || header->requested_size != REQ_SIZE) {
        return 1;
    }

    if (verify_canary(header, FOOTER_SIZE) != memsentry::CorruptionType::NONE) {
        return 1;
    }

    uint8_t* footer_byte = reinterpret_cast<uint8_t*>(user_ptr) + REQ_SIZE;
    *footer_byte ^= 0xFF;
    if (verify_canary(header, FOOTER_SIZE) != memsentry::CorruptionType::FOOTER_CORRUPTED) {
        return 1;
    }

    *footer_byte ^= 0xFF;
    if (verify_canary(header, FOOTER_SIZE) != memsentry::CorruptionType::NONE) {
        return 1;
    }

    header->magic = 0;
    if (verify_canary(header, FOOTER_SIZE) != memsentry::CorruptionType::HEADER_CORRUPTED) {
        return 1;
    }

    // Over-aligned allocation test (64-byte and 128-byte alignment)
    {
        constexpr size_t OVER_ALIGN = 64;
        constexpr size_t OVER_REQ_SIZE = 128;
        alignas(128) uint8_t over_memory[1024] = {0};

        void* over_user_ptr = init_block(over_memory, OVER_REQ_SIZE, OVER_ALIGN, FOOTER_SIZE, 2, "OverAligned", true);
        if (!over_user_ptr)
            return 1;

        uintptr_t user_addr = reinterpret_cast<uintptr_t>(over_user_ptr);
        if ((user_addr % OVER_ALIGN) != 0) {
            std::cerr << "[-] Over-aligned user pointer does not satisfy 64-byte alignment!\n";
            return 1;
        }

        auto* over_header = get_header_from_raw_ptr(over_memory);
        if (!over_header || over_header->magic != memsentry::CANARY_HEADER_MAGIC) {
            return 1;
        }

        if (verify_canary(over_header, FOOTER_SIZE) != memsentry::CorruptionType::NONE) {
            return 1;
        }

        // Corrupt canary footer immediately after user payload
        uint8_t* over_footer = reinterpret_cast<uint8_t*>(over_user_ptr) + OVER_REQ_SIZE;
        *over_footer ^= 0x55;
        if (verify_canary(over_header, FOOTER_SIZE) != memsentry::CorruptionType::FOOTER_CORRUPTED) {
            return 1;
        }

        // Restore and verify clean
        *over_footer ^= 0x55;
        if (verify_canary(over_header, FOOTER_SIZE) != memsentry::CorruptionType::NONE) {
            return 1;
        }
    }

    std::cout << "[PASSED] Canary verification unit test completed successfully.\n";
    return 0;
}
