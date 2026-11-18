# High-Performance Multi-Threaded Concurrency Benchmark: Tamarin vs C++ OpenMP vs Rust

**Workload:** 100,000,000 parallel operations (100 parallel work chunks x 1,000,000 iterations of 8-wide AVX2 vector additions and 4-wide vector dot products).

| Implementation | Language | Runtime Median (ms) | Speedup vs Rust | Compile Time (ms) | Binary Size (KB) | Output Checksum |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Tamarin Safe Structured Concurrency (`parallel for`)** | Tamarin | **13.15 ms** | **0.93x** | 306.0 ms | 30.0 KB | `SUM=-694967296; DOT=500000000` |
| **C++ Clang OpenMP (`schedule(static)`)** | C++ | **15.55 ms** | **0.79x** | 401.1 ms | 143.5 KB | `SUM=-694967296; DOT=500000000` |
| **C++ Clang OpenMP (`schedule(dynamic)`)** | C++ | **13.99 ms** | **0.88x** | 369.1 ms | 144.0 KB | `SUM=-694967296; DOT=500000000` |
| **Rust Scoped Threads (`std::thread::scope`)** | Rust | **12.29 ms** | **1.00x** | 545.2 ms | 159.0 KB | `SUM=-694967296; DOT=500000000` |

### Methodological & Architectural Notes
1. **Scheduling Policies:**
   - C++ OpenMP `schedule(static)` pre-partitions work chunks evenly without atomic queue coordination (equivalent to pre-partitioned `std::thread::scope`).
   - C++ OpenMP `schedule(dynamic)` uses an atomic work-stealing/chunk queue across threads, directly mirroring dynamic thread pools.
   - Tamarin uses an auto-partitioning lock-free thread pool with work-stealing and zero heap allocations on dispatch.
2. **Binary Footprint & CRT Linking:**
   - Tamarin's **30.0 KB** binary dynamically links the Windows system C runtime (`ucrtbase.dll` / `kernel32.dll`), avoiding CRT static bloat while maintaining zero-overhead C ABI interoperability.
   - C++ Clang (**143.5 KB** + `libomp.dll` runtime) and Rust (**159.0 KB**) bundle static CRT, personality routines, panic handling, and unwinding machinery.
