# TAMARIN v0.5.9 — Expert Performance Optimization

**Date:** 2026-11-18 10:01:42 UTC  
**Host OS:** Windows-11-10.0.26200-SP0  
**Processor:** AMD64 Family 25 Model 116 Stepping 1, AuthenticAMD (Logical Cores: 16)  
**Methodology:** Mode A pure computational kernels timed via internal high-resolution QPC (setup/IO excluded); Mode B startup timed via external parent wall clock; 30 repetitions, 5 warmups; interleaved round-robin scheduling; fail-fast checksum validation; resident working set measured via Windows PSAPI `GetProcessMemoryInfo`.

> [!WARNING]
> **PARTIAL BENCHMARK CAMPAIGN:** Only a subset of configurations or workloads was evaluated. This run does not constitute a full formal comparison.

## 1. Toolchains & Compilers Tested

| Toolchain | Version / Build | Target CPU / Architecture Flags |
| :--- | :--- | :--- |
| **CLANG** | `clang version 23.1.5 (https://github.com/llvm/llvm-project 392dfff97d29b59d77e5cac6d4b6ef2585fe7117)` | `-march=native` |
| **MSVC** | `Microsoft (R) C/C++ Optimizing Compiler Version 19.44.35211 for x64` | `/arch:AVX2` |
| **RUSTC** | `rustc 1.99.1 (58cf8f9c3 2026-10-13)` | `-C target-cpu=native` |
| **CARGO** | `cargo 1.99.1 (5f94df478 2026-08-27)` | `-C target-cpu=native` |
| **TAMARIN** | `Tamarin compiler version 0.13.0 (TAMARIN v0.13.0)
Target: x86_64-pc-windows-msvc` | `-march=native` |

## 2. Mode A — Pure Computational Kernel Throughput (Median ms)

| Workload | T-O2 (ms) | T-O3 (ms) | T-Exp (ms) | Clang-O3 (ms) | MSVC-O2 (ms) | Rust-LTO (ms) | Clang-Exp (ms) | MSVC-Exp (ms) | Rust-Exp (ms) | Verification |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| `04_structures_soa` | N/A | N/A | 0.2238 | N/A | N/A | N/A | 0.3381 | 0.4657 | 0.3531 | Exact Match |
| `10_region_alloc` | N/A | N/A | 4.2146 | N/A | N/A | N/A | 5.1014 | 5.5016 | 4.6355 | Exact Match |
| `01_integer` | N/A | N/A | 4.1539 | N/A | N/A | N/A | 4.1258 | 4.5022 | 4.1071 | Exact Match |

## 2.1. Relative Performance Ratios (TAMARIN -O3 Relative to Competitors)

> **Interpretation:** Values `< 1.00x` indicate TAMARIN is faster than the competitor. Values `> 1.00x` indicate the competitor is faster. Ratios include 95% bootstrap confidence intervals.

| Workload | vs Clang Idiom (-O3) | vs MSVC Idiom (/O2) | vs Rust Idiom (LTO) | vs Clang Expert | vs MSVC Expert | vs Rust Expert |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| `04_structures_soa` | N/A | N/A | N/A | N/A | N/A | N/A |
| `10_region_alloc` | N/A | N/A | N/A | N/A | N/A | N/A |
| `01_integer` | N/A | N/A | N/A | N/A | N/A | N/A |
| **GEOMETRIC MEAN** | **0.00x** | **0.00x** | **0.00x** | **0.00x** | **0.00x** | **0.00x** |

## 2.2. Relative Performance Ratios (Expert TAMARIN Relative to Competitors)

> **Interpretation:** Values `< 1.00x` indicate Expert TAMARIN is faster than the competitor. Values `> 1.00x` indicate the competitor is faster. Ratios include 95% bootstrap confidence intervals.

| Workload | vs Clang Expert | vs MSVC Expert | vs Rust Expert | vs Normal TAMARIN (-O3) |
| :--- | :---: | :---: | :---: | :---: |
| `04_structures_soa` | **0.66x** [0.48-1.24] | **0.48x** [0.47-0.92] | **0.63x** [0.56-0.92] | N/A |
| `10_region_alloc` | **0.83x** [0.74-0.85] | **0.77x** [0.72-0.79] | **0.91x** [0.79-0.95] | N/A |
| `01_integer` | **1.01x** [0.94-1.10] | **0.92x** [0.90-1.01] | **1.01x** [0.98-1.11] | N/A |
| **GEOMETRIC MEAN** | **0.82x** | **0.70x** | **0.83x** | **0.00x** |

