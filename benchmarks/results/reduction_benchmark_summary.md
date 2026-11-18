# Parallel Reduction Benchmark: Tamarin v0.11/v0.13 vs C++ OpenMP vs Rust

**Workload:** 100,000,000 parallel operations (100 parallel work chunks x 1,000,000 iterations of 8-wide AVX2 vector additions and 4-wide vector dot products with dual reductions).

## 1. Runtime Performance & Compilation Comparison

| Implementation | Language | Runtime Median (ms) | Speedup vs Rust | Compile Time (ms) | Output Checksum |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Tamarin Safe Parallel Reduction (`parallel (reduction(...)) for`)** | Tamarin | **14.32 ms** | **0.96x** | 317.5 ms | `SUM=-694967296; DOT=500000000` |
| **C++ Clang OpenMP (`schedule(static) reduction(+:...)`)** | C++ | **19.51 ms** | **0.70x** | 415.0 ms | `SUM=-694967296; DOT=500000000` |
| **C++ Clang OpenMP (`schedule(dynamic) reduction(+:...)`)** | C++ | **18.28 ms** | **0.75x** | 398.3 ms | `SUM=-694967296; DOT=500000000` |
| **Rust Scoped Threads (`thread::scope` + JoinHandle reduction)** | Rust | **13.76 ms** | **1.00x** | 687.1 ms | `SUM=-694967296; DOT=500000000` |

---

## 2. Apples-to-Apples Binary Size & Deployment Comparison

Tamarin v0.13 supports both dynamic CRT (`--dynamic`, linking against `ucrtbase.dll`) and static CRT (`--static`, self-contained with `libcmt.lib` / zero CRT dependencies).

The table below breaks down executable file sizes and total deployment footprints (including mandatory vendor runtime DLLs such as Clang's `libomp140.x86_64.dll`):

### Dynamic CRT Linking Comparison (OS Standard UCRT)

| Language / Toolchain | Linkage Flags | Executable Size | External Shipped DLLs Required | Total Shipped Footprint | Standalone on Windows? |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **Tamarin v0.13** | `--dynamic` | **28.5 KB** (29,184 B) | **None** (`ucrtbase.dll` in OS) | **28.5 KB** | **Yes** (Windows 10/11) |
| **C++ Clang 23.1.5** | `-fms-runtime-lib=dll -fopenmp` | **10.5 KB** (10,752 B) | `libomp140.x86_64.dll` (849 KB) | **859.8 KB** | **No** (Needs OpenMP DLL) |
| **Rust 1.85** | `-C target-feature=-crt-static` | **161.0 KB** (164,864 B) | **None** (bundles Rust core/unwind) | **161.0 KB** | **Yes** (Windows 10/11) |

*Key finding:* Tamarin Dynamic is **5.6x smaller than Rust Dynamic** (28.5 KB vs 161.0 KB) and avoids bundling heavy runtime unwind tables. While C++ Clang's `.exe` is 10.5 KB, it cannot run without shipping LLVM's 849 KB OpenMP shared library (`libomp140.x86_64.dll`), making Tamarin's deployment footprint **30x smaller than C++ OpenMP**.

---

### Static CRT Linking Comparison (Self-Contained Executables)

| Language / Toolchain | Linkage Flags | Executable Size | External Shipped DLLs Required | Total Shipped Footprint | Standalone on Windows? |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **Tamarin v0.13** | `--static` | **181.0 KB** (185,344 B) | **None** (`KERNEL32.dll` only) | **181.0 KB** | **Yes** (100% self-contained) |
| **C++ Clang 23.1.5** | `-fms-runtime-lib=static -fopenmp` | **142.5 KB** (145,920 B) | `libomp140.x86_64.dll` (849 KB) | **994.9 KB** | **No** (Needs OpenMP DLL) |
| **Rust 1.85** | `-C target-feature=+crt-static` | **255.5 KB** (261,632 B) | **None** (`KERNEL32.dll` only) | **255.5 KB** | **Yes** (100% self-contained) |

*Key finding:* Tamarin Static is **29% smaller than Rust Static** (181.0 KB vs 255.5 KB) and **5.5x smaller than C++ OpenMP's deployment footprint** (181.0 KB vs 994.9 KB). Tamarin's static binary is completely self-contained with zero runtime dependencies beyond Windows `KERNEL32.dll`.

---

## 3. Architectural & Methodological Notes

1. **Tamarin Reduction Lowering:**
   - Thread-local accumulators allocated on worker thread stack.
   - Registers promoted via LLVM `mem2reg` for zero memory traffic inside the 1M iteration vector loop.
   - Atomic accumulation via LLVM `atomicrmw add seq_cst` executed exactly once per work chunk upon task completion.
   - Work-stealing concurrency runtime integrated directly into the compiled binary without external dependencies.
2. **C++ OpenMP:**
   - Evaluated under both `schedule(static)` (chunk-partitioned without dynamic work queue contention) and `schedule(dynamic)`.
   - Uses `#pragma omp parallel for reduction(+:sum, dot)` via LLVM Clang 23.1.5.
   - Clang OpenMP on Windows dynamically loads `libomp140.x86_64.dll` even when the C runtime itself is statically linked (`/MT`).
3. **Rust Scoped Threads:**
   - Evaluated with idiomatic `std::thread::scope` where each scoped thread returns `(local_sum, local_dot)` by value from its `JoinHandle`, summed upon joining on the main thread (zero atomics, zero contention).
   - Compiled with `rustc -C opt-level=3 -C target-cpu=native`.
