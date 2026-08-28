#include "memsentry/core/header.hpp"
#include <iostream>
#include <cstdint>
#include <cstddef>

int main() {
    using namespace memsentry::core;

    constexpr size_t REQ_SIZE = 64;
    constexpr size_t ALIGN = 16;
    constexpr size_t FOOTER_SIZE = 16;

    alignas(16) uint8_t memory[512] = {0};

    void* user_ptr = init_block(memory, REQ_SIZE, ALIGN, FOOTER_SIZE, 1, "UnitTest", true);
    if (!user_ptr) return 1;

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

    std::cout << "[PASSED] Canary verification unit test completed successfully.\n";
    return 0;
}
