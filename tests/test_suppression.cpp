#include "memsentry/memsentry.hpp"

#include <cassert>
#include <iostream>
#include <string>
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

void* allocate_third_party_leak() {
    MEMSENTRY_SCOPE_TAG("ThirdParty_VendorSDK");
    int* p = new int[64];
    do_not_optimize(p);
    return p;
}

void* allocate_legitimate_leak() {
    MEMSENTRY_SCOPE_TAG("AppCore");
    int* p = new int[32];
    do_not_optimize(p);
    return p;
}

void test_tag_suppression() {
    memsentry::clear_suppressions();

    // 1. Initially no suppressions
    void* vendor_leak = allocate_third_party_leak();
    TEST_ASSERT(memsentry::has_leaks(), "Must detect leak");
    TEST_ASSERT(memsentry::has_unsuppressed_leaks(), "Must have unsuppressed leaks");
    TEST_ASSERT(memsentry::get_suppressed_count() == 0, "Suppressed count should be 0");

    // 2. Add suppression rule for the vendor tag
    memsentry::suppress("ThirdParty_VendorSDK");
    TEST_ASSERT(memsentry::get_suppressed_count() >= 1, "Vendor leak should now be suppressed");

    // 3. Add an unsuppressed application leak
    void* app_leak = allocate_legitimate_leak();
    TEST_ASSERT(memsentry::has_unsuppressed_leaks(), "Application leak must be flagged as unsuppressed");

    // 4. Suppress the application leak as well
    memsentry::suppress("AppCore");
    TEST_ASSERT(!memsentry::has_unsuppressed_leaks(), "Both leaks are now suppressed");

    // Clean up
    delete[] reinterpret_cast<int*>(vendor_leak);
    delete[] reinterpret_cast<int*>(app_leak);
    memsentry::clear_suppressions();
}

void test_symbol_or_file_suppression() {
    memsentry::clear_suppressions();

    void* ptr = new double[100];
    do_not_optimize(ptr);

    // Suppress by tag or test name
    memsentry::suppress("test_symbol_or_file_suppression");
    // Also test partial rule
    memsentry::suppress("General");

    TEST_ASSERT(memsentry::get_suppressed_count() >= 1, "Allocation should be suppressed");

    delete[] reinterpret_cast<double*>(ptr);
    memsentry::clear_suppressions();
}

int main() {
    memsentry::Config config;
    config.enable_stacktrace = true;
    config.enable_canary = true;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    std::cout << "==================================================\n";
    std::cout << "        MemSentry Leak Suppression Tests          \n";
    std::cout << "==================================================\n" << std::flush;

    RUN_TEST(test_tag_suppression);
    RUN_TEST(test_symbol_or_file_suppression);

    std::cout << "==================================================\n" << std::flush;
    if (g_failed_tests == 0) {
        std::cout << "[SUCCESS] All leak suppression tests passed!\n" << std::flush;
        return 0;
    } else {
        std::cerr << "[FAILURE] " << g_failed_tests << " test(s) failed.\n" << std::flush;
        return 1;
    }
}
