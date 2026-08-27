# MemSentry

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=flat-square&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg?style=flat-square)](https://github.com/)
[![License](https://img.shields.io/badge/License-MIT-green.svg?style=flat-square)](LICENSE)
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg?style=flat-square)](#building)
[![Zero-Dependency](https://img.shields.io/badge/Dependencies-Header%20%26%20Static%20Lib-orange.svg?style=flat-square)](#)

A high-performance, cross-platform Memory Leak Detector and Heap Profiling engine for modern C++ applications. Built with low-overhead sharded concurrency, red-zone canary memory corruption detection, automated backtrace symbol resolution, snapshot diffing, subsystem memory tagging, and multi-format report exports (ANSI Terminal, JSON, and standalone dark-mode HTML dashboards).

---

## Key Features

- **Zero-Intrusion Allocation Interception**: Overloads global C++ `new`/`delete` (including sized deallocation and C++17 aligned allocators) and standard allocation routines with thread-local recursion guards.
- **High-Throughput Sharded Tracker**: Utilizes 64 cache-line padded hash shards to eliminate global mutex contention in heavily multi-threaded workloads.
- **Red-Zone Canary Bounds Checking**: Automatically appends 64-bit cryptographic magic patterns to detect buffer overflows, underruns, and double-free anomalies upon deallocation.
- **Native Callstack Capture & Demangling**: Resolves source files, function signatures, and line numbers cross-platform via Windows `DbgHelp` and Linux/macOS `backtrace`/`cxxabi`.
- **Heap Snapshot Diffing**: Captures isolated memory states across execution windows (e.g. game frames, HTTP requests, or transactional loops) to isolate ephemeral vs permanent leaks.
- **Subsystem Scoping**: Tag allocations by engine or subsystem using RAII scopes (`MEMSENTRY_SCOPE_TAG("Physics")`).
- **Interactive Multi-Format Reporting**:
  - Structured ANSI colored terminal audit tables.
  - JSON schema exports for continuous integration pipelines.
  - Standalone, interactive HTML visualizer with size-distribution histograms and expandable callstack inspection.

---

## Architecture Overview

```
+-------------------------------------------------------------------------+
|                            Application Layer                            |
|             (Raw Allocations, STL Containers, Scoped Tags)              |
+------------------------------------+------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
|                       MemSentry Interception Engine                     |
|           - Global new / delete / new[] / delete[] Hooks                |
|           - Thread-Local Recursion Guard                                |
+------------------+----------------------------------+-------------------+
                   |                                  |
                   v                                  v
+------------------------------------+  +---------------------------------+
|        Red-Zone Canary Guard       |  |     Stack Backtrace Engine      |
|  (Buffer Overrun / Underrun Check) |  |  (CaptureStackBackTrace/Syms)   |
+------------------+-----------------+  +-----------------+---------------+
                   |                                      |
                   +------------------+-------------------+
                                      |
                                      v
+-------------------------------------------------------------------------+
|                       Sharded Concurrent Tracker                        |
|             - 64 Cache-Aligned Shards with Fine-Grained Locks           |
|             - Lock-Free Atomic Heap Statistics & Watermarks             |
+------------------+------------------+------------------+----------------+
                   |                  |                  |
                   v                  v                  v
+---------------------+    +--------------------+    +--------------------+
|  Terminal Reporter  |    |   Snapshot Engine  |    |   HTML Dashboard   |
|   (ANSI Tabular)    |    |  (Differential UI) |    | (Dark-Mode UI/JS)  |
+---------------------+    +--------------------+    +--------------------+
```

---

## Quick Start

### 1. Basic Leak Detection

Simply include the header and initialize the runtime at your application entry point:

```cpp
#include "memsentry/memsentry.hpp"

int main() {
    memsentry::init();

    // Intentional memory leak
    int* leaked_data = new int[128];
    (void)leaked_data;

    // Generates an interactive HTML dashboard and terminal summary on exit
    memsentry::export_html("memory_report.html");
    return 0;
}
```

### 2. Scoped Memory Subsystem Profiling

Group allocations by system or module using RAII scopes:

```cpp
#include "memsentry/memsentry.hpp"

void load_assets() {
    MEMSENTRY_SCOPE_TAG("AssetManager");
    char* texture_cache = new char[1024 * 1024];
    // ...
}

void update_physics() {
    MEMSENTRY_SCOPE_TAG("PhysicsEngine");
    float* transforms = new float[512];
    // ...
}
```

### 3. Differential Snapshot Analysis

Identify memory created and never released across specific scopes or workloads:

```cpp
#include "memsentry/memsentry.hpp"

void execute_transaction() {
    auto baseline = memsentry::take_snapshot("Baseline");

    // Perform operations
    run_workload();

    auto post_workload = memsentry::take_snapshot("PostWorkload");

    auto diff = memsentry::profiler::compare_snapshots(baseline, post_workload);
    memsentry::reporter::ConsoleReporter::print_diff(std::cout, diff);
}
```

---

## Building & Integration

### CMake Integration

```cmake
add_subdirectory(memsentry)
target_link_libraries(your_target PRIVATE memsentry)
```

### Building Examples & Tests

#### Windows (MSVC)
```cmd
build.bat
```

#### Linux / macOS (GCC / Clang)
```bash
make
```

Binaries will be placed in the `bin/` directory.

---

## Configuration Options

Runtime options can be configured via `memsentry::Config`:

```cpp
memsentry::Config config;
config.enable_canary = true;           // Enable red-zone bounds checking
config.enable_stacktrace = true;       // Enable native stack trace collection
config.max_stack_depth = 32;           // Maximum backtrace frames captured
config.stack_skip_frames = 2;          // Frames to omit from profiler internal calls
config.auto_report_on_exit = true;     // Dump leak summary upon std::atexit
config.exit_with_code_on_leak = false; // Set non-zero exit code if leaks exist
config.default_tag = "General";        // Default allocation tag

memsentry::init(config);
```

---

## Report Formats

### 1. ANSI Console Output
Outputs high-contrast tabular audits with demangled stack traces directly to `std::cout` or log files.

```
================================================================================
                             MEMSENTRY AUDIT SUMMARY                            
================================================================================
 [STATUS] MEMORY LEAKS DETECTED: 1 block(s) unreleased (512 B)
--------------------------------------------------------------------------------
 Total Allocations   : 1042 (1.45 MB)
 Total Deallocations : 1041 (1.45 MB)
 Peak Heap Usage     : 256.00 KB
 Active Heap Usage   : 512 B
================================================================================
```

### 2. Standalone Interactive HTML Dashboard
Single self-contained HTML file featuring:
- Real-time search and filter by pointer, size, or tag.
- Allocation size distribution histograms.
- Expandable callstack trees with symbol, file, and line annotations.
- Zero external CDN dependencies.

---

## Project Structure

```
.
├── CMakeLists.txt
├── Makefile
├── build.bat
├── README.md
├── include/
│   └── memsentry/
│       ├── memsentry.hpp              # Public API interface
│       ├── config.hpp                 # Configuration parameters
│       ├── types.hpp                  # Core structs and atomic metrics
│       ├── core/
│       │   ├── recursion_guard.hpp    # Thread-local recursion blocker
│       │   ├── header.hpp             # Block layout and canary verification
│       │   ├── sharded_tracker.hpp    # 64-shard concurrent tracking table
│       │   └── allocator_hooks.hpp    # Global allocator overloads
│       ├── stacktrace/
│       │   └── stacktrace.hpp         # Cross-platform stack trace resolution
│       ├── profiler/
│       │   ├── scope_tag.hpp          # RAII allocation tagging
│       │   ├── snapshot.hpp           # Differential snapshot engine
│       │   └── histogram.hpp          # Size-class distribution metrics
│       └── reporter/
│           ├── console_reporter.hpp   # ANSI terminal formatting
│           ├── json_reporter.hpp      # JSON serialization
│           └── html_reporter.hpp      # Single-file HTML visualizer
├── src/
│   ├── memsentry.cpp                  # Manager singleton & runtime hooks
│   ├── allocator_hooks.cpp            # Global new/delete definitions
│   ├── stacktrace.cpp                 # DbgHelp / libunwind implementation
│   ├── snapshot.cpp                   # Differential analysis algorithms
│   └── reporter.cpp                   # Exporter implementations
├── examples/
│   ├── 01_basic_leak.cpp              # Exit reporting and leak diagnosis
│   ├── 02_scoped_profiling.cpp        # Multi-threaded subsystem profiling
│   ├── 03_snapshot_diffing.cpp        # Delta comparison across operations
│   └── 04_buffer_overflow.cpp         # Red-zone canary violation detection
└── tests/
    ├── test_tracker.cpp               # Multi-threaded concurrency stress test
    └── test_canary.cpp                # Bounds corruption unit tests
```

---

## License

This project is released under the [MIT License](LICENSE).
