# TAMARIN v0.4.2 — Benchmark Methodology & Infrastructure Specification

## 1. Overview and Architectural Objectives

The purpose of the TAMARIN v0.4.2 benchmark infrastructure is to establish a rigorous, defensible, reproducible, and mathematically verified performance engineering baseline.

TAMARIN's guiding principle is:
> *No performance claims without reproducible benchmark evidence, strict contract enforcement, and workload equivalence.*

The v0.4.2 milestone establishes full benchmark contract enforcement, exact intermediate validation, aligned timing boundaries, and bootstrap statistical intervals across:
1. **TAMARIN** (`-O0`, `-O1`, `-O2`, `-O3`) compiled with `tamarin.exe` v0.4.2 (LLVM backend, native x86-64).
2. **Optimized C** compiled with LLVM Clang (`-O2`, `-O3`).
3. **Optimized C** compiled with Microsoft Visual C++ (`cl.exe /O2 /W3`).
4. **Safe Rust** compiled in release mode (`cargo build --release`, baseline non-LTO, and `release-lto`).

---

## 2. Benchmark Environment & System Specifications

All benchmarks were recorded on a dedicated reference workstation under identical operating conditions with background processes minimized and power profile set to high-performance.

### 2.1 Hardware Specifications
| Component | Specification |
|:---|:---|
| **Processor (CPU)** | AMD Ryzen 7 7840HS with Radeon 780M Graphics |
| **Microarchitecture** | Zen 4 (x86-64-v3 / AVX-512 capable) |
| **Cores / Threads** | 8 Physical Cores / 16 Logical Processors |
| **Base / Boost Clock** | 3.80 GHz base, up to 5.10 GHz max boost |
| **L1 Cache** | 512 KB total (32 KB instruction + 32 KB data per core) |
| **L2 Cache** | 8 MB total (1024 KB per core) |
| **L3 Cache** | 16 MB shared |
| **System Memory (RAM)** | 16.0 GB DDR5 |
| **Storage** | NVMe PCIe Gen 4 SSD |

### 2.2 Operating System & Platform ABI
| Parameter | Configuration |
|:---|:---|
| **Operating System** | Microsoft Windows 11 Home 64-bit (Build 26200 / x86_64) |
| **C Runtime** | Microsoft Visual C++ CRT (UCRT 10.0.26100.0) |
| **ABI Target** | Windows x86-64 Calling Convention (`x86_64-pc-windows-msvc`) |

### 2.3 Compiler Toolchains & Versions
| Implementation | Compiler / Driver | Flags / Optimization Mode |
|:---|:---|:---|
| **TAMARIN (-O0)** | `tamarin.exe` v0.4.2 (LLVM backend) | `-O0` (No optimization, stack allocas) |
| **TAMARIN (-O1)** | `tamarin.exe` v0.4.2 (LLVM backend) | `-O1` (Basic optimization, mem2reg) |
| **TAMARIN (-O2)** | `tamarin.exe` v0.4.2 (LLVM backend) | `-O2` (Standard optimization pipeline) |
| **TAMARIN (-O3)** | `tamarin.exe` v0.4.2 (LLVM backend) | `-O3` (Aggressive vectorization and unrolling) |
| **C (Clang -O2)** | Clang 23.1.5 (`clang.exe`) | `-O2 -Wall` |
| **C (Clang -O3)** | Clang 23.1.5 (`clang.exe`) | `-O3 -Wall` |
| **C (MSVC /O2)** | MSVC 19.44.35211 (`cl.exe`) | `/nologo /O2 /W3` |
| **Rust (Release)** | Rustc 1.99.1 (`cargo build --release`) | Baseline release (`opt-level = 3`, `lto = false`, `codegen-units = 16`) |
| **Rust (Release-LTO)** | Rustc 1.99.1 (`cargo build --profile release-lto`) | LTO release (`opt-level = 3`, `lto = true`, `codegen-units = 1`) |

---

## 3. Two-Mode Measurement Architecture & Timing Alignment

### Mode A — Pure Computational Kernel Benchmarks
- **Monotonic High-Resolution Timing**: In-process `QueryPerformanceCounter` on Windows.
  - In TAMARIN: Native compiler intrinsics `__benchmark_start()` and `__benchmark_elapsed_ms()`, lowering to LLVM IR calling Win32 `QueryPerformanceCounter` and `QueryPerformanceFrequency`.
  - In C: Shared header `benchmarks/common/timer.h` implementing inline QPC wrappers.
  - In Rust: `std::time::Instant::now()` and `Instant::elapsed()`.
