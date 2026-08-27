#include "memsentry/core/header.hpp"
#include <cassert>
#include <vector>
#include <iostream>

int main() {
    using namespace memsentry::core;

    constexpr size_t REQ_SIZE = 64;
    constexpr size_t ALIGN = 16;
    constexpr size_t FOOTER_SIZE = 16;

    size_t total_size = calculate_total_size(REQ_SIZE, ALIGN, FOOTER_SIZE);
    std::vector<uint8_t> memory(total_size, 0);

    void* user_ptr = init_block(memory.data(), REQ_SIZE, ALIGN, FOOTER_SIZE, 1, "UnitTest", true);
    assert(user_ptr != nullptr);

    auto* header = get_header_from_raw_ptr(memory.data());
    assert(header != nullptr);
    assert(header->magic == memsentry::CANARY_HEADER_MAGIC);
    assert(header->requested_size == REQ_SIZE);

    assert(verify_canary(header, FOOTER_SIZE) == memsentry::CorruptionType::NONE);

    uint8_t* footer_byte = reinterpret_cast<uint8_t*>(user_ptr) + REQ_SIZE;
    *footer_byte ^= 0xFF;
    assert(verify_canary(header, FOOTER_SIZE) == memsentry::CorruptionType::FOOTER_CORRUPTED);

    *footer_byte ^= 0xFF;
    assert(verify_canary(header, FOOTER_SIZE) == memsentry::CorruptionType::NONE);

    header->magic = 0;
    assert(verify_canary(header, FOOTER_SIZE) == memsentry::CorruptionType::HEADER_CORRUPTED);

    std::cout << "[PASSED] Canary verification unit test completed successfully.\n";
    return 0;
}
