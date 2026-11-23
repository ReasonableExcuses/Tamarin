# Tamarin vs Clang C++20 vs Rust: Multi-Threaded CPU Raytracer Benchmark

## Executive Summary
This benchmark evaluates **Tamarin (v0.18.5+)**, **Clang C++20**, and **Rust (1.85+)** on an identical, unthrottled, multi-threaded CPU raytracing kernel rendered live via **SDL3 streaming textures (`SDL_LockTexture`)**.

Across 1280×720 framebuffer rendering with 16 samples per pixel (14.75 million primary rays/frame) tracing through a deterministic 124-sphere scene, all three languages produced **100% bit-for-bit identical RGBA32 checksums** (`0x2BA9B398A1D21CEF`), proving exact mathematical determinism and IEEE-754 floating point parity across both scalar loops and 8-wide AVX2 SIMD batching kernels.

---

## Benchmark System & Environment
- **Host CPU:** AMD Ryzen 7 7840HS (8 Cores / 16 Threads @ 3.8 GHz base, up to 5.1 GHz boost)
- **Host Memory:** 16 GB DDR5-5600 Dual-Channel
- **Operating System:** Microsoft Windows 11 Home x86_64
- **Graphics & Presentation:** SDL3 v3.2.0 (`SDL_TEXTUREACCESS_STREAMING`, `SDL_PIXELFORMAT_RGBA32`)
- **Compilers & Flags:**
  - **Tamarin:** `troop build --release` (`-O3`, `-march=native`, first-class `float8` SIMD vectors, zero unsafe blocks)
  - **Clang C++20:** `clang++ -std=c++20 -O3 -march=native -ffp-contract=off` (AVX2 `<immintrin.h>`, `std::jthread` scanline pool)
  - **Rust:** `cargo build --release` with `RUSTFLAGS="-C target-cpu=native"` (AVX2 `core::arch::x86_64`, Rayon `par_chunks_exact_mut`)

---

## Comprehensive Performance Metrics (10-Frame Average)

| Language & Configuration | Frame Time (ms) | Throughput (Mrays/s) | Clean Build (s) | Binary Size (KB) | Peak RSS (MB) | Unsafe Blocks | Checksum Status |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Tamarin (SIMD 8-wide)** | **197.83** | **74.53** | **0.98** | **96.5** | **12.57** | **0** | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Clang C++20 (SIMD 8-wide)** | 217.96 | 67.65 | 1.10 | 209.5 | 12.22 | N/A | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Rust (SIMD 8-wide)** | 205.74 | 71.67 | 3.98 | 276.5 | 11.98 | 3 | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Tamarin (Standard SoA)** | 959.22 | 15.37 | 0.98 | 96.5 | 12.64 | **0** | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Clang C++20 (Standard SoA)** | 911.41 | 16.18 | 1.10 | 209.5 | 12.23 | N/A | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Rust (Standard SoA)** | 985.88 | 14.96 | 3.98 | 276.5 | 12.02 | 3 | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Clang C++20 (AoS Baseline)** | 931.84 | 15.82 | 1.10 | 209.5 | 12.23 | N/A | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Rust (AoS Baseline)** | 1131.59 | 13.03 | 3.98 | 276.5 | 12.01 | 3 | **MATCH** (`0x2BA9B398A1D21CEF`) |

---

## Key Technical Findings

### 1. Mathematical Determinism & Cross-Language Bit-Exact Checksum
- All three languages achieve a **bit-for-bit identical final checksum of `0x2BA9B398A1D21CEF`**.
- Clang C++ with default optimization settings enables FMA (fused multiply-add contraction via `-ffp-contract=on`), which evaluates `a * b + c` without intermediate IEEE-754 rounding. Rust explicitly disallows FMA contraction without explicit intrinsic calls (RFC 2686), and Tamarin lowers floating-point operations cleanly to standard LLVM IR non-fused operations. Passing `-ffp-contract=off` to Clang aligned C++, Rust, and Tamarin to the exact same IEEE-754 single-precision bits across all 921,600 pixels.
- In 8-wide AVX2 SIMD, evaluating `half_b` and `c` without FMA using explicit parenthesization matches the scalar loop bit pattern 100%.

### 2. Explicit 8-Wide AVX2 SIMD Sphere Batching vs Scalar SoA
- Implementing 8-wide sphere batching with branchless root selection `(r1 <= t_min || closest_so_far <= r1) ? r2 : r1` and 8-sphere early-out masks accelerated all three languages from **~17 Mrays/s to ~70 Mrays/s** (a **4.1x speedup**).
- Tamarin achieves **74.53 Mrays/s (197.83 ms)** using high-level `float8` vectors with **zero unsafe code**, leading C++ (67.65 Mrays/s) and Rust (71.67 Mrays/s).

### 3. Concurrency & Memory Safety Architecture
- **Tamarin:** Parallelized scanlines `parallel for (int y = 0; y < 720; y++)` directly write into the 2D global array `g_framebuffer[720][1280]`. Tamarin's affine index injectivity checker statically verifies that loop index `[y]` partitions writes into disjoint scanlines with **zero data races** and **zero `unsafe` blocks** across the entire application—including all `extern "C"` SDL3 streaming texture locking (`SDL_LockTexture`), unlocking (`SDL_UnlockTexture`), and present calls.
- **Rust:** While Rayon's safe disjoint slice iterators (`fb.par_chunks_exact_mut(WIDTH)`) safely execute the raytracing loops, Rust strictly requires `unsafe` blocks wrapping all raw C ABI SDL3 calls and AVX2 intrinsics, totaling **3 unsafe blocks**.
- **C++20:** Entirely unchecked manual pointer arithmetic and thread dispatch via `std::jthread`.

### 4. Build Latency and Binary Footprint
- **Clean Release Build Time:** Tamarin built in **0.98 s**, significantly faster than Rust's `cargo build --release` (3.98 s) and comparable to Clang C++ (1.10 s).
- **Binary Size:** Tamarin produced a lean **96.5 KB** native binary, smaller than both C++ (209.5 KB) and Rust (276.5 KB).

---

## How to Reproduce
Run the automated benchmark suite:
```powershell
python benchmarks/projects/raytracer_sdl3/benchmark.py
```
Or run each implementation individually in headless mode:
```powershell
# Tamarin (SIMD or SoA)
.\benchmarks\projects\raytracer_sdl3\tamarin\bin\release\raytracer_tamarin.exe --headless 10 --simd
.\benchmarks\projects\raytracer_sdl3\tamarin\bin\release\raytracer_tamarin.exe --headless 10 --soa

# Clang C++20 (SIMD, SoA, or AoS)
.\benchmarks\projects\raytracer_sdl3\cpp\raytracer_cpp.exe --headless 10 --simd
.\benchmarks\projects\raytracer_sdl3\cpp\raytracer_cpp.exe --headless 10 --soa
.\benchmarks\projects\raytracer_sdl3\cpp\raytracer_cpp.exe --headless 10 --aos

# Rust (SIMD, SoA, or AoS)
.\benchmarks\projects\raytracer_sdl3\rust\target\release\raytracer_rust.exe --headless 10 --simd
.\benchmarks\projects\raytracer_sdl3\rust\target\release\raytracer_rust.exe --headless 10 --soa
.\benchmarks\projects\raytracer_sdl3\rust\target\release\raytracer_rust.exe --headless 10 --aos
```
