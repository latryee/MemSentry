# ADR 0001: 64-Shard Cache-Aligned Concurrent Tracker Architecture

## Status
Accepted

## Context
A memory leak detector and heap profiler must intercept every allocation and deallocation across multiple concurrent threads. Tracking allocations requires maintaining a mapping from allocated memory pointers (`const void*`) to metadata records (`AllocationRecord`).

In multi-threaded applications, tracking metadata contention is a primary scalability bottleneck:
- A single global mutex serializes all `new`/`delete` operations, causing massive throughput collapse (>90% slowdown on 8+ cores).
- Fully lock-free open-addressed hash tables (e.g. split-ordered lists or atomic linear probing) suffer from extreme memory allocation complexity during dynamic rehashing and cannot easily store non-trivial records (containing callstack arrays and timestamp structures) without auxiliary allocations that can trigger allocator recursion loops.

## Decision
We implemented a **64-Shard Concurrent Tracker** (`ShardedTracker`), where:
1. The table is partitioned into 64 independent `Shard` structures.
2. Each `Shard` is strictly cache-line aligned (`alignas(64)`) to eliminate false sharing between CPU L1/L2 caches.
3. Each shard contains its own fine-grained `std::mutex` and an `std::unordered_map<const void*, AllocationRecord>`.
4. Pointers are mapped to shards using a fast bitwise mixing hash function:
   ```cpp
   static inline size_t get_shard_index(const void* ptr) noexcept {
       uintptr_t val = reinterpret_cast<uintptr_t>(ptr);
       return ((val >> 4) ^ (val >> 10)) & (SHARD_COUNT - 1);
   }
   ```
5. Shard count is fixed to 64 (a power of 2), enabling single-cycle bitwise AND indexing.

## Consequences

### Positive
- **High Throughput Scaling**: Lock contention is reduced by a factor of 64. Benchmark throughput scales from 5.34 M ops/s on 1 thread to 8.66 M ops/s on 8 threads.
- **Cache-Line Isolation**: `alignas(64)` prevents CPU core cache invalidation storms.
- **Simplicity and Reliability**: Avoids complex lock-free memory reclamation schemes (hazard pointers or epoch-based reclamation) that could allocate memory internally and induce deadlocks.

### Negative
- Taking a global snapshot (`snapshot_all`) requires sequentially acquiring the 64 shard locks. However, snapshots are taken infrequently (on request or exit), making this trade-off ideal.

## Alternatives Considered
- **Single Global Mutex**: Rejected due to severe multi-threaded lock serialization.
- **Lock-Free Hash Map (Atomic Probing)**: Rejected due to rehashing allocation re-entrancy and lack of non-trivial value storage without auxiliary allocations.
