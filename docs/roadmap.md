# Tamarin Technical Roadmap: Towards Version 1.0 GA

**Current Version:** 0.17.0  
**Target Milestone:** 1.0.0 General Availability  
**Document Classification:** Systems Engineering & Compiler Architecture Plan  

---

## 1. Roadmap Overview and Strategic Objectives

Tamarin has established a verified, zero-compromise foundation:
- **C-Idiomatic Syntax**: Clean, direct readability with no extraneous sigil noise.
- **Zero-Cost Memory Safety**: Compile-time borrow checking (`ProvenanceSet`, `ActiveBorrow`), destructive affine moves (`move(x)`), and stack drop-flag synthesis.
- **Data-Oriented Execution**: First-class `@layout(scan)` Structure-of-Arrays (SoA) layout with automated SIMD vectorization.
- **Data-Race Freedom**: Static affine injectivity analysis on `parallel for` loops.
- **Package Management**: Native `troop` CLI with deterministic SemVer 2.0.0 resolution and lockfile generation.
- **Tier 1 Cross-Platform Portability**: First-class native support across Windows MSVC (`x86_64`), Linux GNU (`x86_64`, `aarch64`), and Apple Silicon macOS Darwin (`aarch64`), with `@cfg(...)` conditional compilation directives and multi-OS GitHub Actions CI.
- **High-Performance Standard Library**: Abseil-style Swiss Table `HashMap<K, V>` with 16-way SIMD group probing, zero-allocation streaming `std.json`, and hardware bit-twiddling intrinsics (`std.bits`).

The path to **Version 1.0 GA** focuses on cross-platform portability, high-performance standard library algorithms, developer tooling (LSP), and self-hosting.

---

## 2. Release Schedule and Milestones

```text
  [ v0.16.0 ] ──────────► Cross-Platform Native Portability, @cfg, Multi-OS CI (Windows, Linux, macOS)
          │
          ▼
  [ v0.17.0 Current ] ──► High-Performance Stdlib: Swiss Table, Zero-Alloc JSON, Hardware std.bits (440/440 Passed)
          │
          ▼
  [ v0.18.0 ] ──────────► Tooling & Ecosystem: tamarin-lsp, tamarin-bindgen, & GUI Packages (Raylib/SDL3)
          │
          ▼
  [ v0.19.0 ] ──────────► Self-Hosting Compiler Prototype (Tamarin Frontend in Tamarin)
          │
          ▼
  [ v1.0.0 GA ] ────────► Stable ABI Freeze, Complete Self-Hosting, Enterprise Package Ecosystem
```

---

### Milestone v0.16.0: Cross-Platform Portability & Tier 1 Multi-OS CI [COMPLETED]
*Completed:* November 2026

- **POSIX Platform Support**:
  - Implemented POSIX virtual memory management in `PageProvider` using `mmap(MAP_PRIVATE | MAP_ANONYMOUS)`, `munmap`, and `mprotect`.
  - Implemented Apple Silicon 16 KB hardware page alignment (`allocGranularity_ = 65536`, `std::max<size_t>(4096, sysconf(_SC_PAGESIZE))`), preventing `mprotect` `EINVAL` aborts.
  - Portable resident working set reporting via `mach_task_basic_info` (macOS), `getrusage` (Linux), and `K32GetProcessMemoryInfo` (Windows).
  - Modern C++20 concurrency (`std::chrono::steady_clock`, `std::thread`, `std::mutex`, `std::atomic`).
- **Conditional Compilation (`@cfg(...)`)**:
  - Implemented `@cfg(target_os = ...)` and `@cfg(target_arch = ...)` with boolean combinators `all(...)`, `any(...)`, `not(...)`.
  - Inactive AST declaration and import pruning prior to Sema analysis and codegen.
- **Continuous Integration Matrix & Unified Test Harness**:
  - Created `.github/workflows/ci.yml` multi-OS matrix (`ubuntu-24.04`, `macos-15`, `windows-2025`) with explicit `-DLLVM_DIR` discovery across all runners.
  - Implemented `tests/run_all.py` (Python 3 standard library) executing all 436 tests across 10 suites with 100% pass rate.

---

### Milestone v0.17.0: Swiss Table HashMap and Zero-Allocation `std.json` [COMPLETED]
*Completed:* November 2026

- **Swiss Table `HashMap<K, V>`**:
  - Implemented Abseil-style Swiss Table flat hash map in `lib/std/collections/hash_map.tmn`.
  - 1-byte control byte metadata table (`EMPTY = 128`, `DELETED = 254`, `FULL = 0..127` fingerprint $H_2$).
  - 16-way group probing with 16-byte tail mirroring, $O(1)$ amortized lookups, triangular probe strides, and automatic table growth at 87.5% load factor ($7/8$ capacity).
  - Flat contiguous key and value arrays with generic monomorphization across primitive, pointer, and compound types.
