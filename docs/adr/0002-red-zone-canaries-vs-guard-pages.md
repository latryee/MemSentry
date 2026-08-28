# ADR 0002: Red-Zone Magic Canaries vs OS Virtual Memory Guard Pages

## Status
Accepted

## Context
Detecting heap buffer underruns and buffer overruns (out-of-bounds writes) is essential for memory safety. Two primary approaches exist in systems programming:
1. **OS Virtual Memory Guard Pages (`mprotect` on POSIX / `VirtualProtect` on Windows / Electric Fence / PageHeap)**: Placing an inaccessible read/write protected MMU memory page immediately before and after every allocated block.
2. **Red-Zone Magic Signature Canaries**: Padding allocated buffers with fixed 64-bit cryptographic/magic integer signatures and verifying their integrity upon deallocation.

## Decision
MemSentry adopts **64-bit Red-Zone Magic Canaries** (`CANARY_HEADER_MAGIC = 0xDEADBEEFCAFEBABE` and `CANARY_FOOTER_MAGIC = 0xBAADF00D5EADC0DE`) placed immediately adjacent to the allocated payload.

## Consequences

### Positive
- **Near-Zero Memory Overhead**: Guard pages require rounding every single allocation up to a 4 KB or 64 KB page boundary, inflating a 32-byte allocation by 128x to 256x. MemSentry adds only 16–32 bytes of metadata and footer padding per allocation.
- **Ultra-Low Latency**: Invoking kernel system calls (`mprotect`/`VirtualProtect`) per allocation incurs a massive performance penalty (~5,000 ns to 20,000 ns per alloc). MemSentry's canary initialization and verification takes under 6 ns per allocation.
- **No Page Table Exhaustion**: Avoids consuming OS page table entries (PTEs) or triggering Virtual Address Space limits on 32-bit/64-bit processes.

### Negative
- Canaries detect corruption **at deallocation time** rather than on the exact instruction that causes the overrun. For synchronous hardware page-fault interception, developers can complement MemSentry with AddressSanitizer during specialized debug sessions.

## Alternatives Considered
- **OS Guard Pages (PageHeap / Electric Fence)**: Rejected due to catastrophic 50x–200x memory explosion and heavy kernel syscall overhead.
- **Shadow Byte Mapping (ASan-style)**: Rejected as it requires compiler instrumentation pass (`-fsanitize=address`) and cannot function as a drop-in static link library.
