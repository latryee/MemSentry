#include "memsentry/memsentry.hpp"
#include "memsentry/core/header.hpp"
#include "memsentry/core/suppression_engine.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 32) return 0;

    // 1. Fuzz BlockHeader and Canary Verification
    {
        alignas(16) uint8_t buffer[512] = {0};
        size_t copy_len = (size < sizeof(buffer)) ? size : sizeof(buffer);
        std::memcpy(buffer, data, copy_len);

        auto* header = reinterpret_cast<memsentry::core::BlockHeader*>(buffer);
        // Execute verify_canary with fuzzed input
        (void)memsentry::core::verify_canary(header, 16);
    }

    // 2. Fuzz init_block with randomized parameters
    {
        uint16_t req_size = *reinterpret_cast<const uint16_t*>(data) % 256;
        uint8_t align_byte = data[2] % 64;
        size_t alignment = (align_byte == 0) ? 16 : static_cast<size_t>(align_byte);
        size_t footer_size = (data[3] % 32) + 8;

        alignas(64) uint8_t memory[1024] = {0};
        void* user_ptr = memsentry::core::init_block(
            memory,
            req_size,
            alignment,
            footer_size,
            1,
            "FuzzTag",
            true
        );

        if (user_ptr) {
            auto* header = memsentry::core::get_header_from_raw_ptr(memory);
            (void)memsentry::core::verify_canary(header, footer_size);
        }
    }

    // 3. Fuzz SuppressionEngine pattern matching
    {
        std::string pattern(reinterpret_cast<const char*>(data + 4), (size - 4) % 32);
        memsentry::suppress(pattern);

        memsentry::AllocationRecord rec;
        rec.allocation_id = 999;
        rec.tag = "FuzzScope";
        rec.requested_size = 128;
        (void)memsentry::core::SuppressionEngine::instance().is_suppressed(rec);

        memsentry::clear_suppressions();
    }

    // 4. Fuzz Exporters with synthesized AllocationRecord
    {
        memsentry::MemoryStatsSnapshot stats;
        stats.total_allocated_bytes = size;
        stats.peak_allocated_bytes = size * 2;
        stats.total_allocation_count = 1;

        std::vector<memsentry::AllocationRecord> recs;
        memsentry::AllocationRecord r;
        r.allocation_id = 1;
        r.requested_size = size;
        r.total_size = size + 32;
        r.tag = "FuzzTag";
        r.thread_id = 42;
        recs.push_back(r);

        (void)memsentry::reporter::JsonReporter::serialize(stats, recs);
        (void)memsentry::reporter::HtmlReporter::generate(stats, recs);
    }

    return 0;
}
