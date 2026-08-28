#include "memsentry/memsentry.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static int g_failed_tests = 0;

#define TEST_ASSERT(cond, msg)                                                                                         \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "[-] ASSERTION FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"               \
                      << std::flush;                                                                                   \
            g_failed_tests++;                                                                                          \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define RUN_TEST(fn)                                                                                                   \
    do {                                                                                                               \
        std::cout << "[RUN] " << #fn << "...\n" << std::flush;                                                         \
        int before = g_failed_tests;                                                                                   \
        fn();                                                                                                          \
        if (g_failed_tests == before) {                                                                                \
            std::cout << "  [PASS] " << #fn << "\n" << std::flush;                                                     \
        } else {                                                                                                       \
            std::cout << "  [FAIL] " << #fn << "\n" << std::flush;                                                     \
        }                                                                                                              \
    } while (0)

template <typename T> inline void do_not_optimize(T const& value) {
#if defined(_MSC_VER)
    auto volatile dummy = *reinterpret_cast<const volatile char*>(&value);
    (void)dummy;
#else
    asm volatile("" : : "r,m"(value) : "memory");
#endif
}

void test_free_block_histogram() {
    std::vector<void*> ptrs;

    // Allocate varied sizes
    ptrs.push_back(new char[8]);     // Bucket: [1 - 16 B]
    ptrs.push_back(new char[32]);    // Bucket: [17 - 64 B]
    ptrs.push_back(new char[128]);   // Bucket: [65 - 256 B]
    ptrs.push_back(new char[512]);   // Bucket: [257 B - 1 KB]
    ptrs.push_back(new char[2048]);  // Bucket: [1 KB - 4 KB]

    // Free 3 blocks
    delete[] reinterpret_cast<char*>(ptrs[0]);
    delete[] reinterpret_cast<char*>(ptrs[1]);
    delete[] reinterpret_cast<char*>(ptrs[2]);

    auto report = memsentry::get_fragmentation_report();
    TEST_ASSERT(report.total_freed_blocks >= 3, "At least 3 freed blocks counted");
    TEST_ASSERT(report.total_freed_bytes >= (8 + 32 + 128), "Freed bytes counted accurately");
    TEST_ASSERT(report.avg_freed_block_size > 0.0, "Average freed block size computed");

    // Verify free histogram buckets contain counts
    bool found_small_freed = false;
    for (const auto& b : report.freed_buckets) {
        if (b.count > 0) {
            found_small_freed = true;
        }
    }
    TEST_ASSERT(found_small_freed, "Freed histogram contains non-zero buckets");

    // Clean up remaining
    delete[] reinterpret_cast<char*>(ptrs[3]);
    delete[] reinterpret_cast<char*>(ptrs[4]);
}

void test_fragmentation_ratio_calculation() {
    // Allocate high watermark
    std::vector<void*> temp_ptrs;
    for (int i = 0; i < 20; ++i) {
        temp_ptrs.push_back(new char[1024 * 10]);  // 10 KB each
    }

    // Free 80% of allocations to induce fragmentation
    for (size_t i = 0; i < 16; ++i) {
        delete[] reinterpret_cast<char*>(temp_ptrs[i]);
    }

    auto report = memsentry::get_fragmentation_report();
    TEST_ASSERT(report.peak_allocated_bytes > report.current_allocated_bytes, "Peak exceeds current");
    TEST_ASSERT(report.external_fragmentation_ratio > 0.5, "Fragmentation ratio reflects released memory");

    // Clean up remaining
    for (size_t i = 16; i < temp_ptrs.size(); ++i) {
        delete[] reinterpret_cast<char*>(temp_ptrs[i]);
    }
}

int main() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = true;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    std::cout << "==================================================\n";
    std::cout << "     MemSentry Heap Fragmentation Tests           \n";
    std::cout << "==================================================\n" << std::flush;

    RUN_TEST(test_free_block_histogram);
    RUN_TEST(test_fragmentation_ratio_calculation);

    std::cout << "==================================================\n" << std::flush;
    if (g_failed_tests == 0) {
        std::cout << "[SUCCESS] All heap fragmentation tests passed!\n" << std::flush;
        return 0;
    } else {
        std::cerr << "[FAILURE] " << g_failed_tests << " test(s) failed.\n" << std::flush;
        return 1;
    }
}
