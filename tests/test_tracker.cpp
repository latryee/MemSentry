#include "memsentry/core/allocator_hooks.hpp"
#include "memsentry/core/recursion_guard.hpp"
#include "memsentry/memsentry.hpp"

#include <cstddef>
#include <iostream>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <thread>
#endif

template <typename T> inline void do_not_optimize(T const& value) {
#if defined(_MSC_VER)
    auto volatile dummy = *reinterpret_cast<const volatile char*>(&value);
    (void)dummy;
#else
    asm volatile("" : : "r,m"(value) : "memory");
#endif
}

void thread_stress_worker(int thread_id, int iterations) {
    (void)thread_id;
    MEMSENTRY_SCOPE_TAG("StressWorker");
    std::vector<void*> ptrs;
    ptrs.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        size_t sz = (i % 32 + 1) * 16;
        void* p = memsentry::core::track_alloc(sz);
        if (p) {
            do_not_optimize(p);
            ptrs.push_back(p);
        }
    }

    for (size_t i = 0; i < ptrs.size(); ++i) {
        void* p = ptrs[i];
        do_not_optimize(p);
        memsentry::core::track_free(p);
    }
}

#if defined(_WIN32)
struct WorkerArg {
    int thread_id;
    int iterations;
};

static DWORD WINAPI Win32StressWorker(LPVOID lpParam) {
    auto* arg = static_cast<WorkerArg*>(lpParam);
    thread_stress_worker(arg->thread_id, arg->iterations);
    return 0;
}
#endif

int main() {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    void* initial = memsentry::core::track_alloc(64);
    if (!initial)
        return 1;
    memsentry::core::track_free(initial);

    constexpr int NUM_THREADS = 4;
    constexpr int ITERATIONS_PER_THREAD = 250;

    auto baseline_stats = memsentry::get_stats();

#if defined(_WIN32)
    {
        HANDLE handles[NUM_THREADS];
        WorkerArg args[NUM_THREADS];
        for (int i = 0; i < NUM_THREADS; ++i) {
            args[i] = {i, ITERATIONS_PER_THREAD};
            handles[i] = CreateThread(nullptr, 0, Win32StressWorker, &args[i], 0, nullptr);
        }
        WaitForMultipleObjects(NUM_THREADS, handles, TRUE, INFINITE);
        for (int i = 0; i < NUM_THREADS; ++i) {
            CloseHandle(handles[i]);
        }
    }
#else
    {
        std::vector<std::thread> threads;
        threads.reserve(NUM_THREADS);

        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back(thread_stress_worker, i, ITERATIONS_PER_THREAD);
        }

        for (auto& t : threads) {
            t.join();
        }
    }
#endif

    auto final_stats = memsentry::get_stats();
    if (final_stats.total_allocation_count <
        baseline_stats.total_allocation_count + (NUM_THREADS * ITERATIONS_PER_THREAD)) {
        std::cerr << "[FAIL] Allocation count mismatch\n" << std::flush;
        return 1;
    }
    if (final_stats.total_free_count < baseline_stats.total_free_count + (NUM_THREADS * ITERATIONS_PER_THREAD)) {
        std::cerr << "[FAIL] Free count mismatch\n" << std::flush;
        return 1;
    }

    std::cout << "[PASSED] Multi-threaded tracker stress test completed successfully.\n" << std::flush;
    return 0;
}
