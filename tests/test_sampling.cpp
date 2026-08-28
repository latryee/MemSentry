#include "memsentry/memsentry.hpp"

#include <cassert>
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

void test_sampling_every_n() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = true;
    config.sample_every_n = 4;  // Track 1 out of 4 allocations (25% sampling)
    config.auto_report_on_exit = false;
    memsentry::init(config);

    auto base = memsentry::get_stats();

    constexpr int TOTAL_ALLOCS = 100;
    std::vector<int*> ptrs;
    ptrs.reserve(TOTAL_ALLOCS);

    for (int i = 0; i < TOTAL_ALLOCS; ++i) {
        int* p = new int(i);
        do_not_optimize(p);
        ptrs.push_back(p);
    }

    auto active = memsentry::get_active_allocations();
    // With 1-in-4 sampling, active tracked allocations should be approximately 25 (+/- 2 due to counter phase)
    TEST_ASSERT(active.size() >= 23 && active.size() <= 27, "Sampled active allocations count must be ~25% of total");

    for (int* p : ptrs) {
        delete p;
    }

    auto final_stats = memsentry::get_stats();
    TEST_ASSERT(final_stats.active_allocation_count == base.active_allocation_count,
                "All sampled and unsampled allocations cleanly deallocated");

    memsentry::shutdown();
}

void test_sampling_percentage() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.sampling_percentage = 10;  // 10% sampling
    config.sample_every_n = 1;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    constexpr int TOTAL_ALLOCS = 200;
    std::vector<int*> ptrs;
    ptrs.reserve(TOTAL_ALLOCS);

    for (int i = 0; i < TOTAL_ALLOCS; ++i) {
        int* p = new int(i);
        do_not_optimize(p);
        ptrs.push_back(p);
    }

    auto active = memsentry::get_active_allocations();
    // With 10% sampling, ~20 allocations should be tracked
    TEST_ASSERT(active.size() >= 18 && active.size() <= 22, "10% sampled allocations count must be ~20");

    for (int* p : ptrs) {
        delete p;
    }

    memsentry::shutdown();
}

void test_poisson_geometric_sampling() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.sampling_rate_bytes = 4096;  // 4 KB mean sample interval
    config.auto_report_on_exit = false;
    memsentry::init(config);

    constexpr int NUM_CHUNKS = 500;
    constexpr size_t CHUNK_SIZE = 128;  // Total allocated = 64 KB
    std::vector<uint8_t*> ptrs;
    ptrs.reserve(NUM_CHUNKS);

    for (int i = 0; i < NUM_CHUNKS; ++i) {
        uint8_t* p = new uint8_t[CHUNK_SIZE];
        do_not_optimize(p);
        ptrs.push_back(p);
    }

    auto active = memsentry::get_active_allocations();
    // 64 KB total / 4 KB interval = expected ~16 sampled allocations (statistically bounded between 4 and 40)
    TEST_ASSERT(!active.empty(), "Poisson sampling must capture sample allocations");
    TEST_ASSERT(active.size() < NUM_CHUNKS, "Poisson sampling must maintain overhead < 100%");

    for (uint8_t* p : ptrs) {
        delete[] p;
    }

    memsentry::shutdown();
}

int main() {
    std::cout << "==================================================\n";
    std::cout << "       MemSentry Sampling Mode Unit Tests         \n";
    std::cout << "==================================================\n" << std::flush;

    RUN_TEST(test_sampling_every_n);
    RUN_TEST(test_sampling_percentage);
    RUN_TEST(test_poisson_geometric_sampling);

    std::cout << "==================================================\n" << std::flush;
    if (g_failed_tests == 0) {
        std::cout << "[SUCCESS] All sampling mode tests passed!\n" << std::flush;
        return 0;
    } else {
        std::cerr << "[FAILURE] " << g_failed_tests << " test(s) failed.\n" << std::flush;
        return 1;
    }
}
