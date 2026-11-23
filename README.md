<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/icon_white.svg">
    <source media="(prefers-color-scheme: light)" srcset="assets/icon_black.svg">
    <img alt="Tamarin Logo" src="assets/icon_black.svg" width="130">
  </picture>
</p>

<h1 align="center">Tamarin</h1>

<p align="center">
  <a href="https://github.com/ReasonableExcuses/Tamarin/actions/workflows/ci.yml">
    <img src="https://github.com/ReasonableExcuses/Tamarin/actions/workflows/ci.yml/badge.svg" alt="Tamarin CI Status">
  </a>
</p>

<p align="center">
  <strong>A high-performance systems programming language with idiomatic C syntax, zero-cost compile-time memory safety, and LLVM-native code generation.</strong>
</p>

<p align="center">
  Tamarin combines the mechanical simplicity of C with static borrow and affine move tracking, first-class Structure-of-Arrays (<code>@layout(scan)</code>) auto-vectorization, zero-glue C99 ABI emission, and the native <code>troop</code> package manager.
</p>

---

## Technical Architecture

The Tamarin compilation pipeline translates high-level semantic guarantees directly into vectorizable LLVM IR and standalone C99 headers:

```text
+---------------------------------------------------------------------------------+
|                            Tamarin Compilation Pipeline                         |
+---------------------------------------------------------------------------------+
   Source (.tmn)
           │
           ▼
   Handcrafted Lexer & Recursive Descent Parser  ──────►  AST (Abstract Syntax Tree)
                                                                  │
           ┌──────────────────────────────────────────────────────┘
           ▼
   Semantic Analysis (Sema)
     ├── Static Type Checking & Monomorphization
     ├── Borrow Checker (ProvenanceSet + ActiveBorrow Verification)
     ├── Move Checker (Destructive Moves & Stack Drop Flags)
     └── Data Race Checker (Affine Injectivity on parallel for)
           │
           ▼
   Tamarin IR (TIR) - SSA Control Flow Graph
     ├── Range Analysis & Bounds Check Elimination (BCE)
     └── Alias Analysis & Redundancy Elimination
           │
           ▼
   LLVM Code Generation & Optimization (-O3, --target-cpu native, --fast-math)
           │
           ├──────────────────────────────┬──────────────────────────────┐
           ▼                              ▼                              ▼
   Native Binary (.exe/.dll)       C99 Header (.h)                LLVM IR (.ll)
   (Static / Dynamic CRT)       (tamarin --emit-header)        (tamarin --emit-llvm)
```

---

## Key Technical Pillars

- **Idiomatic C Syntax**: Exact C function declarations (`ReturnType Name(Args...)`), structs, and control flow without superfluous sigils or lifetime annotations.
- **Zero-Cost Memory Safety**: Compile-time reader-writer lock tracking (`ActiveBorrow`), pointer provenance roots (`ProvenanceSet`), and linear resource consumption (`move(x)`) with zero garbage collector or runtime overhead.
- **Data-Oriented `@layout(scan)`**: Transparently transforms arrays of structs into contiguous Structure-of-Arrays (SoA) column buffers, streaming data through CPU cache lines and auto-vectorizing with AVX2/AVX-512.
- **Compile-Time Data-Race Freedom**: Proves affine index injectivity $f(i) = c_1 \cdot i + c_2$ ($c_1 \ne 0$) on `parallel for` loops, guaranteeing disjoint memory writes across worker threads.
- **High-Performance Standard Library**: Google Abseil-style Swiss Table `HashMap<K, V>` with 16-way SIMD/group probing, zero-allocation streaming `std.json` with contiguous tape AST representation, and hardware-accelerated bit-twiddling intrinsics (`std.bits`).
- **Native C99 ABI & Header Generation**: Direct `@c_layout` struct alignment, `@export` un-mangled calling conventions, and automated C99 `.h` emission via `--emit-header`.
- **Automated C Binding Generator (`tamarin-bindgen`)**: `libclang`-powered tool that translates arbitrary C/C++ header files into idiomatic Tamarin bindings with zero manual boilerplate.
- **Language Server Protocol (`tamarin-lsp`)**: Production-grade LSP 3.17 server with sub-50ms diagnostics, hover type tooltips, go-to-definition, reference finder, hierarchical outline, code completion, and signature help.
- **Integrated Package Management**: The `troop` build tool and package manager provides deterministic SemVer 2.0.0 resolution, dependency graph DAG scheduling, automatic native `.dll` deployment on Windows, and reproducible lockfiles (`troop.lock`).
- **Graphics & Native Ecosystem**: Zero-glue bindings and immediate-mode GUI support for Raylib 5.5 (`tamarin-raylib`), SDL3 platform, and multi-backend `SDL_GPU` (Vulkan, Direct3D 12, Metal, OpenGL) with project scaffolding templates (`troop new -t raylib-game`, `troop new -t sdl3-app`).

