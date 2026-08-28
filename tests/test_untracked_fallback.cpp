#include "memsentry/memsentry.hpp"
#include "memsentry/core/allocator_hooks.hpp"
#include "memsentry/core/recursion_guard.hpp"
#include <iostream>
#include <vector>
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

void test_untracked_before_init() {
    // Before memsentry::init() is explicitly called, Manager is either uninitialized
    // or lazily initialized. Testing direct fallback path with recursion guard active.
    memsentry::core::RecursionGuard guard;
    void* ptr = memsentry::core::track_alloc(256);
    TEST_ASSERT(ptr != nullptr, "track_alloc with RecursionGuard must return valid memory");
    std::memset(ptr, 0xAA, 256);
    do_not_optimize(ptr);
    memsentry::core::track_free(ptr);
}

void test_recursion_guard_bypass() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = true;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    auto baseline_stats = memsentry::get_stats();

    // 1. Normal allocation must be tracked
    int* tracked_ptr = new int(42);
    do_not_optimize(tracked_ptr);
    auto after_tracked = memsentry::get_stats();
    TEST_ASSERT(after_tracked.active_allocation_count == baseline_stats.active_allocation_count + 1,
                "Normal allocation must increment active count");

    // 2. Allocation inside RecursionGuard must NOT be tracked
    void* untracked_raw = nullptr;
    int* untracked_obj = nullptr;
    {
        memsentry::core::RecursionGuard guard;
        TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "RecursionGuard must report active");

        untracked_raw = memsentry::core::track_alloc(512);
        TEST_ASSERT(untracked_raw != nullptr, "Untracked raw allocation must succeed");
        std::memset(untracked_raw, 0xBB, 512);
        do_not_optimize(untracked_raw);

        untracked_obj = new int(999);
        do_not_optimize(untracked_obj);

        auto during_guard = memsentry::get_stats();
        TEST_ASSERT(during_guard.active_allocation_count == after_tracked.active_allocation_count,
                    "Allocations inside RecursionGuard must not increase active allocation count");
    }

    // 3. After RecursionGuard exits, freeing untracked memory must succeed without undercounting
    {
        memsentry::core::RecursionGuard guard;
        memsentry::core::track_free(untracked_raw);
        delete untracked_obj;
    }

    auto after_untracked_freed = memsentry::get_stats();
    TEST_ASSERT(after_untracked_freed.active_allocation_count == after_tracked.active_allocation_count,
                "Freeing untracked memory must not affect tracked active count");

    // 4. Free tracked pointer normally
    delete tracked_ptr;
    auto final_stats = memsentry::get_stats();
    TEST_ASSERT(final_stats.active_allocation_count == baseline_stats.active_allocation_count,
                "Freeing tracked pointer must restore baseline active count");
}

void test_nested_recursion_guards() {
    TEST_ASSERT(!memsentry::core::RecursionGuard::is_active(), "RecursionGuard must not be active initially");

    {
        memsentry::core::RecursionGuard guard1;
        TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "RecursionGuard must be active at depth 1");
        {
            memsentry::core::RecursionGuard guard2;
            TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "RecursionGuard must be active at depth 2");
            {
                memsentry::core::RecursionGuard guard3;
                TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "RecursionGuard must be active at depth 3");

                void* p = memsentry::core::track_alloc(128);
                TEST_ASSERT(p != nullptr, "Allocation at depth 3 must succeed");
                memsentry::core::track_free(p);
            }
            TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "RecursionGuard must remain active at depth 2");
        }
        TEST_ASSERT(memsentry::core::RecursionGuard::is_active(), "RecursionGuard must remain active at depth 1");
    }

    TEST_ASSERT(!memsentry::core::RecursionGuard::is_active(), "RecursionGuard must become inactive after all guards exit");
}

int main() {
    std::cout << "==================================================\n";
    std::cout << "       MemSentry Untracked Fallback Tests         \n";
    std::cout << "==================================================\n" << std::flush;

    RUN_TEST(test_untracked_before_init);
    RUN_TEST(test_recursion_guard_bypass);
    RUN_TEST(test_nested_recursion_guards);

    std::cout << "==================================================\n" << std::flush;
    if (g_failed_tests == 0) {
        std::cout << "[SUCCESS] All untracked fallback tests passed!\n" << std::flush;
        return 0;
    } else {
        std::cerr << "[FAILURE] " << g_failed_tests << " test(s) failed.\n" << std::flush;
        return 1;
    }
}
