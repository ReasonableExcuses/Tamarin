# Tamarin Technical Roadmap: Towards Version 1.0 GA

**Current Version:** 0.18.5  
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
- **Developer Tooling & Interoperability**: Production-grade `tamarin-lsp` (LSP 3.17 server with real-time squiggles, hover, definition, references, outline, completion, signature help) and `tamarin-bindgen` (automated C header binding generator).
- **Graphics & Native Ecosystem**: Raylib 5.5 (`tamarin-raylib`), SDL3 platform, and modern multi-backend `SDL_GPU` (Vulkan, Direct3D 12, Metal, OpenGL) with project scaffolding templates (`troop new --template=...`).

The path to **Version 1.0 GA** focuses on self-hosting and compiler maturation.

---

## 2. Release Schedule and Milestones

```text
  [ v0.16.0 ] ──────────► Cross-Platform Native Portability, @cfg, Multi-OS CI (Windows, Linux, macOS)
          │
          ▼
  [ v0.17.0 ] ──────────► High-Performance Stdlib: Swiss Table, Zero-Alloc JSON, Hardware std.bits (440/440 Passed)
          │
          ▼
  [ v0.18.0 ] ──────────► Tooling & Ecosystem: tamarin-lsp, tamarin-bindgen, VS Code Extension (637/637 Passed)
          │
          ▼
  [ v0.18.5 ] ──────────► Graphics Ecosystem & Compiler Soundness (BUG-40..45, 200 Tests, 856/856 Passed)
          │
          ▼
  [ v0.19.0 Current ] ──► Self-Hosting Compiler Prototype (Tamarin Frontend in Tamarin)
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

### Milestone v0.18.0: Developer Tooling (`tamarin-lsp`), `tamarin-bindgen`, and Editor Packaging [COMPLETED]
*Completed:* November 2026

- **`tamarin-lsp` (Language Server Protocol Engine)**:
  - Implemented full LSP 3.17 JSON-RPC 2.0 server communicating over standard I/O (`--stdio`).
  - **In-Memory VFS & UTF-16 Position Translation**: Real-time multi-document synchronization tracking open buffers, incremental edits, and bidirectional UTF-8/UTF-16 code unit offset mappings.
  - **Fault-Tolerant Parser Error Recovery**: Resynchronization tokens for incomplete statements and blocks, keeping the AST and document outline valid during active keystrokes.
  - **Real-Time Diagnostic Engine**: Instantaneous error and warning squiggles published via `textDocument/publishDiagnostics` with source spans, severity levels, and automated squiggle clearing upon edit fixes.
  - **Semantic Navigation & Inspection**:
    - `textDocument/hover`: Markdown formatted tooltips with full type signatures, parameter docs, and struct memory layout directives (`@c_layout`, `@layout(scan)`).
    - `textDocument/definition`: Instant go-to-definition jump targeting local variables, function parameters, methods, functions, structs, enums, variants, and traits.
    - `textDocument/references`: Find-references locating all lexical usages across the document.
    - `textDocument/documentSymbol`: Hierarchical document outline tree and editor breadcrumbs.
  - **Intelligent Code Completion & Signature Help**:
    - `textDocument/completion`: Complete keyword set, built-in types, in-scope identifiers, and struct dot-completion (`v.`) enumerating fields and methods.
    - `textDocument/signatureHelp`: Interactive parameter help and active parameter tracking during function calls.
- **Official Editor Extensions & Packaging**:
  - VS Code extension in `editors/vscode/` (`tamarin-vscode-0.18.0`) with TextMate grammar, language configuration, and language client bootstrap.
  - Comprehensive editor configuration guides in `docs/lsp.md` for VS Code, Neovim (`nvim-lspconfig`), Helix (`languages.toml`), and Sublime Text (`LSP`).
- **`tamarin-bindgen` (Automatic C/C++ Binding Generator)**:
  - `libclang` C AST cursor visitor generating idiomatic `@c_layout` structs and `extern "C"` declarations directly from arbitrary C headers.
  - Automatically translates C types, pointers (`*T`, `*const T`), fixed-size arrays (`T[N]`), sequential ADT enums (`enum Name { ... }`), bitflag/non-sequential enums (`const int`), preprocessor `#define` constants, and thin C callback function pointers (`*void /* fn(...) -> ret */`).
  - Automated keyword sanitization resolving collisions between C identifiers and Tamarin reserved keywords (`type_`, `trait_`, `fn_`, `match_`).
  - Dual-interface CLI: standalone executable (`tamarin-bindgen`) and integrated compiler subcommand (`tamarin bindgen`).
  - Validated on 120 hard real-world C header patterns with 100% pass rate.
