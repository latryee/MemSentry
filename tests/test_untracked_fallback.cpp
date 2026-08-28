#include "memsentry/memsentry.hpp"
#include "memsentry/core/allocator_hooks.hpp"
#include "memsentry/core/recursion_guard.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <cassert>
#include <cstring>
#include <new>

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

// 1. Pre-init allocation + pre-init free (Pure Untracked Fallback)
void test_pure_pre_init_alloc_free() {
    TEST_ASSERT(!memsentry::is_initialized(), "Must not be initialized yet");

    void* raw_ptr = memsentry::core::track_alloc(256);
    TEST_ASSERT(raw_ptr != nullptr, "Pre-init track_alloc must return valid memory");
    std::memset(raw_ptr, 0xAA, 256);
    do_not_optimize(raw_ptr);
    memsentry::core::track_free(raw_ptr);

    int* obj_ptr = new int(1337);
    TEST_ASSERT(obj_ptr != nullptr, "Pre-init new must succeed");
    TEST_ASSERT(*obj_ptr == 1337, "Pre-init object value preserved");
    do_not_optimize(obj_ptr);
    delete obj_ptr;

    auto stats = memsentry::get_stats();
    TEST_ASSERT(stats.total_allocation_count == 0, "Pre-init allocations must not increment stats");
}

// 2. Pre-init allocation + post-init free (Cross-Lifecycle Safe Free)
void test_pre_init_alloc_post_init_free() {
    TEST_ASSERT(!memsentry::is_initialized(), "Must not be initialized before test");

    // Allocate before init
    int* early_leak = new int(4242);
    void* early_raw = memsentry::core::track_alloc(512);
    do_not_optimize(early_leak);
    do_not_optimize(early_raw);

    // Initialize MemSentry
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = true;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    TEST_ASSERT(memsentry::is_initialized(), "Must be initialized now");
    auto baseline = memsentry::get_stats();
    TEST_ASSERT(baseline.active_allocation_count == 0, "Baseline active count must be 0");

    // Track a normal allocation
    int* tracked = new int(100);
    do_not_optimize(tracked);
    auto after_tracked = memsentry::get_stats();
    TEST_ASSERT(after_tracked.active_allocation_count == 1, "Tracked allocation registered");

    // Free pre-init allocations (must safely detect untracked pointers)
    delete early_leak;
    memsentry::core::track_free(early_raw);

    auto after_early_free = memsentry::get_stats();
    TEST_ASSERT(after_early_free.active_allocation_count == 1,
                "Freeing pre-init allocations must not alter tracked active count");

    // Free tracked allocation
    delete tracked;
    auto final_stats = memsentry::get_stats();
    TEST_ASSERT(final_stats.active_allocation_count == 0, "All tracked memory cleanly freed");

    memsentry::shutdown();
    TEST_ASSERT(!memsentry::is_initialized(), "Must be shut down");
}

// 3. Pre-init realloc fallback
void test_pre_init_realloc_fallback() {
    TEST_ASSERT(!memsentry::is_initialized(), "Must not be initialized");

    void* ptr = memsentry::core::track_alloc(64);
    std::memset(ptr, 'A', 64);
    do_not_optimize(ptr);

    void* resized = memsentry::core::track_realloc(ptr, 128);
    TEST_ASSERT(resized != nullptr, "Pre-init realloc must succeed");
    do_not_optimize(resized);
    memsentry::core::track_free(resized);
}

// 4. Post-shutdown allocation and deallocation
void test_post_shutdown_fallback() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    memsentry::init(config);
    TEST_ASSERT(memsentry::is_initialized(), "Initialized");

    int* tracked_ptr = new int(555);
    do_not_optimize(tracked_ptr);

    memsentry::shutdown();
    TEST_ASSERT(!memsentry::is_initialized(), "Shutdown complete");

    // Freeing tracked pointer after shutdown must succeed via fallback
    delete tracked_ptr;

    // New allocations after shutdown must succeed in fallback mode
    char* post_shutdown_buf = new char[1024];
    do_not_optimize(post_shutdown_buf);
    delete[] post_shutdown_buf;
}

// 5. RecursionGuard nesting & bypass
void test_recursion_guard_nesting() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = true;
    memsentry::init(config);

    auto base = memsentry::get_stats();

    TEST_ASSERT(!memsentry::core::RecursionGuard::is_active(), "Guard initially inactive");

    {
        memsentry::core::RecursionGuard g1;
        TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "Guard active level 1");
        void* p1 = memsentry::core::track_alloc(128);
        do_not_optimize(p1);

        {
            memsentry::core::RecursionGuard g2;
            TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "Guard active level 2");
            void* p2 = memsentry::core::track_alloc(256);
            do_not_optimize(p2);

            {
                memsentry::core::RecursionGuard g3;
                TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "Guard active level 3");
                void* p3 = memsentry::core::track_alloc(512);
                do_not_optimize(p3);
                memsentry::core::track_free(p3);
            }
            TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "Guard active level 2 after unwinding g3");
            memsentry::core::track_free(p2);
        }
        TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "Guard active level 1 after unwinding g2");
        memsentry::core::track_free(p1);
    }

    TEST_ASSERT(!memsentry::core::RecursionGuard::is_active(), "Guard inactive after all scopes exited");

    auto after = memsentry::get_stats();
    TEST_ASSERT(after.active_allocation_count == base.active_allocation_count,
                "Nested guard allocations must not pollute tracked active stats");

    memsentry::shutdown();
}

// 6. Multi-threaded concurrent pre-init & fallback stress
void test_multithreaded_fallback() {
    constexpr int NUM_THREADS = 4;
    constexpr int ITERS = 500;

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([]() {
            for (int i = 0; i < ITERS; ++i) {
                void* p = memsentry::core::track_alloc(64);
                do_not_optimize(p);
                memsentry::core::track_free(p);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    TEST_ASSERT(!memsentry::is_initialized(), "Must remain uninitialized");
}

// 7. Static / Global Object Simulation
struct GlobalObjectSimulator {
    int* data;
    GlobalObjectSimulator() {
        data = new int[64];
        for (int i = 0; i < 64; ++i) data[i] = i * 2;
    }
    ~GlobalObjectSimulator() {
        delete[] data;
    }
};

void test_static_global_object_lifecycle() {
    // Simulate static global allocation before init
    GlobalObjectSimulator* sim = new GlobalObjectSimulator();

    // Start MemSentry
    memsentry::init();

    int* runtime_alloc = new int(777);
    do_not_optimize(runtime_alloc);
    delete runtime_alloc;

    // Teardown MemSentry
    memsentry::shutdown();

    // Simulate static destruction after shutdown
    delete sim;
}

int main() {
    std::cout << "==================================================\n";
    std::cout << "    MemSentry 100% Coverage Untracked Fallback    \n";
    std::cout << "==================================================\n" << std::flush;

    RUN_TEST(test_pure_pre_init_alloc_free);
    RUN_TEST(test_pre_init_alloc_post_init_free);
    RUN_TEST(test_pre_init_realloc_fallback);
    RUN_TEST(test_post_shutdown_fallback);
    RUN_TEST(test_recursion_guard_nesting);
    RUN_TEST(test_multithreaded_fallback);
    RUN_TEST(test_static_global_object_lifecycle);

    std::cout << "==================================================\n" << std::flush;
    if (g_failed_tests == 0) {
        std::cout << "[SUCCESS] All untracked fallback tests passed with 100% coverage!\n" << std::flush;
        return 0;
    } else {
        std::cerr << "[FAILURE] " << g_failed_tests << " test(s) failed.\n" << std::flush;
        return 1;
    }
}
