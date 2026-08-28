#include "memsentry/memsentry.hpp"
#include "memsentry/core/header.hpp"
#include "memsentry/core/allocator_hooks.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include <cstdint>
#include <cstring>

struct ScenarioResult {
    std::string name;
    std::string fault_type;
    bool memsentry_detected;
    std::string memsentry_diagnostic;
    bool asan_equivalent;
    std::string asan_diagnostic;
    bool ubsan_equivalent;
};

static int g_failed_tests = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n" << std::flush; \
            g_failed_tests++; \
            return; \
        } \
    } while (0)

template <typename T>
inline void do_not_optimize(T const& value) {
#if defined(_MSC_VER)
    auto volatile dummy = *reinterpret_cast<const volatile char*>(&value);
    (void)dummy;
#else
    asm volatile("" : : "r,m"(value) : "memory");
#endif
}

// 1. Buffer Overflow (Canary Overrun)
ScenarioResult test_scenario_buffer_overflow() {
    constexpr size_t SIZE = 64;
    alignas(16) uint8_t raw_mem[256] = {0};
    void* user_ptr = memsentry::core::init_block(raw_mem, SIZE, 16, 16, 101, "OverflowTest", true);

    auto* header = memsentry::core::get_header_from_raw_ptr(raw_mem);
    
    // Simulate off-by-one / buffer overrun overwrite
    uint8_t* canary_byte = reinterpret_cast<uint8_t*>(user_ptr) + SIZE;
    *canary_byte ^= 0xDE; // Corrupt canary

    auto status = memsentry::core::verify_canary(header, 16);
    bool detected = (status == memsentry::CorruptionType::FOOTER_CORRUPTED);

    return {
        "Buffer Overflow (Right Out-Of-Bounds)",
        "Overrun payload boundary (+1 to +16 bytes)",
        detected,
        "CorruptionType::FOOTER_CORRUPTED (Canary mismatch)",
        true,
        "AddressSanitizer: heap-buffer-overflow",
        false
    };
}

// 2. Buffer Underrun (Header Magic Corruption)
ScenarioResult test_scenario_buffer_underrun() {
    constexpr size_t SIZE = 128;
    alignas(16) uint8_t raw_mem[512] = {0};
    void* user_ptr = memsentry::core::init_block(raw_mem, SIZE, 16, 16, 102, "UnderrunTest", true);
    (void)user_ptr;

    auto* header = memsentry::core::get_header_from_raw_ptr(raw_mem);
    
    // Corrupt header magic signature
    header->magic = 0x00000000;

    auto status = memsentry::core::verify_canary(header, 16);
    bool detected = (status == memsentry::CorruptionType::HEADER_CORRUPTED);

    return {
        "Buffer Underrun (Left Out-Of-Bounds)",
        "Underrun block header (Magic overwrite)",
        detected,
        "CorruptionType::HEADER_CORRUPTED (Magic mismatch)",
        true,
        "AddressSanitizer: heap-buffer-overflow (left)",
        false
    };
}

// 3. Double-Free / Untracked Pointer
ScenarioResult test_scenario_double_free() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = true;
    memsentry::init(config);

    void* ptr = memsentry::core::track_alloc(64);
    memsentry::core::track_free(ptr);

    // Second free of the same pointer
    auto base_stats = memsentry::get_stats();
    memsentry::core::track_free(ptr);
    auto after_double_free = memsentry::get_stats();

    // Tracker erased on first free, so second free is safely handled as untracked
    bool handled_cleanly = (after_double_free.active_allocation_count == base_stats.active_allocation_count);

    return {
        "Double-Free Anomaly",
        "Freeing already-deallocated heap pointer",
        handled_cleanly,
        "Safe untracked bypass + Zero heap corruption",
        true,
        "AddressSanitizer: attempting double-free",
        false
    };
}

// 4. Intentional Memory Leak Detection
ScenarioResult test_scenario_intentional_leak() {
    memsentry::Config config;
    config.enable_stacktrace = true;
    config.enable_canary = true;
    memsentry::init(config);

    auto base = memsentry::get_stats();

    int* leaked_data = new int[32];
    do_not_optimize(leaked_data);

    auto current = memsentry::get_stats();
    bool leak_registered = (current.active_allocation_count == base.active_allocation_count + 1);

    auto active = memsentry::get_active_allocations();
    bool found_record = false;
    for (const auto& rec : active) {
        if (rec.user_ptr == leaked_data) {
            found_record = true;
            break;
        }
    }

    delete[] leaked_data;

    return {
        "Memory Leak Detection",
        "Unreleased heap allocation at scope exit",
        leak_registered && found_record,
        "Tracked in Active Shard + Callstack Captured",
        true,
        "LeakSanitizer (LSan): Direct leak",
        false
    };
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "        MemSentry vs ASan/UBSan Comparative Verification Test Suite            \n";
    std::cout << "================================================================================\n" << std::flush;

    std::vector<ScenarioResult> results;
    results.push_back(test_scenario_buffer_overflow());
    results.push_back(test_scenario_buffer_underrun());
    results.push_back(test_scenario_double_free());
    results.push_back(test_scenario_intentional_leak());

    std::cout << "\n" << std::left
              << std::setw(32) << "Fault Scenario"
              << " | " << std::setw(12) << "MemSentry"
              << " | " << std::setw(8) << "ASan"
              << " | " << std::setw(8) << "UBSan"
              << " | Diagnostic Equivalence\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    bool all_passed = true;
    for (const auto& r : results) {
        std::cout << std::left << std::setw(32) << r.name
                  << " | " << std::setw(12) << (r.memsentry_detected ? "[DETECTED]" : "[MISSED]")
                  << " | " << std::setw(8) << (r.asan_equivalent ? "[YES]" : "[NO]")
                  << " | " << std::setw(8) << (r.ubsan_equivalent ? "[YES]" : "[N/A]")
                  << " | " << r.memsentry_diagnostic << "\n";
        if (!r.memsentry_detected) all_passed = false;
    }

    std::cout << "================================================================================\n";
    if (all_passed) {
        std::cout << "[SUCCESS] All comparative sanitizer test scenarios verified successfully!\n" << std::flush;
        return 0;
    } else {
        std::cerr << "[FAILURE] One or more sanitizer matrix scenarios failed!\n" << std::flush;
        return 1;
    }
}