---

## Quick Start

### 1. Project Scaffolding with `troop`
```bash
# Create a new binary application package
troop new my_app --bin
cd my_app

# Build and execute with maximum optimizations (-O3, native vectorization)
troop run --release
```

### 2. Standalone Compilation & C99 Header Export
```bash
# Compile to a native dynamic library and emit standard C99 headers
tamarin src/lib.tmn -O3 --emit-header --shared -o bin/libcore.dll
```

### 3. Idiomatic Code Showcase
```tamarin
// src/main.tmn
import std.io;

// 1. Data-Oriented SoA Struct: automatically de-interleaved into column vectors
@layout(scan)
struct Particle {
    float x;
    float y;
    float vx;
    float vy;
};

// 2. Algebraic Data Types: tagged unions with payload
enum ComputeResult {
    Success(int),
    Overflow
}

// 3. Exact C-style function signature with affine move semantics
ComputeResult simulate(Particle[1024] particles, int steps) {
    if (steps <= 0) {
        return ComputeResult::Overflow;
    }

    // Auto-vectorized column operations
    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < 1024; i++) {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
        }
    }

    // Stack-allocated, borrow-checked closure (16-byte fat pointer)
    int threshold = 100;
    auto filter_fn = (float pos) -> bool => {
        return pos > (float)threshold;
    };

    int active_count = 0;
    for (int i = 0; i < 1024; i++) {
        if (filter_fn(particles[i].x)) {
            active_count++;
        }
    }

    return ComputeResult::Success(active_count);
}

int main() {
    Particle particles[1024];
    for (int i = 0; i < 1024; i++) {
        particles[i].x = 0.0;
        particles[i].y = 0.0;
        particles[i].vx = 1.25;
        particles[i].vy = 0.75;
    }

    // Exhaustive pattern matching
    match (simulate(particles, 100)) {
        ComputeResult::Success(count) => {
            println("Simulation completed successfully.");
            return 0;
        }
        ComputeResult::Overflow => {
            println("Simulation failed: invalid parameters.");
            return 1;
        }
    }
}
```

---

## Reproducible Performance Benchmarks

All benchmarks were compiled with maximum optimization flags and executed on dedicated physical hardware.

- **Host System**: HP OMEN 15
- **CPU**: AMD Ryzen 7 7840HS with Radeon 780M Graphics (8 Physical Cores, 16 Threads)
- **Memory**: 16 GB DDR5 RAM
- **Storage**: NVMe SSD
- **OS**: Microsoft Windows 11 Pro x64 (Build 22631)
- **Compilers**: Tamarin v0.18.5 (LLVM 21.1.5 backend), Clang C++20 (LLVM 21.1.5 `-O3 -march=native`), MSVC 19.44 (`/O2 /arch:AVX2`), Rustc 1.84 (`-C opt-level=3 -C target-cpu=native`).

### Benchmark Results Comparison

| Workload | Metric | Tamarin (v0.18.5) | Clang C++20 | MSVC C++20 | Rust (1.84) | Ratio vs C++ (Clang) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **N-Body Orbit Simulation** (10k steps, AVX2) | Execution Time | **41.2 ms** | 41.5 ms | 43.8 ms | 41.9 ms | **0.99x** (Parity) |
| **SoA Column Scan & Reduction** (10M elements) | Execution Time | **1.84 ms** | 1.85 ms | 2.12 ms | 1.91 ms | **0.99x** (Parity) |
| **Recursive Fibonacci (fib 40)** | Execution Time | **312 ms** | 315 ms | 321 ms | 314 ms | **0.99x** (Parity) |
| **Async Microtask Dispatch** (1,000,000 tasks) | Execution Time | **14.8 ms** | 15.2 ms | 18.6 ms | 38.4 ms | **0.97x** (Faster) |
| **Async Microtask Memory** | Peak RSS | **3.8 MB** | 4.1 MB | 5.2 MB | 28.5 MB | **0.93x** (Leaner) |

### Real-World GUI Application Benchmark (Raylib 5.5 Calculator)

Evaluated on `benchmarks/projects/gui_test/` across a complete, native GUI Calculator (360x560, locked 50 FPS display pacing, 300 active render frames, identical state machine and color theme):

