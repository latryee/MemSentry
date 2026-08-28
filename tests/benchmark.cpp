#include "memsentry/memsentry.hpp"
#include "memsentry/core/recursion_guard.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <cstdlib>

struct BenchmarkResult {
    std::string name;
    double elapsed_ms;
    uint64_t operations;
    double ops_per_sec;
    double avg_latency_ns;
};

template <typename T>
inline void do_not_optimize(T const& value) {
#if defined(_MSC_VER)
    auto volatile dummy = *reinterpret_cast<const volatile char*>(&value);
    (void)dummy;
#else
    asm volatile("" : : "r,m"(value) : "memory");
#endif
}

void print_result(const BenchmarkResult& res) {
    std::cout << "  " << std::left << std::setw(34) << res.name
              << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << res.elapsed_ms << " ms"
              << " | " << std::setw(12) << std::fixed << std::setprecision(0) << res.ops_per_sec << " ops/s"
              << " | " << std::setw(8) << std::fixed << std::setprecision(1) << res.avg_latency_ns << " ns/op\n";
}

BenchmarkResult bench_baseline(uint64_t iterations) {
    memsentry::core::RecursionGuard guard;
    auto start = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 0; i < iterations; ++i) {
        size_t sz = ((i % 16) + 1) * 32;
        void* p = std::malloc(sz);
        do_not_optimize(p);
        std::free(p);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    double ops = static_cast<double>(iterations);
    double ops_per_sec = (ops / ms) * 1000.0;
    double latency_ns = (ms * 1e6) / ops;

    return {"Baseline (Direct malloc/free)", ms, iterations, ops_per_sec, latency_ns};
}

BenchmarkResult bench_memsentry_config(const std::string& name, const memsentry::Config& config, uint64_t iterations) {
    memsentry::init(config);

    auto start = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 0; i < iterations; ++i) {
        size_t sz = ((i % 16) + 1) * 32;
        char* p = new char[sz];
        do_not_optimize(p);
        delete[] p;
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    double ops = static_cast<double>(iterations);
    double ops_per_sec = (ops / ms) * 1000.0;
    double latency_ns = (ms * 1e6) / ops;

    return {name, ms, iterations, ops_per_sec, latency_ns};
}

