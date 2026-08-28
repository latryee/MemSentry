#include "memsentry/memsentry.hpp"
#include "memsentry/core/allocator_hooks.hpp"
#include <iostream>
#include <vector>
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

void test_c_malloc_free() {
    auto base_stats = memsentry::get_stats();

    void* p = memsentry_malloc(128);
    TEST_ASSERT(p != nullptr, "memsentry_malloc must return non-null pointer");
    std::memset(p, 0x5A, 128);
    do_not_optimize(p);

    auto after_alloc = memsentry::get_stats();
    TEST_ASSERT(after_alloc.active_allocation_count == base_stats.active_allocation_count + 1,
                "memsentry_malloc must increment active allocations");

    memsentry_free(p);

    auto after_free = memsentry::get_stats();
    TEST_ASSERT(after_free.active_allocation_count == base_stats.active_allocation_count,
                "memsentry_free must restore active allocations");
}

void test_c_calloc_zero_init() {
    auto base_stats = memsentry::get_stats();

    constexpr size_t NUM = 16;
    constexpr size_t ELEM_SIZE = sizeof(uint64_t);
    uint64_t* arr = reinterpret_cast<uint64_t*>(memsentry_calloc(NUM, ELEM_SIZE));
    TEST_ASSERT(arr != nullptr, "memsentry_calloc must return valid pointer");

    // Verify zero-initialization
    for (size_t i = 0; i < NUM; ++i) {
        TEST_ASSERT(arr[i] == 0, "calloc memory must be zero-initialized");
    }

    auto stats = memsentry::get_stats();
    TEST_ASSERT(stats.active_allocation_count == base_stats.active_allocation_count + 1,
                "calloc must be tracked in statistics");

    memsentry_free(arr);
}

void test_c_realloc() {
    void* p1 = memsentry_malloc(64);
    TEST_ASSERT(p1 != nullptr, "malloc(64) succeeded");
    std::memset(p1, 0x42, 64);
    do_not_optimize(p1);

    // Expand
    void* p2 = memsentry_realloc(p1, 256);
    TEST_ASSERT(p2 != nullptr, "realloc(256) succeeded");
    uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(p2);
    for (size_t i = 0; i < 64; ++i) {
        TEST_ASSERT(byte_ptr[i] == 0x42, "realloc preserved original contents");
    }

    // Shrink
    void* p3 = memsentry_realloc(p2, 32);
    TEST_ASSERT(p3 != nullptr, "realloc(32) succeeded");
    byte_ptr = reinterpret_cast<uint8_t*>(p3);
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT(byte_ptr[i] == 0x42, "realloc shrink preserved contents");
    }

    memsentry_free(p3);
}

void test_c_aligned_alloc() {
    constexpr size_t ALIGN = 64;
    constexpr size_t SIZE = 256;

    void* ptr = memsentry_aligned_alloc(ALIGN, SIZE);
    TEST_ASSERT(ptr != nullptr, "aligned_alloc succeeded");
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    TEST_ASSERT((addr % ALIGN) == 0, "aligned_alloc satisfied 64-byte alignment");

    std::memset(ptr, 0xCC, SIZE);
    do_not_optimize(ptr);

    memsentry_free(ptr);
}

void test_c_posix_memalign() {
    void* ptr = nullptr;
    int res = memsentry_posix_memalign(&ptr, 64, 128);
    TEST_ASSERT(res == 0, "posix_memalign returned 0");
    TEST_ASSERT(ptr != nullptr, "posix_memalign returned non-null ptr");
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    TEST_ASSERT((addr % 64) == 0, "posix_memalign satisfied alignment");

    std::memset(ptr, 0xEE, 128);
    do_not_optimize(ptr);

    memsentry_free(ptr);
}

#if defined(_WIN32)
void test_c_win_aligned_malloc() {
    void* ptr = memsentry_aligned_malloc(128, 128);
    TEST_ASSERT(ptr != nullptr, "memsentry_aligned_malloc succeeded");
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    TEST_ASSERT((addr % 128) == 0, "_aligned_malloc satisfied 128-byte alignment");

    std::memset(ptr, 0x77, 128);
    do_not_optimize(ptr);

    memsentry_aligned_free(ptr);
}
#endif

int main() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = true;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    std::cout << "==================================================\n";
    std::cout << "        MemSentry C Allocation Hooks Tests        \n";
    std::cout << "==================================================\n" << std::flush;

    RUN_TEST(test_c_malloc_free);
    RUN_TEST(test_c_calloc_zero_init);
    RUN_TEST(test_c_realloc);
    RUN_TEST(test_c_aligned_alloc);
    RUN_TEST(test_c_posix_memalign);
#if defined(_WIN32)
    RUN_TEST(test_c_win_aligned_malloc);
#endif

    std::cout << "==================================================\n" << std::flush;
    if (g_failed_tests == 0) {
        std::cout << "[SUCCESS] All C allocation hook tests passed cleanly!\n" << std::flush;
        return 0;
    } else {
        std::cerr << "[FAILURE] " << g_failed_tests << " test(s) failed.\n" << std::flush;
        return 1;
    }
}
