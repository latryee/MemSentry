#include "memsentry/core/allocator_hooks.hpp"
#include "memsentry/core/header.hpp"
#include "memsentry/core/msvc_debug_guard.hpp"
#include "memsentry/memsentry.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
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

// 1. High-contention concurrent allocation, payload modification, and deallocation
MEMSENTRY_NO_SANITIZE void test_concurrent_canary_alloc_free_stress() {
    constexpr int NUM_THREADS = 8;
    constexpr int ITERATIONS = 1000;

    std::vector<std::thread> workers;
    workers.reserve(NUM_THREADS);

    auto base_stats = memsentry::get_stats();

    for (int t = 0; t < NUM_THREADS; ++t) {
        memsentry::core::RecursionGuard guard;
        workers.emplace_back([t]() {
            MEMSENTRY_SCOPE_TAG("CanaryStressWorker");
            for (int i = 0; i < ITERATIONS; ++i) {
                size_t sz = ((i + t) % 64 + 1) * 16;
                uint8_t* buf = new uint8_t[sz];
                TEST_ASSERT(buf != nullptr, "Buffer allocation failed");

                // Fill entire user buffer with byte patterns up to exact boundary
                std::memset(buf, static_cast<uint8_t>(t ^ i), sz);
                do_not_optimize(buf);

                // Verify user contents remain intact
                for (size_t b = 0; b < sz; ++b) {
                    TEST_ASSERT(buf[b] == static_cast<uint8_t>(t ^ i), "Payload integrity check failed");
                }

                // Deallocate (triggers internal canary verification)
                delete[] buf;
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    auto final_stats = memsentry::get_stats();
    TEST_ASSERT(final_stats.total_allocation_count >= base_stats.total_allocation_count + (NUM_THREADS * ITERATIONS),
                "All worker allocations must be registered");
    TEST_ASSERT(final_stats.total_free_count >= base_stats.total_free_count + (NUM_THREADS * ITERATIONS),
                "All worker allocations must be safely freed without false corruptions");

    // Verify no worker leaks remain
    auto active = memsentry::get_active_allocations();
    size_t worker_active_count = 0;
    for (const auto& rec : active) {
        if (rec.tag && std::strcmp(rec.tag, "CanaryStressWorker") == 0) {
            worker_active_count++;
        }
    }
    TEST_ASSERT(worker_active_count == 0, "Zero CanaryStressWorker blocks leaked");
}

// 2. Shared block concurrent multi-threaded read/write with boundary preservation
MEMSENTRY_NO_SANITIZE void test_shared_block_concurrent_access() {
    constexpr int NUM_READERS = 4;
    constexpr int NUM_WRITERS = 4;
    constexpr size_t BLOCK_SIZE = 1024 * 16;

    uint8_t* shared_buf = new uint8_t[BLOCK_SIZE];
    std::memset(shared_buf, 0xAA, BLOCK_SIZE);

    std::atomic<bool> running{true};
    std::atomic<uint64_t> read_ops{0};
    std::atomic<uint64_t> write_ops{0};

    std::vector<std::thread> threads;

    // Writer threads writing to distinct segments
    for (int w = 0; w < NUM_WRITERS; ++w) {
        memsentry::core::RecursionGuard guard;
        threads.emplace_back([&running, &write_ops, shared_buf, w]() {
            size_t seg_size = BLOCK_SIZE / NUM_WRITERS;
            size_t start = w * seg_size;
            while (running.load(std::memory_order_relaxed)) {
                for (size_t i = 0; i < seg_size; ++i) {
                    shared_buf[start + i] = static_cast<uint8_t>((start + i) & 0xFF);
                }
                write_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Reader threads
    for (int r = 0; r < NUM_READERS; ++r) {
        memsentry::core::RecursionGuard guard;
        threads.emplace_back([&running, &read_ops, shared_buf]() {
            uint64_t checksum = 0;
            while (running.load(std::memory_order_relaxed)) {
                for (size_t i = 0; i < BLOCK_SIZE; i += 64) {
                    checksum += shared_buf[i];
                }
                do_not_optimize(checksum);
                read_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Run for 150 ms under high contention
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    running.store(false, std::memory_order_relaxed);

    for (auto& th : threads) {
        th.join();
    }

    // Freeing shared block must successfully verify canary
    delete[] shared_buf;

    TEST_ASSERT(read_ops.load() > 0, "Reader ops occurred");
    TEST_ASSERT(write_ops.load() > 0, "Writer ops occurred");
}

int main() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = true;
    config.canary_footer_size = 16;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    std::cout << "==================================================\n";
    std::cout << "  MemSentry Multi-Threaded Canary Race Test (TSan)\n";
    std::cout << "==================================================\n" << std::flush;

    RUN_TEST(test_concurrent_canary_alloc_free_stress);
    RUN_TEST(test_shared_block_concurrent_access);

    std::cout << "==================================================\n" << std::flush;
    if (g_failed_tests == 0) {
        std::cout << "[SUCCESS] Canary race condition stress test passed with ZERO false positives!\n" << std::flush;
        return 0;
    } else {
        std::cerr << "[FAILURE] " << g_failed_tests << " test(s) failed.\n" << std::flush;
        return 1;
    }
}
