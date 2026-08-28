#include "memsentry/memsentry.hpp"
#include "memsentry/core/allocator_hooks.hpp"
#include "memsentry/core/header.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <new>

static int g_failed_tests = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_failed_tests++; \
            return; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        std::cout << "[RUN] " << #fn << "...\n"; \
        int before = g_failed_tests; \
        fn(); \
        if (g_failed_tests == before) { \
            std::cout << "  [PASS] " << #fn << "\n"; \
        } else { \
            std::cout << "  [FAIL] " << #fn << "\n"; \
        } \
    } while (0)

template <typename T>
inline void do_not_optimize(T const& value) {
#if defined(_MSC_VER)
    *reinterpret_cast<const volatile char*>(&value);
#else
    asm volatile("" : : "r,m"(value) : "memory");
#endif
}

void test_basic_alloc_free() {
    auto initial_stats = memsentry::get_stats();
    int* p = new int(12345);
    do_not_optimize(p);
    TEST_ASSERT(p != nullptr, "p must not be null");
    TEST_ASSERT(*p == 12345, "p value must be preserved");

    auto alloc_stats = memsentry::get_stats();
    TEST_ASSERT(alloc_stats.active_allocation_count == initial_stats.active_allocation_count + 1, "active alloc count incremented");

    do_not_optimize(p);
    delete p;

    auto free_stats = memsentry::get_stats();
    TEST_ASSERT(free_stats.active_allocation_count == initial_stats.active_allocation_count, "active alloc count restored");
}

void test_array_alloc_free() {
    auto initial_stats = memsentry::get_stats();
    constexpr size_t count = 100;
    double* arr = new double[count];
    do_not_optimize(arr);
    for (size_t i = 0; i < count; ++i) arr[i] = static_cast<double>(i) * 1.5;

    TEST_ASSERT(arr[50] == 75.0, "array contents preserved");
    do_not_optimize(arr);
    delete[] arr;

    auto free_stats = memsentry::get_stats();
    TEST_ASSERT(free_stats.active_allocation_count == initial_stats.active_allocation_count, "array free tracked");
}

void test_nothrow_alloc() {
    int* p = new (std::nothrow) int(99);
    do_not_optimize(p);
    TEST_ASSERT(p != nullptr, "nothrow alloc succeeded");
    TEST_ASSERT(*p == 99, "nothrow value preserved");
    do_not_optimize(p);
    ::operator delete(p, std::nothrow);
}

struct alignas(64) AlignedBlock {
    uint64_t data[8];
};

void test_aligned_alloc() {
    auto* ptr = new (std::align_val_t{64}) AlignedBlock();
    do_not_optimize(ptr);
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    TEST_ASSERT((addr % 64) == 0, "pointer address must satisfy 64-byte alignment");

    do_not_optimize(ptr);
    ::operator delete(ptr, std::align_val_t{64});
}

void test_scoped_tagging() {
    {
        MEMSENTRY_SCOPE_TAG("TestSubsystem");
        TEST_ASSERT(std::string(memsentry::profiler::ScopedTag::current()) == "TestSubsystem", "tag must match scope");
        {
            MEMSENTRY_SCOPE_TAG("NestedSubsystem");
            TEST_ASSERT(std::string(memsentry::profiler::ScopedTag::current()) == "NestedSubsystem", "nested tag active");
        }
        TEST_ASSERT(std::string(memsentry::profiler::ScopedTag::current()) == "TestSubsystem", "tag restored after inner scope");
    }
    TEST_ASSERT(std::string(memsentry::profiler::ScopedTag::current()) == "General", "tag restored to General");
}

void test_snapshot_diffing() {
    auto snap1 = memsentry::take_snapshot("Snap1");

    int* persistent = new int(100);
    do_not_optimize(persistent);
    int* temporary = new int(200);
    do_not_optimize(temporary);

    delete temporary;

    auto snap2 = memsentry::take_snapshot("Snap2");
    auto diff = memsentry::profiler::compare_snapshots(snap1, snap2);

    TEST_ASSERT(diff.new_allocations.size() >= 1, "persistent block detected in diff");
    TEST_ASSERT(diff.net_allocations_delta >= 1, "net allocation delta correct");

    do_not_optimize(persistent);
    delete persistent;
}

void test_json_and_html_export() {
    int* leak_block = new int[32];
    do_not_optimize(leak_block);

    bool json_ok = memsentry::export_json("test_output.json");
    TEST_ASSERT(json_ok, "export_json must succeed");

    bool html_ok = memsentry::export_html("test_output.html");
    TEST_ASSERT(html_ok, "export_html must succeed");

    do_not_optimize(leak_block);
    delete[] leak_block;
}

int main() {
    memsentry::Config config;
    config.enable_stacktrace = true;
    config.enable_canary = true;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    std::cout << "==================================================\n";
    std::cout << "           MemSentry Test Suite Execution         \n";
    std::cout << "==================================================\n";

    RUN_TEST(test_basic_alloc_free);
    RUN_TEST(test_array_alloc_free);
    RUN_TEST(test_nothrow_alloc);
    RUN_TEST(test_aligned_alloc);
    RUN_TEST(test_scoped_tagging);
    RUN_TEST(test_snapshot_diffing);
    RUN_TEST(test_json_and_html_export);

    std::cout << "==================================================\n";
    if (g_failed_tests == 0) {
        std::cout << "[SUCCESS] All unit tests passed cleanly!\n";
        return 0;
    } else {
        std::cerr << "[FAILURE] " << g_failed_tests << " test(s) failed.\n";
        return 1;
    }
}