- **Zero-Allocation Streaming JSON (`std.json`)**:
  - Implemented in `lib/std/json.tmn` with zero heap allocations for parsed string views and keys.
  - Flat contiguous tape AST representation (`JsonNode nodes[]`) eliminating pointer chasing and tree recursion.
  - High-speed streaming parser handling nested objects, arrays, numbers (integers, decimals, scientific exponents), booleans, and nulls.
  - Query APIs: `root()`, `find_field()`, `get_int()`, `get_float()`, `get_bool()`, `array_get()`, `str_equals()`.
- **Bit-Twiddling Intrinsics (`std.bits`)**:
  - Implemented in `lib/std/bits.tmn` and lowered via LLVM intrinsics: `popcnt`, `popcount`, `clz`, `ctz`, `bitrev`, `bitreverse`, `bswap`.
  - Added `__builtin_hash` lowering across 32-bit, 64-bit, float, and slice types (`__tamarin_hash32`, `__tamarin_hash64`, `__tamarin_hash_f32`, `__tamarin_hash_f64`, `__tamarin_hash_bytes`, `__tamarin_hash_view`).

---

### Milestone v0.18.0: Developer Tooling (`tamarin-lsp`), `tamarin-bindgen`, and GUI Ecosystem
*Target Date:* Q3 2027

- **`tamarin-lsp` (Language Server Protocol Engine)**:
  - Implement full LSP JSON-RPC server communicating over standard I/O.
  - Provide real-time semantic diagnostics, error squiggles, and warning reporting.
  - Implement autocompletion, hover documentation, go-to-definition, and find-all-references using persistent AST symbol graphs.
  - Publish official VS Code, Neovim, and Helix editor extensions.
- **`tamarin-bindgen` (Automatic C/C++ Binding Generator)**:
  - Clang LibTooling-based C/C++ header parser that automatically generates idiomatic `@c_layout` and `extern "C"` Tamarin bindings from arbitrary C headers.
  - Automates type translation (`char*` -> `string_view` / raw pointer, C structs -> `@c_layout struct`, enums, and callback function pointers).
- **GUI & Graphics Ecosystem (Option B Architecture)**:
  - Clean separation of concerns: GUI and graphics run via high-performance C ABI interop in package space, keeping the compiler core and standard library lightweight, embeddable, and free of massive C++ graphics engine dependencies.
  - Use `tamarin-bindgen` to generate and maintain official community bindings:
    - **Raylib (`tamarin-raylib`)**: Fast, lightweight 2D/3D hardware-accelerated graphics, windowing, audio, and game loop primitives.
    - **SDL3 (`tamarin-sdl3`)**: Industry-standard cross-platform window management, hardware event handling, and GPU rendering pipelines.
    - **Dear ImGui (`tamarin-imgui`)**: High-performance immediate-mode GUI for developer tooling, debuggers, game UI, and native desktop dashboards.
  - Showcase example applications distributed via `troop add` templates (`tamarin init --template=raylib-game`).

---

### Milestone v0.19.0: Self-Hosting Compiler Prototype
*Target Date:* Q4 2027

- **Frontend Bootstrap**:
  - Rewrite the Tamarin Lexer, Parser, and AST builder in pure Tamarin (`tamarin/src/frontend/`).
  - Compile the new frontend using the C++ reference compiler (`tamarin.exe`).
- **Intermediate Lowering**:
  - Lower the Tamarin-written AST into TIR and pass through the existing optimization pipeline.
- **Verification**:
  - Compile the test suite using both the C++ reference compiler and the bootstrapped compiler to verify identical binary and AST outputs.

---

### Milestone v1.0.0 GA: General Availability & Long-Term Stability
*Target Date:* Q2 2028

- **Full Self-Hosting Compiler**:
  - Complete end-to-end self-hosting: lexer, parser, sema, TIR optimizer, LLVM lowering, and `troop` package manager written 100% in Tamarin.
- **Stable ABI and Language Freeze**:
  - Strict backward compatibility guarantee for the 1.x release series.
  - Complete stability guarantee for `@c_layout`, `@export`, and standard library signatures.
- **Troop Central Package Registry**:
  - Launch high-throughput, cryptographically verified decentralized package registry with checksum validation.
- **Documentation and Book**:
  - Comprehensive systems programming textbook: *Programming in Tamarin: High-Performance Data-Oriented Engineering*.
