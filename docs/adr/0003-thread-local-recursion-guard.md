# ADR 0003: Thread-Local Recursion Guard vs Dynamic Binary Hook Detouring

## Status
Accepted

## Context
When global allocation hooks (`operator new`, `malloc`, `realloc`, `free`) intercept memory calls, the tracking subsystem itself (e.g. allocating `std::unordered_map` bucket nodes, resolving DbgHelp symbols, formatting output strings, or capturing stack frames) must frequently allocate memory.

Without reentrancy protection, an allocation inside the tracking engine triggers `operator new` again, resulting in an immediate **infinite recursive loop and stack overflow**.

Two design approaches can solve this:
1. **Dynamic Binary Hook Detouring (e.g. Microsoft Detours / MinHook)**: Runtime trampoline patching of binary code bytes with assembly jumps.
2. **Thread-Local RAII Recursion Guards (`thread_local int g_recursion_depth`)**: Using an atomic or thread-local counter incremented on tracker entry and checked at every hook entry point.

## Decision
MemSentry implements a **Thread-Local RAII Recursion Guard** (`core::RecursionGuard`):
1. Every hook checks `RecursionGuard::is_active()` before entering tracking logic.
2. If active, the allocation falls back directly to the raw underlying system allocator (`core::raw_system_alloc` / `HeapAlloc` / `dlsym(RTLD_NEXT)`).
3. The guard increments `g_recursion_depth` on entry and decrements it on RAII destruction, supporting arbitrary nested recursive calls.

## Consequences

### Positive
- **100% Reentrancy Immunity**: Any STL container allocation, symbol lookup, or reporter formatting triggered inside MemSentry cleanly routes to untracked raw memory without recursion.
- **Portability & Safety**: Avoids modifying binary machine instructions in `.text` memory pages, which triggers W^X security protections, antivirus heuristic flags, and platform-specific assembly maintenance.
- **Zero Thread Contention**: `thread_local` counter modifications are local to the executing CPU core, with zero bus locking or atomic CAS overhead.

### Negative
- Allocations made by internal profiling mechanisms are not included in user heap statistics, which is desirable to prevent profiling artifact pollution.

## Alternatives Considered
- **Binary Code Patching / Trampoline Detours**: Rejected due to binary fragility, DEP/W^X permission conflicts, and portability issues across ARM64/x86_64.
- **Custom Lock-Free Static Allocator for Tracker Metadata**: Rejected due to high implementation complexity compared to the elegance of thread-local fallback to raw system allocations.
