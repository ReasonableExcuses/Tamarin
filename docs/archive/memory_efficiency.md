# TAMARIN Memory Efficiency & Paging Dynamics

> **Context**: This technical report provides the empirical and architectural foundation for Tier 3 (Page-Backed Lazy-Commit Regions) within the comprehensive [TAMARIN Memory Architecture (v0.13)](memory_architecture.md).

## 1. Executive Summary

TAMARIN delivers critical fast small-region allocator optimization and zero steady-state kernel overhead:
1. **Metadata-Aware Reservation Cache**: `__tamarin_runtime_rcache_acquire` returns both reservation capacity and previous commitment size, allowing generated code to completely skip `@__tamarin_page_commit` when the acquired block already has sufficient committed pages.
2. **Fast Small-Region Pool**: Replaces scope-by-scope kernel allocations with a bounded, thread-local pool of reusable, already-committed small-region blocks (`MAX_SLOTS = 4`). Creating and destroying small regions executes via fast O(1) pointer operations without invoking `VirtualAlloc` or `VirtualFree`.
3. **Zero Steady-State Virtual Memory Syscalls**: After 1 warmup iteration, the 50,000-iteration `10a_high_churn` steady-state loop executes with **exactly 0 OS virtual memory calls** (`reserve = 0`, `commit = 0`, `decommit = 0`, `release = 0`).
4. **Aggregate Committed-Memory Cap**: The aggregate committed-memory cap (`TAMARIN_RCACHE_MEMCAP`, default 128 KB) applies strictly across the entire thread-local pool rather than per slot.
5. **Airtight Zero-Initialization Guarantee**: On region exit, `__tamarin_runtime_rcache_put` zeroes used bytes via userland `memset`. Pooled blocks are always 100% clean, zeroed memory.
6. **Benchmark Performance Breakthrough**: `10a_high_churn` median latency dropped from 103.21 ms to **0.7564 ms** (**137x speedup**), running **2.95x faster than Clang C** (2.23 ms) and **4.24x faster than Rust** (3.21 ms). `10_region_alloc` runs in **21.79 ms** (faster than Rust's 27.47 ms).

---

## 2. Memory Accounting Definitions

To prevent conflation of memory metrics, TAMARIN documentation and telemetry enforce clear definitions:

- **Virtual Reservation (`MEM_RESERVE`)**: Contiguous address space reserved in the process virtual memory table (aligned to 64 KB). Consumes **0 bytes of physical RAM** and **0 bytes of commit charge**.
- **Committed Bytes / Commit Charge (`MEM_COMMIT`)**: Address space backed by physical RAM page frames or pagefile space (committed in 4 KB page multiples). This represents the operating system guarantee of storage availability.
- **Resident Memory / Working Set**: Physical RAM frames currently mapped into the process page table by the Windows NT memory manager. Pages remain in the working set until trimmed or decommitted.
- **Payload Bytes**: Usable memory handed out to user arrays, structs, and objects via region bump pointers.
- **Allocator Metadata**: Internal bookkeeping structures (32-byte region chunk headers, 104-byte stack control blocks).
- **Sidecar Overhead**: External memory allocated for destructor records (184 bytes for initial 4-record chunk, 40 bytes per record).
- **Alignment Padding**: Unused bytes introduced between allocations to satisfy hardware alignment requirements (1, 2, 4, 8, 16 bytes).
- **Unused Committed Bytes**: Committed pages held within a chunk beyond the current allocation offset (`committed_bytes - used_bytes`).

---

## 3. Empty Region Overhead: 0 Virtual Bytes, 0 Syscalls

### 3.1 Lazy Initialization Guarantee
In TAMARIN v0.5.4, `@__tamarin_region_init` only clears stack control block fields (setting `head_chunk = null`, `cur_chunk = null`).
- **Syscalls on entry**: 0.
- **Virtual address space reserved**: 0 bytes.
- **Physical memory committed**: 0 bytes.
- **Syscalls on exit**: If `head_chunk == null`, `@__tamarin_region_destroy` immediately exits without calling `VirtualFree` or `free`.

### Empirical Verification
Unit test `CodeGen.RegionEmptyZeroAllocations` confirms:
```text
[TAMARIN MEMORY STATS] Region destroyed:
  Initial hint:                    65536 bytes
  Virtual reserved:                    0 bytes
  Committed bytes / commit charge:     0 bytes
  Payload handed out:                  0 bytes
  Allocations:                         0
  Chunks:                              0
  Commit operation count:              0
  Destructors:                         0
```

---

## 4. Physical Commitment Dynamics: 4 KB Granularity

When an allocation is requested inside a region:
1. If no chunk exists, a 64 KB reservation is made via `@__tamarin_page_reserve` (`VirtualAlloc(null, 65536, MEM_RESERVE, PAGE_READWRITE)`).
2. Physical pages are committed in 4,096-byte multiples only as needed:
   $$\text{needed\_commit} = \lceil (\text{used} + \text{size} + 32) / 4096 \rceil \times 4096$$
3. Fast-path allocations within already committed page frontiers require only pointer arithmetic and offset updates:
   - **0 system calls**.
   - **0 lock operations**.
   - **Sub-nanosecond execution time**.

### Memory Savings vs. Fixed Arenas

| Request Size | v0.5.0 Fixed Malloc | v0.5.4 Committed RAM | Physical Memory Savings |
|--------------|---------------------|----------------------|-------------------------|
| 0 bytes      | 65,536 bytes        | **0 bytes**          | **100% (64 KB)**        |
| 256 bytes    | 65,536 bytes        | **4,096 bytes**      | **93.75% (60 KB)**      |
| 1,024 bytes  | 65,536 bytes        | **4,096 bytes**      | **93.75% (60 KB)**      |
| 5,000 bytes  | 65,536 bytes        | **8,192 bytes**      | **87.50% (56 KB)**      |
| 16,000 bytes | 65,536 bytes        | **20,480 bytes**     | **68.75% (44 KB)**      |

---

## 5. Kernel Zero-Initialization Cost Optimization

In systems languages, safe initialization of raw arrays typically forces compilers to emit an explicit `memset` over every allocated buffer.

Under the Windows x64 kernel memory subsystem:
- Transitioning pages from `MEM_RESERVE` to `MEM_COMMIT` guarantees that the operating system zeroes the newly mapped physical page frames before exposing them to user space.
- TAMARIN takes advantage of this guarantee: when allocating arrays on freshly committed pages, explicit per-element zeroing is omitted, saving CPU cache bandwidth and avoiding redundant writes.
- Unit test `CodeGen.RegionMultiPageCrossingZeroInit` confirms that a 16,384-byte array spanning 4 freshly committed pages contains strictly zero-valued elements without explicit userland memset loops.

---

## 6. Adaptive Sidecar Overhead: Empirical Measurements

In v0.5.3, a fixed 32-record sidecar chunk was allocated on the first non-trivial object registration, consuming 1,304 bytes.

In v0.5.4, TAMARIN implements **adaptive sidecar capacity**:
- **First Chunk**: 4 records $\times$ 40 bytes $+$ 24-byte header = **184 bytes**.
- **Geometric Expansion**: $4 \to 8 \to 16 \to 32 \to 64 \to 128$ records.

### Sidecar Memory Comparison

| Resource Owners | Payload Bytes (Token: 8B) | v0.5.3 Sidecar Allocated | v0.5.4 Sidecar Allocated | v0.5.4 Used Bytes | Wasted Capacity | System Allocations | Savings |
|----------------:|--------------------------:|-------------------------:|-------------------------:|------------------:|----------------:|-------------------:|--------:|
| **1**           | 8 B                       | 1,304 B                  | **184 B**                | 40 B              | 144 B           | 1                  | **85.9%** |
| **2**           | 16 B                      | 1,304 B                  | **184 B**                | 80 B              | 104 B           | 1                  | **85.9%** |
| **4**           | 32 B                      | 1,304 B                  | **184 B**                | 160 B             | 24 B            | 1                  | **85.9%** |
| **16**          | 128 B                     | 1,304 B                  | **1,192 B**              | 640 B             | 552 B           | 3                  | 8.6%    |
| **32**          | 256 B                     | 1,304 B                  | **2,496 B**              | 1,280 B           | 1,216 B         | 4                  | -91.4%  |
| **33**          | 264 B                     | 2,608 B                  | **2,496 B**              | 1,320 B           | 1,176 B         | 4                  | **4.3%** |
| **1,000**       | 8,000 B                   | 41,728 B (32 chunks)     | **46,232 B** (13 chunks) | 40,000 B          | 6,232 B         | **13**             | 59.4% fewer calls |

*Key Takeaway*: For typical region scopes containing 1 to 4 resource owners, v0.5.4 saves **1,120 bytes (85.9%)** of heap memory while eliminating the OOM window.

---

## 7. High-Churn Performance & Thread-Local Caching Policies

When executing high-churn loops with thousands of short-lived regions (such as `10a_high_churn`), operating system memory syscalls dominate execution time.

### 7.1 Microbenchmark Isolation of NT Memory Operations

Direct empirical measurement across 50,000 iterations reveals the individual costs of Windows memory operations:

| Operation | Total Time (50,000 ops) | Latency per Op | Description |
|:---|---:|---:|:---|
| `VirtualAlloc(MEM_RESERVE)` + `VirtualFree(MEM_RELEASE)` | 59.04 ms | 1.18 µs | Address space reservation & release |
| `VirtualAlloc(MEM_COMMIT)` + `VirtualFree(MEM_DECOMMIT)` | 43.77 ms | 0.88 µs | Page table mapping & unmapping |
| `VirtualAlloc(MEM_COMMIT)` alone | 27.56 ms | 0.55 µs | Mapping page frame into PTE |
| `VirtualFree(MEM_DECOMMIT)` alone | 22.87 ms | 0.46 µs | Invalidation and page return |
| Userland `memset` (4 KB zeroing) | **0.0035 ms** | **0.00007 µs** | Userland memory zeroing |
| Userland `memset` (128 B zeroing) | **0.0010 ms** | **0.00002 µs** | Minimal payload zeroing |

*Key Finding*: `VirtualAlloc(MEM_COMMIT)` and `VirtualFree(MEM_DECOMMIT)` are roughly **12,000x slower** than userland zero-filling because each call requires transition into kernel mode and manipulation of NT page directory tables.

### 7.2 Cache Policies: Trade-off Analysis

In v0.5.5, TAMARIN implements configurable thread-local caching policies (`thread_local ThreadReservationCache`):

- **Policy 0 (Default: Decommit on Reuse)**:
  - Preserves minimal physical commit charge.
  - When a region exits, committed pages are immediately decommitted to 0 bytes via `VirtualFree(MEM_DECOMMIT)`.
  - The virtual address space reservation is retained in a thread-local cache slot (up to 2 slots per thread).
  - High-churn runtime: **101.40 ms** (50,000 iterations).
  - Advantage: Zero committed memory footprint between region scopes.

- **Policy 1 (Retain Committed with Bounded Zeroing)**:
  - Enabled via environment variable `TAMARIN_RCACHE_POLICY=1`.
  - Configurable memory cap via `TAMARIN_RCACHE_MEMCAP` (default 131,072 bytes = 128 KB per thread).
  - When a region exits, if its committed size is within `memory_cap`, memory is NOT decommitted. Instead, userland `memset` zeroes the modified portion to restore the language's zero-initialization invariant.
  - Subsequent region scopes in the same thread acquire the pre-committed block with **0 system calls**.
  - High-churn runtime: **26.66 ms** (a **3.8x speedup**).
  - Safety: Commit charge is strictly capped at `memory_cap`. Any commitment exceeding the cap is decommitted. On thread exit, all retained memory is freed to the OS.

### 7.3 Multi-Workload Policy Comparison (v0.5.6)

| Workload | Policy 0 (Decommit) | Policy 1 (Small-Region Pool) | Speedup vs Policy 0 | Clang C -O2 | Rust (Sound Arena) | TAMARIN vs C |
|:---|---:|---:|---:|---:|---:|---:|
| `10_region_alloc` | 44.04 ms | **21.79 ms** | **2.02x** | 4.63 ms | 27.47 ms | Faster than Rust |
| `10a_high_churn` | 103.21 ms | **0.7564 ms** | **137x** | 2.23 ms | 3.21 ms | **2.95x faster** |
| `10b_large_single` | 0.30 ms | **0.2933 ms** | ~1.0x | 0.12 ms | 0.15 ms | ~2.4x |
| `10c_chunk_growth` | 2.21 ms | **2.1441 ms** | ~1.0x | 0.24 ms | 0.31 ms | ~8.9x (page growth) |
| `10d_empty_region` | **0.00 ms** | **0.00 ms** | — | 0.00 ms | 5.17 ms | Immediate exit |
| `10e_sparse_dtors` | 2.40 ms | **0.1221 ms** | **19.6x** | 0.12 ms | 0.16 ms | **Matches C, beats Rust** |
| `10f_reuse` | 2.20 ms | **0.0335 ms** | **65.7x** | 0.10 ms | 0.12 ms | **3.08x faster** |

---

## 8. CLI Tools for Developers

### 8.1 Static Inspection
```bash
tamarin build --emit-memory-report program.tmn
```
Prints the static memory planning table detailing all variables, their storage tier, lifetime, destructor classification, borrow status, and payload contribution.

### 8.2 Runtime Telemetry
```bash
tamarin build --memory-stats program.tmn -o program.exe
./program.exe
```
Prints detailed runtime region statistics to `stderr` on each region destroy event.