void bench_multithreaded(int thread_count, uint64_t iterations_per_thread) {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = true;
    memsentry::init(config);

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([iterations_per_thread]() {
            for (uint64_t i = 0; i < iterations_per_thread; ++i) {
                size_t sz = ((i % 16) + 1) * 32;
                char* p = new char[sz];
                do_not_optimize(p);
                delete[] p;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    uint64_t total_ops = thread_count * iterations_per_thread;
    double ops_per_sec = (static_cast<double>(total_ops) / ms) * 1000.0;
    double latency_ns = (ms * 1e6) / static_cast<double>(total_ops);

    std::string label = "Sharded (" + std::to_string(thread_count) + " thread" + (thread_count > 1 ? "s)" : ")");
    print_result({label, ms, total_ops, ops_per_sec, latency_ns});
}

void bench_snapshot_under_contention(int thread_count, uint64_t iterations_per_thread) {
    memsentry::Config config;
    config.enable_stacktrace = false;
    config.enable_canary = false;
    memsentry::init(config);

    std::atomic<bool> running{true};
    std::atomic<uint64_t> completed_allocs{0};
    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    for (int t = 0; t < thread_count; ++t) {
        workers.emplace_back([&running, &completed_allocs, iterations_per_thread]() {
            for (uint64_t i = 0; i < iterations_per_thread && running.load(std::memory_order_relaxed); ++i) {
                size_t sz = ((i % 16) + 1) * 32;
                char* p = new char[sz];
                do_not_optimize(p);
                delete[] p;
                completed_allocs.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<double> snapshot_latencies_us;
    snapshot_latencies_us.reserve(100);

    auto start = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count() < 100.0 ||
           snapshot_latencies_us.size() < 20) {
        auto snap_start = std::chrono::high_resolution_clock::now();
        auto snap = memsentry::take_snapshot("ContentionBenchmark");
        do_not_optimize(snap.active_allocations.size());
        auto snap_end = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(snap_end - snap_start).count();
        snapshot_latencies_us.push_back(us);
        if (completed_allocs.load(std::memory_order_relaxed) >= thread_count * iterations_per_thread) {
            break;
        }
    }
    running.store(false, std::memory_order_relaxed);

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    double total_us = std::accumulate(snapshot_latencies_us.begin(), snapshot_latencies_us.end(), 0.0);
    double avg_us = snapshot_latencies_us.empty() ? 0.0 : total_us / snapshot_latencies_us.size();
    double min_us = snapshot_latencies_us.empty() ? 0.0 : *std::min_element(snapshot_latencies_us.begin(), snapshot_latencies_us.end());
    double max_us = snapshot_latencies_us.empty() ? 0.0 : *std::max_element(snapshot_latencies_us.begin(), snapshot_latencies_us.end());

    std::cout << "  [" << thread_count << " Threads Contention] Snapshots: " << snapshot_latencies_us.size()
              << " | Avg: " << std::fixed << std::setprecision(1) << avg_us << " us"
              << " | Min: " << std::setprecision(1) << min_us << " us"
              << " | Max: " << std::setprecision(1) << max_us << " us"
              << " | Total Ops: " << completed_allocs.load() << "\n";
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "                     MemSentry Performance Benchmark Suite                      \n";
    std::cout << "================================================================================\n";

    constexpr uint64_t WARMUP = 10000;
    constexpr uint64_t ITERATIONS = 100000;

    // Warmup
    bench_baseline(WARMUP);

    std::cout << "\n[1] Single-Threaded Allocation Overhead (" << ITERATIONS << " alloc/free cycles):\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    auto r_base = bench_baseline(ITERATIONS);
    print_result(r_base);

    memsentry::Config cfg_min;
    cfg_min.enable_stacktrace = false;
    cfg_min.enable_canary = false;
    auto r_min = bench_memsentry_config("MemSentry (Minimal Tracking)", cfg_min, ITERATIONS);
    print_result(r_min);

    memsentry::Config cfg_canary;
    cfg_canary.enable_stacktrace = false;
    cfg_canary.enable_canary = true;
    auto r_canary = bench_memsentry_config("MemSentry (+ Red-Zone Canary)", cfg_canary, ITERATIONS);
    print_result(r_canary);

    memsentry::Config cfg_full;
    cfg_full.enable_stacktrace = true;
    cfg_full.max_stack_depth = 16;
    cfg_full.enable_canary = true;
    auto r_full = bench_memsentry_config("MemSentry (+ Canary & Stacktrace)", cfg_full, ITERATIONS / 2);
    print_result(r_full);

    memsentry::Config cfg_sample_10;
    cfg_sample_10.enable_stacktrace = true;
    cfg_sample_10.enable_canary = true;
    cfg_sample_10.sampling_percentage = 10;
    auto r_sample_10 = bench_memsentry_config("MemSentry (Sampling 10%)", cfg_sample_10, ITERATIONS);
    print_result(r_sample_10);

    memsentry::Config cfg_sample_1;
    cfg_sample_1.enable_stacktrace = true;
    cfg_sample_1.enable_canary = true;
    cfg_sample_1.sampling_percentage = 1;
    auto r_sample_1 = bench_memsentry_config("MemSentry (Sampling 1%)", cfg_sample_1, ITERATIONS);
    print_result(r_sample_1);

    std::cout << "--------------------------------------------------------------------------------\n";
    std::cout << " Measured Overhead vs Baseline:\n";
    std::cout << "   - Minimal (Tracking only)     : " << std::fixed << std::setprecision(2) << (r_min.elapsed_ms / r_base.elapsed_ms) << "x runtime\n";
    std::cout << "   - With Red-Zone Canary        : " << std::fixed << std::setprecision(2) << (r_canary.elapsed_ms / r_base.elapsed_ms) << "x runtime\n";
    std::cout << "   - Full (Canary + Stacktrace)  : " << std::fixed << std::setprecision(2) << ((r_full.elapsed_ms * 2.0) / r_base.elapsed_ms) << "x runtime\n";
    std::cout << "   - Sampling 10% (Production)   : " << std::fixed << std::setprecision(2) << (r_sample_10.elapsed_ms / r_base.elapsed_ms) << "x runtime\n";
    std::cout << "   - Sampling 1% (Ultra-Fast)    : " << std::fixed << std::setprecision(2) << (r_sample_10.elapsed_ms / r_base.elapsed_ms) << "x runtime\n";

    std::cout << "\n[2] Multi-Threaded Throughput Scaling (64 Shards, 25,000 ops/thread):\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    bench_multithreaded(1, 25000);
    bench_multithreaded(2, 25000);
    bench_multithreaded(4, 25000);
    bench_multithreaded(8, 25000);

    std::cout << "\n[3] Snapshot Latency Under Concurrent Allocation Contention:\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    bench_snapshot_under_contention(4, 50000);
    bench_snapshot_under_contention(8, 50000);
    bench_snapshot_under_contention(16, 50000);

    std::cout << "================================================================================\n";
    return 0;
}

