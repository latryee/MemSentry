#include "memsentry/memsentry.hpp"
#include "memsentry/core/allocator_hooks.hpp"
#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <cstdint>

static int g_failed_tests = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n" << std::flush; \
            g_failed_tests++; \
            return; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        std::cout << "[RUN] " << #fn << "...\n" << std::flush; \
        int before = g_failed_tests; \
        fn(); \
        if (g_failed_tests == before) { \
            std::cout << "  [PASS] " << #fn << "\n" << std::flush; \
        } else { \
            std::cout << "  [FAIL] " << #fn << "\n" << std::flush; \
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

void test_realloc_growth_and_shrink() {
    auto base_stats = memsentry::get_stats();

    // 1. Initial alloc (64 bytes)
    void* p1 = memsentry_malloc(64);
    TEST_ASSERT(p1 != nullptr, "p1 allocated");
    std::memset(p1, 0xAB, 64);
    do_not_optimize(p1);

    auto stats_64 = memsentry::get_stats();
    TEST_ASSERT(stats_64.current_allocated_bytes == base_stats.current_allocated_bytes + 64,
                "Current bytes must increase by 64");

    // 2. Growth to 256 bytes
    void* p2 = memsentry_realloc(p1, 256);
    TEST_ASSERT(p2 != nullptr, "p2 reallocated (grow)");
    uint8_t* bytes_p2 = reinterpret_cast<uint8_t*>(p2);
    for (size_t i = 0; i < 64; ++i) {
        TEST_ASSERT(bytes_p2[i] == 0xAB, "Original bytes preserved after growth");
    }

    auto stats_256 = memsentry::get_stats();
    TEST_ASSERT(stats_256.current_allocated_bytes == base_stats.current_allocated_bytes + 256,
                "Current bytes must update to 256");

    // 3. Shrink to 32 bytes
    void* p3 = memsentry_realloc(p2, 32);
    TEST_ASSERT(p3 != nullptr, "p3 reallocated (shrink)");
    uint8_t* bytes_p3 = reinterpret_cast<uint8_t*>(p3);
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT(bytes_p3[i] == 0xAB, "Prefix preserved after shrinking");
    }

    auto stats_32 = memsentry::get_stats();
    TEST_ASSERT(stats_32.current_allocated_bytes == base_stats.current_allocated_bytes + 32,
                "Current bytes must update to 32");

    memsentry_free(p3);
    auto final_stats = memsentry::get_stats();
    TEST_ASSERT(final_stats.current_allocated_bytes == base_stats.current_allocated_bytes,
                "All memory cleanly freed after realloc sequence");
}

void test_realloc_edge_cases() {
    auto base_stats = memsentry::get_stats();

    // Realloc(nullptr, 128) -> malloc(128)
    void* p = memsentry_realloc(nullptr, 128);
    TEST_ASSERT(p != nullptr, "realloc(nullptr, size) must allocate memory");

    auto after_alloc = memsentry::get_stats();
    TEST_ASSERT(after_alloc.active_allocation_count == base_stats.active_allocation_count + 1, "Alloc count +1");

    // Realloc(p, 0) -> free(p)
    void* res = memsentry_realloc(p, 0);
    TEST_ASSERT(res == nullptr, "realloc(p, 0) must return nullptr and free");

    auto after_free = memsentry::get_stats();
    TEST_ASSERT(after_free.active_allocation_count == base_stats.active_allocation_count, "Alloc count restored");
}

void test_realloc_canary_check() {
    void* p = memsentry_malloc(64);
    TEST_ASSERT(p != nullptr, "Allocated 64 bytes");

    // Corrupt canary byte right after the 64-byte payload
    uint8_t* canary = reinterpret_cast<uint8_t*>(p) + 64;
    *canary ^= 0xFF; // Corrupt

    // Reallocating corrupted block should trigger canary detection
    void* p2 = memsentry_realloc(p, 128);
    TEST_ASSERT(p2 != nullptr, "Realloc still succeeds to avoid total process crash");

    memsentry_free(p2);
}

int main() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = true;
    config.canary_footer_size = 16;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    std::cout << "==================================================\n";
    std::cout << "          MemSentry Realloc Unit Tests            \n";
    std::cout << "==================================================\n" << std::flush;

    RUN_TEST(test_realloc_growth_and_shrink);
    RUN_TEST(test_realloc_edge_cases);
    RUN_TEST(test_realloc_canary_check);

    std::cout << "==================================================\n" << std::flush;
    if (g_failed_tests == 0) {
        std::cout << "[SUCCESS] All realloc unit tests passed!\n" << std::flush;
        return 0;
    } else {
        std::cerr << "[FAILURE] " << g_failed_tests << " test(s) failed.\n" << std::flush;
        return 1;
    }
}