- **Frequency Initialization Order**:
  - `QueryPerformanceFrequency(&freq)` is strictly called and verified (`freq > 0`) *before* taking the start counter.
  - Frequency query latency is completely excluded from the measured computation interval.
- **Timing Boundary Parity**:
  - The high-resolution timer starts immediately before the computational workload begins and stops immediately after it completes.
  - Storage allocation, array zero-initialization memsets (`01_integer`, `03_arrays`, `04_structures_aos`, `04_structures_soa`, `08_memory_ops`), and buffer setup are executed *outside* the timed interval across all languages.
  - Result reduction and console I/O are executed *outside* the timed interval.

### Mode B — Process Startup Overhead Benchmark
- **Timing Mechanism**: External parent-process wall clock timing (`time.perf_counter_ns()`) measured by the runner.
- **Workload (`09_startup`)**: Minimal no-op binary that initializes the runtime and immediately terminates (`return 0` / `fn main() {}`).
- **Repetitions**: 100 measured repetitions (10 warmups) to achieve statistical stability on sub-10ms process launches.

---

## 4. Exact Validation & Strict Contract Enforcement

### 4.1 Field-by-Field Schema Enforcement
The runner no longer validates only aggregate checksums. Every workload defines a schema of all intermediate and final outputs:
- **`01_integer`**: `PRIMES:2262;COLLATZ:1834634;H:1370357862;CHECKSUM:1372194758`
- **`02_float`**: `MANDEL:4703990612284669952;MATMUL:4693648769122566158;CHECKSUM:4705341001542139908`
- **`03_arrays`**: `SCAN:636540000;TRANSFORM:256601600;FINAL:128766936;CHECKSUM:1021908536`
- **`04_structures_aos`**: `TOTAL_X:307760;TOTAL_Y:308284;TOTAL_Z:81787;TOTAL_V:21249;CHECKSUM:719080`
- **`04_structures_soa`**: `TOTAL_X:307760;TOTAL_Y:308284;TOTAL_Z:81787;TOTAL_V:21249;CHECKSUM:719080`
- **`05_function_calls`**: `FIB:5702887;ACC:556720966;CHECKSUM:562423853`
- **`06_branching`**: `PRED:21874987500000;UNPRED:17479387;CHECKSUM:21875004979387`
- **`07_virtual_dispatch`**: `DIRECT:850000000;VIRTUAL:850000000;CHECKSUM:1700000000`
- **`08_memory_ops`**: `TRANSFORMS:3276800;MATCHES:63000;BYTE_SUM:1531860;CHECKSUM:4871660`
- **`09_startup`**: Mode B clean exit code 0.

### 4.2 Exact IEEE-754 Floating-Point Bitcasting
Workload `02_float` eliminates 4-decimal approximations in favor of exact 64-bit IEEE-754 bitcast integers:
- In Tamarin: `__f64_to_bits(double)` intrinsic lowering to LLVM `bitcast double %d to i64`.
- In C: `double_to_bits(double)` in `timer.h` using `memcpy`.
- In Rust: `f64::to_bits()`.
All 9 configurations produced 100% bit-for-bit identical bit patterns down to the last mantissa bit.

### 4.3 Fail-Fast Termination
The runner rejects:
- Missing, duplicate, or unknown result fields.
- Non-zero process exit codes.
- Negative, NaN, or non-finite timings.
- Delimiter anomalies or missing colons.
- Checksum or field mismatches during warmup or measured runs.
On any violation, the campaign exits non-zero immediately and suppresses the summary report.

---

## 5. Statistical Rigor & Scheduling Protocol

### 5.1 Interleaved Round-Robin Scheduling
Configurations are executed in an **interleaved round-robin sequence**:
$$\text{For rep } 1 \dots N: \quad \text{Run}(C_1) \to \text{Run}(C_2) \to \dots \to \text{Run}(C_k)$$
This neutralizes temporal thermal throttling, operating system background activity, and CPU boost clock decay.

### 5.2 Bootstrap 95% Confidence Intervals
- 1,000 bootstrap resamples with replacement (fixed seed 42) for medians and speedup ratios.
- Preserves distribution-free interval estimates robust against non-normal latency distributions.

### 5.3 Sample Sizes & Metrics
- **Mode A (Compute)**: 5 validated warmup executions followed by 30 recorded repetitions.
- **Mode B (Startup)**: 10 validated warmup executions followed by 100 recorded repetitions.
- **Reported Metrics**: Median, 95% Bootstrap CI, Mean, Standard Deviation ($\sigma$), Min, Max, and IQR.