| Metric | Tamarin (v0.18.5) | Clang C++20 | Rust (1.85) | Benchmark Analysis |
| :--- | :--- | :--- | :--- | :--- |
| **Frame Pacing (50 FPS target)** | **20.01 ms** | 20.47 ms | 20.22 ms | Smooth locked 50 FPS across all three (sub-ms variance is compositor jitter) |
| **Total 300-Frame Time** | **6.004 s** | 6.141 s | 6.065 s | Identical display-synchronized completion (~6.0 s) |
| **Stripped Binary Size** | **502.5 KB** | **489.0 KB** | 641.5 KB | Within **2.7%** of C++; **21.7% smaller** than Rust (no unwinding/format bloat) |
| **Release Build Time** | **0.73 s** | **0.28 s** | 1.03 s | C++ fastest (single file); Tamarin compiles **29% faster** than `cargo build --release` |
| **Peak Working Set** | **92.59 MB** | 92.17 MB | 92.23 MB | Identical memory footprint (GPU swapchain context) |
| **Safety Invariants** | **Safe by Default** (0 `unsafe`) | Unsafe Manual | Unsafe FFI Blocks | Tamarin requires **zero `unsafe` blocks**; Rust requires `unsafe` for every Raylib call |

### Multi-Threaded CPU Raytracer Benchmark (SDL3 Streaming Textures)

Evaluated on `benchmarks/projects/raytracer_sdl3/` across a high-throughput, multi-threaded CPU path/ray tracer rendering 1280×720 frames (16 SPP, max bounce depth 8, 124 spheres: ground + 3 hero spheres + 120 procedural spheres on a 15×8 grid) live to an unthrottled **SDL3 streaming texture** (`SDL_LockTexture`, `SDL_PIXELFORMAT_RGBA32`). Symmetrically benchmarked across **Tamarin (v0.18.5+)**, **Clang C++20**, and **Rust (1.84)**:

| Language & Configuration | Frame Time (ms) | Throughput (Mrays/s) | Clean Build (s) | Binary Size (KB) | Peak RSS (MB) | Unsafe Blocks | Bit-Exact Checksum |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Tamarin (SIMD 8-wide)** | **231.01** | **63.83** | **1.13** | **96.5** | **12.61** | **0** | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Clang C++20 (SIMD 8-wide)** | 247.52 | 59.57 | 1.49 | 209.5 | 12.23 | Unchecked | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Rust (SIMD 8-wide)** | 244.61 | 60.28 | 4.67 | 276.5 | 12.00 | 3 | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Tamarin (Standard SoA)** | 1057.42 | 13.94 | 1.13 | 96.5 | 12.59 | **0** | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Clang C++20 (Standard SoA)** | 1051.67 | 14.02 | 1.49 | 209.5 | 12.23 | Unchecked | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Rust (Standard SoA)** | 1023.88 | 14.40 | 4.67 | 276.5 | 12.01 | 3 | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Clang C++20 (AoS Baseline)** | 1033.76 | 14.26 | 1.49 | 209.5 | 12.22 | Unchecked | **MATCH** (`0x2BA9B398A1D21CEF`) |
| **Rust (AoS Baseline)** | 1190.52 | 12.39 | 4.67 | 276.5 | 11.95 | 3 | **MATCH** (`0x2BA9B398A1D21CEF`) |

*Key Technical Architectural Takeaways:*
- **100% Mathematical Determinism:** All 8 configurations produce the identical final checksum `0x2BA9B398A1D21CEF` and bit-identical PNG/BMP screenshots (SHA-256 `eecfed20fd678671eb8f5810cfbaea233ce7f648977a0b6759860f6893a52575`), proving zero mathematical compromises or precision loss.
- **Safety with AVX2 Speed:** Tamarin achieves the fastest frame time (**231.01 ms / 63.83 Mrays/s**) using first-class safe `float8` vector types and race-free affine `parallel for` with **zero `unsafe` blocks**, while Rust requires `unsafe` for AVX2 intrinsics and C FFI (3 blocks total).
- **Compilation Speed & Footprint:** Tamarin compiles clean in **1.13 s** (4.1× faster than `cargo build --release` at 4.67 s) and generates a stripped binary of **96.5 KB** (65% smaller than Rust's 276.5 KB and 54% smaller than C++'s 209.5 KB).

*Methodology Note on Asynchronous Execution & GUI Rendering:*  
Tamarin's async runtime optimizes fine-grained leaf tasks through cooperative stack frames and eager inline task execution, avoiding mandatory heap allocation and thread context switches for non-suspending workloads. Independent parallel loops execute across the lock-free `ThreadTaskPool` work-stealing scheduler. In GUI and graphics workloads, Tamarin C ABI struct lowering passes composite values directly in registers without intermediate wrapper thunks or runtime allocations. For unthrottled CPU compute throughput, see the compute benchmarks and raytracer above where `@layout(scan)` SoA vectorization and safe `float8` SIMD directly differentiate execution speed.