- **GUI & Graphics Ecosystem (Option B Architecture)**:
  - Clean separation of concerns: GUI and graphics run via high-performance C ABI interop in package space, keeping the compiler core and standard library lightweight, embeddable, and free of massive C++ graphics engine dependencies.
  - Use `tamarin-bindgen` to generate and maintain official community bindings:
    - **Raylib (`tamarin-raylib`)**: Fast, lightweight 2D/3D hardware-accelerated graphics, windowing, audio, and game loop primitives.
    - **SDL3 (`tamarin-sdl3`)**: Industry-standard cross-platform window management, hardware event handling, and GPU rendering pipelines.
    - **Dear ImGui (`tamarin-imgui`)**: High-performance immediate-mode GUI for developer tooling, debuggers, game UI, and native desktop dashboards.
  - Showcase example applications distributed via `troop add` templates (`tamarin init --template=raylib-game`).

---

### Milestone v0.18.5: Graphics Ecosystem (`tamarin-raylib` & `tamarin-sdl3`) and Compiler Soundness [COMPLETED]
*Completed:* November 2026

- **Compiler Soundness & Memory Safety Audit (BUG-40 through BUG-45)**:
  - Fixed null pointer dereference in `checkUnary` (`src/sema/type_checker.cpp:2128`) on struct field member access without `this.` (`BUG-40`).
  - Fixed `const int&` parsing and semantic verification as immutable reference (`BUG-41`).
  - Statically closed local stack reference escapes through struct fields (`b.data`) and array indexing (`arr[0]`) (`BUG-42`).
  - Fixed affine move tracking across nested scopes and `movedStack_` variable declaration initialization (`BUG-43`).
  - Enforced loop affine move rules, rejecting moves of outer variables in loops without loop-body reassignment (`BUG-44`).
  - Fixed LLVM codegen return opcode emitting scalar `i8` instead of `ptr` for decayed array/string return values (`BUG-45`).
- **200 New Unit Tests & Multi-Platform Test Gauntlet (856 / 856 Passed)**:
  - Added 50 memory safety unit tests (`tests/codegen/memory_safety_test.cpp`).
  - Added 50 concurrency soundness unit tests (`tests/codegen/concurrency_soundness_test.cpp`).
  - Added 50 IR/LLVM soundness unit tests (`tests/codegen/ir_llvm_soundness_test.cpp`).
  - Added 50 compiler performance unit tests (`tests/codegen/compiler_performance_test.cpp`).
  - Total unit tests in `tamarin_tests` increased to 603; multi-platform gauntlet executes 856 tests with 100% pass rate.
- **Compiler Linker Directives**:
  - Implemented `-l`, `--link`, `-L`, `--lib-dir`, `-Wl,`, and `-Xlinker` CLI flags in Tamarin compiler driver (`src/main.cpp`).
  - Automatically forward link arguments and library paths to Clang and MSVC `link.exe` across Windows, Linux, and macOS.
- **`tamarin-raylib` (Raylib 5.5 Integration)**:
  - C ABI struct-by-value lowering on MSVC x64 and System V AMD64 for composite structs (`Vector2`, `Vector3`, `Color`, `Camera3D`).
  - Zero-glue bindings for `raylib`, `raymath`, and `rlgl` with 100% semantic verification via `tamarin check --lib`.
  - Interactive 2D/3D physics & geometry demos with automated headless CI verification.
- **`tamarin-sdl3` Core (Platform, Windowing, Events, Input)**:
  - Enhanced `tamarin-bindgen` C parser to translate `CXType_Enum` to integer types, handling 96 enums and 1022 constants.
  - High-DPI window lifecycle, event loop dispatch, keyboard/mouse/gamepad input handling.
- **`tamarin-sdl3` GPU (Modern `SDL_GPU`, Vulkan, Direct3D 12, Metal, OpenGL)**:
  - Multi-backend GPU rendering via `SDL3/SDL_gpu.h` targeting **Direct3D 12** on Windows, **Vulkan** on Linux, and **Metal** on macOS.
  - Relaxed C ABI escape contract and borrow tracking for raw pointers; full `null` literal support.
- **Troop Package Manager Templates & Native DLL Deployment**:
  - `troop new --template=raylib-game` and `troop new --template=sdl3-app`.
  - Automatic native `.dll` deployment on Windows: copies required `.dll` files from `lib-dirs` to output `bin/` directory upon build completion.
- **Real-World Application Benchmarking (Raylib 5.5 Calculator)**:
  - Validated native GUI application development in `benchmarks/projects/gui_test/`: Tamarin achieves 50.0 FPS (20.01 ms frame time) and 502 KB binary size with zero `unsafe` blocks, matching C++ and outperforming Rust.
- **Documentation**:
  - Published comprehensive graphics guide in `docs/graphics.md`.

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
