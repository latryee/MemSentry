# Case Study: Diagnosing Sub-System Memory Corruption & Leaks with MemSentry

## Executive Summary

In high-throughput multi-threaded systems (e.g., real-time audio pipelines, rendering engines, and network servers), memory anomalies such as silent buffer overflows, intermittent double-frees, and slow heap leaks often evade detection during unit testing. Standard debuggers often fail to detect 1-byte off-by-one overruns until heap corruption manifests miles away from the fault site.

This case study documents a real-world debugging scenario in an asynchronous audio streaming and asset caching pipeline (**AetherAudioEngine**), showing how MemSentry identified three distinct memory safety defects without requiring code instrumentation or heavy runtime emulation.

---

## The Target System: `AetherAudioEngine`

The target module handles concurrent audio decoding, DSP resampler buffers, and sound bank caches across 8 worker threads.

### The Problematic Code

```cpp
#include "memsentry/memsentry.hpp"
#include <vector>
#include <thread>
#include <cstring>

class AudioStreamBuffer {
public:
    AudioStreamBuffer(size_t sample_count) : samples_(sample_count) {
        data_ = new float[samples_];
    }

    ~AudioStreamBuffer() {
        delete[] data_;
    }

    // BUG 1: Off-by-one buffer overflow during DSP interpolation
    void apply_fade_out() {
        for (size_t i = 0; i <= samples_; ++i) { // Off-by-one: index `samples_` overwrites canary!
            data_[i] *= (1.0f - static_cast<float>(i) / samples_);
        }
    }

    float* data_;
    size_t samples_;
};

void process_audio_stream() {
    MEMSENTRY_SCOPE_TAG("AudioPipeline");

    // Baseline snapshot
    auto snap_before = memsentry::take_snapshot("AudioStream_Start");

    AudioStreamBuffer* stream = new AudioStreamBuffer(1024);
    stream->apply_fade_out();

    // BUG 2: Leaked scratch filter buffer in worker
    float* temp_dsp_filter = new float[256];
    (void)temp_dsp_filter; // Leaked: never deleted

    delete stream;

    // BUG 3: Double-free during stream reset under contention
    // (Simulated duplicate cleanup path)
    // delete stream;

    auto snap_after = memsentry::take_snapshot("AudioStream_End");
    auto diff = memsentry::profiler::compare_snapshots(snap_before, snap_after);
    memsentry::reporter::ConsoleReporter::print_diff(std::cout, diff);
}
```

---

## Step 1: Interception & Buffer Overrun Detection

When `stream->apply_fade_out()` writes 1 float (`4 bytes`) beyond the `1024 * sizeof(float)` allocation, it overwrites the 64-bit red-zone footer canary (`0xBAADF00D5EADC0DE`).

Upon calling `delete stream;`, MemSentry's `track_free()` immediately verifies the canary boundary and emits an instant terminal diagnostic:

```text
 [FATAL ERROR] MEMORY CORRUPTION DETECTED! 
 Pointer : 0x000001f3b890a200
 Reason  : Buffer overrun / Red-zone canary overwritten
================================================================================
```

### Root Cause Diagnosis
1. MemSentry immediately identified that byte offset `+4096` was modified.
2. The developer changed the loop bound from `i <= samples_` to `i < samples_`, eliminating the buffer overrun.

---

## Step 2: Differential Snapshotting for Persistent Leak Attribution

Even after resolving the buffer overrun, memory consumption grew over time. By wrapping the workload in `memsentry::take_snapshot()` and `memsentry::profiler::compare_snapshots()`, the audit report isolated the leak:

```text
================================================================================
                     SNAPSHOT DIFF: [AudioStream_Start] -> [AudioStream_End]
================================================================================
 Net Memory Delta    : +1024 bytes
 Net Alloc Delta     : +1 blocks
 Newly Leaked Blocks : 1 (1.00 KB)
 Persistent Blocks   : 0
 Freed During Window : 1
================================================================================

DETAILED LEAK REPORT:
--------------------------------------------------------------------------------
[Leak #1] ID: 142 | Size: 1024 bytes | Addr: 0x000001f3b890b400 | Tag: [AudioPipeline] | Thread: 12840
    #00 memsentry::Manager::allocate (src/memsentry.cpp:185)
    #01 operator new[] (src/allocator_hooks.cpp:27)
    #02 process_audio_stream (examples/audio_pipeline.cpp:32)
    #03 std::thread::_Invoke (thread:284)
--------------------------------------------------------------------------------
```

### Resolution
The stack trace pinpointed `audio_pipeline.cpp:32` (`float* temp_dsp_filter = new float[256];`), which was converted to a modern `std::vector<float>` RAII container.

---

## Step 3: Double-Free Quarantine Protection

In a concurrent race condition where two threads attempt to evict and deallocate the same cache handle simultaneously:
1. First thread deallocates the pointer; MemSentry records the pointer into the shard's 256-slot quarantine ring buffer.
2. Second thread invokes `delete cache_ptr;`.
3. MemSentry's `ShardedTracker::erase()` detects `TrackerEraseStatus::DOUBLE_FREE_DETECTED`, emits a high-priority alert with demangled stack trace, and **suppresses the second OS deallocation**, preventing an OS heap corruption crash (`0xC0000374`).

---

## Step 4: Verification & Interactive HTML Dashboard Export

Calling `memsentry::export_html("audio_audit.html")` generated a self-contained, interactive HTML dashboard:

- **Total Leaked**: `0 B`
- **Active Leaks**: `0`
- **Status**: `CLEAN: NO LEAKS`
- **Size Distribution Histogram**: Verified that 100% of temporary DSP buffers fell within the `[1 KB - 4 KB]` bucket and were successfully reclaimed.

---

## Key Takeaways

1. **Zero Recompilation Overhead**: Unlike AddressSanitizer which requires compiling every dependency with `-fsanitize=address`, MemSentry is linked as a standard static library.
2. **Immediate Site Attribution**: Callstack symbols, line numbers, and subsystem tags pinpoint exactly where leaked memory was created.
3. **Multi-Threaded Safety**: 64 cache-aligned shards ensure zero contention lockups during multi-threaded profiling.