## 2.3. Geometric Mean Performance Summary

| Comparison Benchmark | Geometric Mean Ratio | Status |
| :--- | :---: | :--- |
| Normal TAMARIN -O3 vs **Clang Idiomatic (-O3)** | **0.00x** | TAMARIN Leads |
| Normal TAMARIN -O3 vs **MSVC Idiomatic (/O2)** | **0.00x** | TAMARIN Leads |
| Normal TAMARIN -O3 vs **Rust Idiomatic (Release-LTO)** | **0.00x** | TAMARIN Leads |
| Normal TAMARIN -O3 vs **Clang Expert (-O3 Native SIMD)** | **0.00x** | TAMARIN Leads |
| Normal TAMARIN -O3 vs **MSVC Expert (/O2 AVX2)** | **0.00x** | TAMARIN Leads |
| Normal TAMARIN -O3 vs **Rust Expert (Release-LTO Native)** | **0.00x** | TAMARIN Leads |
| **Expert TAMARIN** vs **Clang Expert (-O3 Native SIMD)** | **0.82x** | Expert TAMARIN Leads |
| **Expert TAMARIN** vs **MSVC Expert (/O2 AVX2)** | **0.70x** | Expert TAMARIN Leads |
| **Expert TAMARIN** vs **Rust Expert (Release-LTO Native)** | **0.83x** | Expert TAMARIN Leads |
| **Expert TAMARIN** vs **Normal TAMARIN (-O3)** | **0.00x** | Expert TAMARIN Leads |

## 2.4. Peak Working-Set Memory (KB) & Binary Size (KB)

| Workload | T-O3 WS (KB) | T-Exp WS (KB) | Clang-Exp WS (KB) | Rust-Exp WS (KB) | T-O3 Bin (KB) | T-Exp Bin (KB) | Clang-Exp Bin (KB) | Rust-Exp Bin (KB) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| `04_structures_soa` | N/A | 4390.0 | 4010.0 | 4390.0 | N/A | 23.0 | 143.5 | 143.5 |
| `10_region_alloc` | N/A | 4300.0 | 3958.0 | 4392.0 | N/A | 29.5 | 152.5 | 158.0 |
| `01_integer` | N/A | 4404.0 | 3968.0 | 4340.0 | N/A | 21.0 | 144.5 | 143.0 |

## 3. Mode B — Process Startup and Runtime Initialization Overhead

> **Methodology:** External parent-process wall clock timing (`time.perf_counter_ns()`) measuring launch, Windows CRT/runtime initialization, main function entry, and termination of a minimal no-op executable. 100 repetitions, 10 warmups, interleaved scheduling.

| Configuration | Median (ms) | 95% CI (ms) | Mean (ms) | StdDev (ms) | Peak WS (KB) | Binary Size (KB) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |

## 4. Architectural Analysis: AoS vs SoA SIMD Vectorization

Comparison of Array of Structures (`@layout(point)`) versus Structure of Arrays (`@layout(scan)`) across 5,000 particles and 200 simulation steps:

| Language / Toolchain | AoS Median (ms) | SoA Median (ms) | SoA Speedup Factor |
| :--- | :---: | :---: | :---: |

## 5. Tamarin Optimization Level Progression

| Workload | -O0 (Unoptimized) | -O1 (Basic) | -O2 (Standard) | -O3 (Aggressive) | Expert | Expert vs -O3 Speedup | -O3 vs -O0 Speedup |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| `04_structures_soa` | N/A ms | N/A ms | N/A ms | N/A ms | 0.2238 ms | **N/A** | **N/A** |
| `10_region_alloc` | N/A ms | N/A ms | N/A ms | N/A ms | 4.2146 ms | **N/A** | **N/A** |
| `01_integer` | N/A ms | N/A ms | N/A ms | N/A ms | 4.1539 ms | **N/A** | **N/A** |

