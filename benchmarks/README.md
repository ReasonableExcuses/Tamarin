# TAMARIN Benchmark Suite (v0.13)

The TAMARIN benchmark infrastructure provides rigorous, reproducible, and defensible performance comparisons between TAMARIN-generated native code, optimized C and C++ (Clang and MSVC), and safe Rust in release mode.

## Architectural Methodology & Benchmark Milestones

TAMARIN benchmarks enforce strict schema conformity, bit-exact floating-point validation (`__f64_to_bits`), aligned timer boundaries with pre-fetched frequency queries, and bootstrap 95% confidence intervals across compute kernels and startup latency:

1. **Microbenchmark Suite (`benchmarks/runner.py`)**: 17 workloads covering integer arithmetic, floating-point Mandelbrot/MatMul, array indexing, AoS vs SoA particle kinematics, function calls, branches, virtual dispatch, memory operations, startup latency, and memory region allocation.
2. **Generational Slab & Handle Dereference (`benchmarks/results/benchmark_summary.md`)**: Workloads `11a_slab_churn` and `11b_handle_deref` comparing generational handle validation (~5.97 ns) against raw pointers and Rust slotmaps.
3. **Native SIMD & N-Body Simulation (`benchmarks/simd_bench/` & `benchmarks/results/nbody_simulation_summary.md`)**: 100M AVX2 accumulations and 335M force-pair gravitational physics simulations comparing native `float8` / `float4` against C++ Clang and Rust AVX2.
4. **Structured Concurrency (`benchmarks/parallel_bench/`)**: 100M parallel vector operations comparing Tamarin work-stealing thread pools against C++ Clang OpenMP and Rust scoped threads.
5. **Parallel Reductions (`benchmarks/reduction_bench/`)**: 100M parallel reduction operations comparing Tamarin in-register accumulators against C++ OpenMP and Rust JoinHandles.

---

## Workload Catalogue

| ID | Name | Parameters & Computational Kernel | Reference Checksum |
| :--- | :--- | :--- | :--- |
| **01** | `01_integer` | Prime sieve (20,000 elems), Collatz summation (1..20,000), 64-bit unsigned hash (2M iters). | `1372194758` |
| **02** | `02_float` | Mandelbrot escape counts (200x200 grid) and dense matrix multiply (64x64, 5 passes). | Exact IEEE-754 bitcast |
| **03** | `03_arrays` | Sequential scan (500 passes x 10,000 elems), LCG random indexing (1M updates). | `1021908536` |
| **04a** | `04_structures_aos` | Particle kinematics simulation (5,000 particles x 200 steps) with Array-of-Structures layout (`@layout(point)`). | `719080` |
| **04b** | `04_structures_soa` | Particle kinematics simulation (5,000 particles x 200 steps) with Structure-of-Arrays layout (`@layout(scan)`). | `719080` |
| **05** | `05_function_calls` | Deep recursion (`fib(34)`) and tight function dispatch loop (5M iterations). | `562423853` |
| **06** | `06_branching` | Predictable pattern branch loop + unpredictable branch loop driven by 64-bit LCG. | `21875004979387` |
| **07** | `07_virtual_dispatch` | Direct method calls vs polymorphic dynamic vtable dispatch (4M calls) with `@noinline`. | `1700000000` |
| **08** | `08_memory_ops` | 16,384-byte buffer case transformations (200 passes) and linear pattern matching. | `4871660` |
| **09** | `09_startup` | Minimal process launch, CRT initialization, argument parsing, and termination. | `Exit Code 0` |
| **10** | `10_region_alloc` | Monotonic arena bump allocation across 5,000 iterations with nested regions and bulk reclamation. | `135571875000` |
| **11a** | `11a_slab_churn` | Generational slab recycling: 200 rounds of 1,024 entity insertions and removals. | Complete recycling |
| **11b** | `11b_handle_deref` | Generational handle lookup: 2.05M handle dereferences measuring validation overhead. | $\le 1.10\times$ raw pointer |
| **SIMD** | `simd_bench` | 100M 8-wide AVX2 vector additions and 4-wide dot product reductions. | `SUM=721420288; DOT=134217728` |
| **PAR** | `parallel_bench`| 100M parallel vector ops over work-stealing thread pool chunks. | Bit-exact parity with Rust |
| **RED** | `reduction_bench` | 100M parallel reductions with in-register accumulators and atomic merges. | `SUM=-694967296; DOT=500000000` |

---

## Toolchains Evaluated

- **TAMARIN**: `tamarin.exe` v0.13 (`-O0`, `-O1`, `-O2`, `-O3`, `--dynamic`, `--static`) with LLVM backend (`x86_64-pc-windows-msvc`).
- **Clang C/C++**: LLVM Clang 23.1.5 (`-O2`, `-O3`, `-fopenmp`).
- **MSVC C/C++**: Microsoft Visual C++ 19.44.35211 (VS 2022 Community) (`/O2 /W3 /openmp`).
- **Rust**: `rustc 1.85` / `cargo` (release profile: `opt-level=3`, non-LTO, `codegen-units=16`; and `release-lto`).

---

## Running the Benchmarks

```powershell
# Standard 30-repetition run across all workloads and toolchains
python benchmarks/runner.py

# Include Rust LTO configuration
python benchmarks/runner.py --include-lto

# Customize repetitions and warmups
python benchmarks/runner.py --reps 50 --warmup 5 --startup-reps 100

# Run specific workloads
python benchmarks/runner.py --workloads 10_region_alloc,04_structures_aos,04_structures_soa

# Toolchain overrides
python benchmarks/runner.py --tamarin-exe build/Release/tamarin.exe --clang-exe "C:\Program Files\LLVM\bin\clang.exe"
```

Generated outputs:
- `benchmarks/results/benchmark_data.json` — Machine-readable raw measurements, per-repetition records, toolchain metadata, statistics, and 95% bootstrap confidence intervals.
- `benchmarks/results/benchmark_summary.md` — Formatted summary comparison tables and architectural analysis.
