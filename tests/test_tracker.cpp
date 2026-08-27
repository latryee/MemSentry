#include "memsentry/memsentry.hpp"
#include <cassert>
#include <thread>
#include <vector>
#include <iostream>

void thread_stress_worker(int thread_id, int iterations) {
    (void)thread_id;
    MEMSENTRY_SCOPE_TAG("StressWorker");
    std::vector<void*> ptrs;
    ptrs.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        size_t sz = (i % 64 + 1) * 16;
        void* p = ::operator new(sz);
        ptrs.push_back(p);
    }

    for (void* p : ptrs) {
        ::operator delete(p);
    }
}

int main() {
    memsentry::Config config;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    constexpr int NUM_THREADS = 8;
    constexpr int ITERATIONS_PER_THREAD = 1000;

    auto baseline_stats = memsentry::get_stats();

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

    auto final_stats = memsentry::get_stats();
    assert(final_stats.total_allocation_count >= baseline_stats.total_allocation_count + (NUM_THREADS * ITERATIONS_PER_THREAD));
    assert(final_stats.total_free_count >= baseline_stats.total_free_count + (NUM_THREADS * ITERATIONS_PER_THREAD));

    std::cout << "[PASSED] Multi-threaded tracker stress test completed successfully.\n";
    return 0;
}