---

## Comprehensive Test Verification Scorecard

The Tamarin test suite validates syntax, type soundness, lifetime verification, affine destructive moves, closure invariants, concurrency safety, C ABI cross-compiler compliance, conditional compilation, multi-target lowering, language server protocols, graphics ecosystems, and package management via the unified cross-platform test harness (`python3 tests/run_all.py`):

| Test Suite | Scope & Category | Tests | Status |
| :--- | :--- | :--- | :--- |
| **`tamarin_tests`** | Unit Tests: Lexer, Parser, Sema, IR, BCE, Codegen, Modules, Stdlib, Troop, @cfg, LSP, MemorySafety (50), ConcurrencySoundness (50), IRLLVMSoundness (50), CompilerPerformance (50) | **603 / 603** | **100% PASS** |
| **`test_rust_soundness.ps1`** | Rust-Level Memory Soundness: Borrowing, Mutation Aliasing, Dangling Ref Rejection | **16 / 16** | **100% PASS** |
| **`test_rust_dark_corners.ps1`** | Adversarial Borrowing: Circular References, Conditional Escaping, Field Splitting | **14 / 14** | **100% PASS** |
| **`test_boss_corners.ps1`** | Stress Gauntlet: Pointer Provenance, Lifetime Extension, Dynamic Scopes | **6 / 6** | **100% PASS** |
| **`test_move_semantics.ps1`** | Affine Semantics: Destructive Move, Drop Flags, Linear Container Soundness | **7 / 7** | **100% PASS** |
| **`test_closures.ps1`** | Stack-Allocated Closures: 16-byte Fat Pointers, Non-Escaping Captures | **5 / 5** | **100% PASS** |
| **`test_async_coroutines.ps1`** | Asynchronous Coroutines: Zero-Allocation Tasks, Suspension, Fibers | **10 / 10** | **100% PASS** |
| **`run_final_suite.ps1`** | Mega Suite: Dynamic/Static Runtime Linking, Clang C ABI Round-Trip, Bounds Traps | **7 / 7** | **100% PASS** |
| **`test_troop.ps1`** | Package Manager: `new`, `add`, `build`, `run`, `test`, `clean`, `troop.lock`, Native DLL Deployment | **10 / 10** | **100% PASS** |
| **`@cfg & Multi-Target CLI`** | Conditional Compilation Directives & Cross-Target Object Lowering (Linux/macOS/Windows) | **5 / 5** | **100% PASS** |
| **`tamarin-bindgen`** | Adversarial C Header Translation: Enums, Structs, Pointers, Callbacks, Keywords | **120 / 120** | **100% PASS** |
| **`tamarin-lsp`** | LSP 3.17 Protocol & Lifecycle: Diagnostics, Hover, Definition, References, Completion | **41 / 41** | **100% PASS** |
| **`Graphics & Native Ecosystem`** | Raylib 5.5 Immediate-Mode 2D/3D & Multi-Backend SDL_GPU Pipeline E2E | **12 / 12** | **100% PASS** |
| **Total Verified** | **Exhaustive Multi-Platform Verification Score (`python3 tests/run_all.py`)** | **856 / 856** | **100% PASS** |

---

## Documentation Index

- [Language Reference](docs/language-reference.md): Grammar, types, `@layout(scan)`, SIMD vectors, enums, closures, and async.
- [Memory Safety & Concurrency](docs/memory-and-concurrency.md): Provenance sets, active borrows, destructive moves, affine loop injectivity, and regions.
- [Compiler Architecture](docs/compiler-architecture.md): Frontend, TIR intermediate representation, optimization passes, LLVM lowering, and Troop internals.
- [C ABI Interoperability](docs/c-abi-interop.md): `@c_layout`, `@export`, header generation (`--emit-header`), and `tamarin-bindgen` foreign function bindings.
- [Graphics & Native Ecosystem](docs/graphics.md): Raylib 5.5, SDL3 platform, modern multi-backend `SDL_GPU`, and Troop project templates.
- [Language Server Protocol (`tamarin-lsp`)](docs/lsp.md): VS Code extension, Neovim, Helix configuration, and real-time editor intelligence.
- [Technical Roadmap](docs/roadmap.md): Version milestones from v0.16.0 (cross-platform Linux/macOS) to v1.0.0 GA.

---

## License

Tamarin is distributed under the [MIT License](LICENSE).
