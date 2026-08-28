#include "memsentry/memsentry.hpp"
#include <iostream>
#include <vector>
#include <cassert>
#include <atomic>

static int g_failed_tests = 0;
static std::atomic<int> g_watchdog_trigger_count{0};
static std::atomic<uint64_t> g_last_current_bytes{0};
static std::atomic<uint64_t> g_last_limit_bytes{0};

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

void on_limit_breached(uint64_t current, uint64_t limit) {
    g_watchdog_trigger_count.fetch_add(1, std::memory_order_relaxed);
    g_last_current_bytes.store(current, std::memory_order_relaxed);
    g_last_limit_bytes.store(limit, std::memory_order_relaxed);

    // Verify recursion safety: allocating inside callback must not trigger deadlock or infinite recursion
    int* cb_alloc = new int(1234);
    do_not_optimize(cb_alloc);
    delete cb_alloc;
}

void test_watchdog_trigger() {
    g_watchdog_trigger_count = 0;
    g_last_current_bytes = 0;
    g_last_limit_bytes = 0;

    constexpr uint64_t LIMIT_BYTES = 10000; // 10 KB threshold

    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = false;
    config.auto_report_on_exit = false;
    config.max_heap_bytes = LIMIT_BYTES;
    config.on_limit_exceeded = on_limit_breached;
    memsentry::init(config);

    // 1. Below limit (5 KB allocation) -> no trigger
    char* buf1 = new char[5000];
    do_not_optimize(buf1);
    TEST_ASSERT(g_watchdog_trigger_count.load() == 0, "Watchdog must not fire below limit");

    // 2. Cross limit (6 KB additional allocation -> 11 KB total) -> triggers watchdog
    char* buf2 = new char[6000];
    do_not_optimize(buf2);
    TEST_ASSERT(g_watchdog_trigger_count.load() >= 1, "Watchdog must fire when threshold is exceeded");
    TEST_ASSERT(g_last_limit_bytes.load() == LIMIT_BYTES, "Limit bytes reported accurately");
    TEST_ASSERT(g_last_current_bytes.load() >= 11000, "Current bytes reported accurately");

    delete[] buf1;
    delete[] buf2;

    memsentry::shutdown();
}

int main() {
    std::cout << "==================================================\n";
    std::cout << "       MemSentry Memory Watchdog Unit Tests       \n";
    std::cout << "==================================================\n" << std::flush;

    RUN_TEST(test_watchdog_trigger);

    std::cout << "==================================================\n" << std::flush;
    if (g_failed_tests == 0) {
        std::cout << "[SUCCESS] All memory watchdog tests passed!\n" << std::flush;
        return 0;
    } else {
        std::cerr << "[FAILURE] " << g_failed_tests << " test(s) failed.\n" << std::flush;
        return 1;
    }
}
