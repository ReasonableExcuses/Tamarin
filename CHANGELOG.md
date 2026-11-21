# Changelog

All notable changes to the TAMARIN compiler will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.18.0] - 2026-11-21

### Added & Hardened — Milestone v0.18.0: Developer Tooling (`tamarin-lsp`), Automatic C Binding Generator (`tamarin-bindgen`), and Official Editor Packaging
- **Language Server Protocol Engine (`tamarin-lsp`)**:
  - Implemented full LSP 3.17 JSON-RPC 2.0 server communicating over standard I/O (`--stdio`).
  - **In-Memory VFS & UTF-16 Position Translation (`lsp/src/vfs.cpp`)**: Multi-document buffer lifecycle tracking (`didOpen`, `didChange`, `didClose`), incremental text synchronization, and bidirectional UTF-8 byte offset to UTF-16 code unit position mappings.
  - **Fault-Tolerant Parser Error Recovery (`src/parser/parser.cpp`)**: Semicolon, brace, and declaration-level resynchronization tokens preventing surrounding AST definitions from discarding when typing incomplete code.
  - **Real-Time Diagnostic Engine (`lsp/src/analyzer.cpp`)**: Sub-50ms diagnostic publisher (`textDocument/publishDiagnostics`) running Lexer $\to$ Parser $\to$ Sema and automatically clearing squiggles when errors are resolved.
  - **Semantic Navigation & Code Intelligence (`lsp/src/navigation.cpp`)**:
    - `textDocument/hover`: Formatted markdown tooltips displaying inferred types, parameter lists, doc comments, and struct memory layout directives (`@c_layout`, `@layout(scan)`).
    - `textDocument/definition`: Instant go-to-definition resolution targeting local variables, function parameters, methods, functions, structs, enums, variants, and traits.
    - `textDocument/references`: Lexical reference locator scanning all token occurrences across open documents.
    - `textDocument/documentSymbol`: Hierarchical outline trees and editor breadcrumbs.
  - **Code Completion & Signature Help (`lsp/src/completion.cpp`)**:
    - `textDocument/completion`: Full keyword set, built-in types, in-scope identifiers, and struct dot-completion (`v.`) enumerating fields and methods.
    - `textDocument/signatureHelp`: Interactive parameter tooltips with active parameter tracking during function calls.
- **Automatic C Binding Generator (`tamarin-bindgen`)**:
  - `libclang` C AST cursor visitor translating arbitrary C headers into idiomatic `@c_layout` structs and `extern "C"` declarations.
  - Automatically translates C types, pointers (`*T`, `*const T`), fixed-size arrays (`T[N]`), sequential ADT enums, bitflag/non-sequential enums, `#define` constants, and function pointer callbacks.
  - Automated identifier sanitization avoiding collisions with Tamarin reserved keywords (`type_`, `trait_`, `fn_`, `match_`).
  - Dual CLI interface: standalone `tamarin-bindgen` executable and integrated compiler subcommand (`tamarin bindgen`).
  - Validated against 120 adversarial real-world C header patterns with 100% pass rate.
- **Official Editor Packaging (`editors/vscode/`, `docs/lsp.md`)**:
  - Published official VS Code extension (`tamarin-vscode-0.18.0`) with TextMate syntax highlighting grammar, language configuration, language client bootstrap, and custom dark/light file icons.
  - Published editor configuration recipes for Neovim (`nvim-lspconfig`), Helix (`languages.toml`), and Sublime Text (`LSP`).
- **Comprehensive Multi-OS Verification**:
  - All 637 tests passing across 12 suites (100% pass rate) on Windows 11 MSVC, Ubuntu 24.04 Linux, and macOS 15 Apple Silicon on GitHub Actions CI.

---

## [0.17.0] - 2026-11-19

### Added & Hardened — Milestone v0.17.0: Swiss Table HashMap, Zero-Allocation `std.json`, and Hardware `std.bits`
- **Swiss Table `HashMap<K, V>` (`lib/std/collections/hash_map.tmn`)**:
  - Implemented Google Abseil / Rust hashbrown style flat hash table.
  - 1-byte control byte metadata table: `EMPTY = 128` (`0x80`), `DELETED = 254` (`0xFE`), `FULL = 0..127` (7-bit $H_2$ fingerprint).
  - 16-way SIMD/group probing with 16-byte tail mirroring (`ctrl[capacity + i] = ctrl[i]`), guaranteeing bounds-safe group loads without wrapping overhead.
  - Triangular probe sequences preventing primary clustering.
  - High load factor ($87.5\%$ or $7/8$ capacity) with automated table doubling upon saturation.
  - Comprehensive API: `init()`, `init_with_capacity(minCap)`, `insert(key, val)`, `get(key)`, `get_or(key, fallback)`, `contains(key)`, `remove(key)`, `size()`, `cap()`, `is_empty()`, `clear()`, `release()`, `is_slot_full(i)`.
- **Zero-Allocation Streaming JSON (`std.json`) (`lib/std/json.tmn`)**:
  - Non-owning `JsonStringView` slices referencing source buffers with zero heap allocations for string tokens and object keys.
  - Flat contiguous tape AST (`JsonNode nodes[]`), eliminating pointer chasing and tree recursion.
  - High-speed streaming DOM parser and query engine supporting nested objects, arrays, numbers (integers, decimals, scientific exponents), booleans, and nulls.
  - Query APIs: `root()`, `find_field(objId, key)`, `get_int(objId, key)`, `get_float(objId, key)`, `get_bool(objId, key)`, `array_get(arrId, idx)`, `str_equals(nodeId, expected)`.
- **Hardware Bit-Twiddling Intrinsics (`std.bits`) (`lib/std/bits.tmn`, `src/ir/`, `src/backend/`)**:
  - Exposes hardware-accelerated population count (`popcnt` / `popcount`), count leading zeros (`clz`), count trailing zeros (`ctz`), bit reversal (`bitrev` / `bitreverse`), and byte swap (`bswap`) lowered directly to LLVM intrinsics.
  - Added universal `__builtin_hash` lowering across 32-bit, 64-bit, float, double, and slice types (`__tamarin_hash32`, `__tamarin_hash64`, `__tamarin_hash_f32`, `__tamarin_hash_f64`, `__tamarin_hash_bytes`, `__tamarin_hash_view`).
- **Compiler Bug Fixes & Soundness Hardening (`src/sema/type_checker.cpp`)**:
  - Fixed array subscript type checking to automatically dereference `Reference` types (`int&`) when used as array indices.
  - Fixed struct field assignment validation to allow repointing non-owning `ArrayView` fields (such as `const char source[];`), while preserving element immutability.
- **Verification Scorecard**:
  - All 440 tests across all 10 unit, soundness, integration, and E2E suites passing (100% pass rate).

---

## [0.16.0] - 2026-11-18

### Added & Hardened — Milestone v0.16.0: Cross-Platform Native Portability (Linux, macOS Darwin, Windows), Conditional Compilation (`@cfg`), and Multi-OS CI
- **Cross-Platform Virtual Memory & Concurrency Runtime (`include/tamarin/runtime/`, `src/runtime/`)**:
  - Abstracted `PageProvider` with `#ifdef _WIN32` (`VirtualAlloc`/`VirtualFree`) and POSIX `#else` (`mmap`/`munmap` using `MAP_PRIVATE | MAP_ANONYMOUS`).
  - Preserved the 64 KB reservation and 4 KB commitment virtual memory model across all platforms.
  - Implemented Apple Silicon 16 KB hardware page alignment: `allocGranularity_ = 65536` (64 KB) with `std::max<size_t>(4096, static_cast<size_t>(sysconf(_SC_PAGESIZE)))` for commit/decommit alignment, preventing `mprotect` `EINVAL` crashes on Apple Silicon.
  - Implemented thread-safe `mmap` reservation tracking via `std::unordered_map<void*, size_t>` guarded by `std::mutex`, ensuring accurate `munmap(base, size)` on POSIX.
  - Portable resident working set reporting via `mach_task_basic_info` (`TASK_BASIC_INFO_64`) on macOS Darwin, `getrusage(RUSAGE_SELF)` on Linux, and `K32GetProcessMemoryInfo` on Windows.
  - Replaced Win32 timers and synchronization primitives with portable C++20 (`std::chrono::steady_clock`, `std::thread`, `std::mutex`, `std::atomic`).
- **Multi-Target LLVM Lowering & Linker Driver (`src/backend/`, `src/driver/`, `troop/src/`)**:
  - Initialized all LLVM targets (`InitializeAllTargetInfos`, `InitializeAllTargets`, `InitializeAllTargetMCs`, `InitializeAllAsmPrinters`), enabling cross-compilation across `x86_64-pc-windows-msvc`, `x86_64-unknown-linux-gnu`, and `aarch64-apple-darwin`.
  - Added `--target <triple>` CLI argument with `TargetTriple::parse` and `llvm::TargetRegistry::lookupTarget`, dynamically configuring module `DataLayout` and target triple.
  - Target-dependent file extension and linker abstractions in `TargetTriple`: executable extensions (`.exe` on Windows vs `""` on POSIX), static libraries (`.lib` vs `lib*.a`), shared libraries (`.dll`, `.so`, `.dylib`), and static library archiving (`lib.exe` / `llvm-ar` vs `ar rcs`).
  - Implemented portable C++ standard library linking in `compileToExecutable` and `compileToSharedLibrary`: `-lstdc++ -pthread -ldl -lm` on Linux and `-lc++ -pthread -lm` on macOS.
  - Portable self-executable location resolution (`getSelfExeDir`) using `GetModuleFileNameW` on Windows, `/proc/self/exe` on Linux, and `_NSGetExecutablePath` on macOS Darwin across `tamarin`, `troop`, and `llvm_codegen`.
  - Fixed POSIX subprocess invocation: removed outer double-quote wrapping (`""cmd" ... ""` required only by Windows `cmd.exe`), preventing `command not found: ""` on Linux and macOS.
- **Conditional Compilation Directives (`@cfg(...)`) (`include/tamarin/ast/`, `src/parser/`, `src/sema/`)**:
  - Added AST representation `CfgExpr` with recursive boolean evaluation (`all(...)`, `any(...)`, `not(...)`) against target platform predicates (`target_os = "windows" | "linux" | "macos" | "darwin"`, `target_arch = "x86_64" | "aarch64"`).
  - Parser integration: `@cfg(...)` supported on top-level imports and declarations (`FunctionDecl`, `StructDecl`, `VarDecl`).
  - Implemented inactive declaration pruning in Parser (`parseProgram`) and Sema (`pruneInactiveDeclarations`), cleanly eliminating inactive platform code before semantic analysis, type-checking, and code generation.
- **Cross-Platform Verification Orchestrator & GitHub Actions CI (`tests/run_all.py`, `.github/workflows/ci.yml`)**:
  - Implemented `tests/run_all.py` using pure Python 3 standard library (`subprocess`, `pathlib`, `tempfile`, `platform`), unifying C++ unit tests, custom PowerShell gauntlets, and new `@cfg` / cross-target tests.
  - Portable bounds trap exit-code detection: checks `proc.returncode != 0` portably across Windows Win32 exception codes and POSIX signals (SIGILL/SIGTRAP).
  - Created `.github/workflows/ci.yml` multi-OS matrix covering `ubuntu-24.04`, `macos-15`, and `windows-2025` with explicit `-DLLVM_DIR` discovery across all three runners.
- **Test Suite Scorecard**:
  - Unit tests: **356 / 356 PASSED (100%)** in `tamarin_tests` (added `@cfg` parser, sema pruning, and cross-target Darwin/Windows binary emission tests).
  - Custom gauntlets: **80 / 80 PASSED (100%)** across Mega Suite (7), Rust Soundness (16), Dark Corners (14), Boss Corners (6), Move Semantics (7), Closures (5), Async Coroutines (10), Troop E2E (10), and @cfg & Cross-Target (5).
  - Grand total: **436 / 436 PASSED (100%)**.

---

## [0.15.0] - 2026-11-18

### Added — Milestone v0.15.0: The Troop Package Manager (`troop` / `tamarin troop`)
- **First-Class Project Manifest (`troop.toml`) (`troop/include/troop/manifest.h`, `troop/src/manifest.cpp`)**:
  - Implemented high-performance zero-dependency TOML parser for package metadata, edition, entry point, build profiles (`[profile.dev]`, `[profile.release]`), and dependency specifications.
  - Supports local path dependencies (`{ path = "../math" }`), registry dependencies, and git dependencies.
- **Semantic Versioning 2.0.0 (`SemVer`) & Requirements (`VersionReq`) (`troop/include/troop/semver.h`, `troop/src/semver.cpp`)**:
  - Implemented SemVer 2.0.0 parser with major, minor, patch, pre-release tags, and build metadata.
  - Implemented flexible version requirement predicates: exact (`=1.2.3`), caret (`^1.2.3`), tilde (`~1.2.3`), wildcard (`*`, `1.*`), and comparison ranges (`>= 1.0.0, < 2.0.0`).
- **Deterministic Dependency Resolution & DAG Ordering (`troop/include/troop/dependency_resolver.h`, `troop/src/dependency_resolver.cpp`)**:
  - Recursively resolves local path and registry dependencies.
  - Detects cyclic dependencies using depth-first active recursion chains (`cyclic dependency detected: A -> B -> A`).
  - Computes topological build order and aggregates all module search paths (`-I`).
- **Cryptographic Lockfile Generation (`troop.lock`) (`troop/include/troop/lockfile.h`, `troop/src/lockfile.cpp`)**:
  - Automatically synthesizes and verifies reproducible content checksums for bit-exact builds across environments.
- **Package Scaffolding & Life-Cycle Management (`troop/src/package_manager.cpp`)**:
  - `troop new <path> [--lib|--bin]`: Scaffolds standard project structure (`troop.toml`, `src/`, `tests/`, `.gitignore`).
  - `troop init [--lib|--bin]`: In-place package initialization.
  - `troop build [--release]`: Resolves DAG and compiles targets to `bin/release/` or `bin/debug/`.
  - `troop run [--release]`: Builds and immediately executes the target executable.
  - `troop test`: Discovers and executes all package tests in `tests/*.tam`.
  - `troop add <dep> [--path <path>]`: Automatically adds dependencies and updates `troop.lock`.
  - `troop clean`: Removes build and binary artifacts (`bin/`, `build/`, `.troop/`).
- **Dual Toolchain Interface (`troop.exe` and `tamarin troop`) (`troop/src/main.cpp`, `src/main.cpp`)**:
  - Shipped as standalone `troop.exe` and integrated as `tamarin troop ...` alias.
- **Module Resolution Multi-Extension Support (`src/driver/module_graph.cpp`)**:
  - Extended static module discovery to search `.tam` and `.tmn` files, as well as `<name>/lib.tam`, `<name>/lib.tmn`, and `<name>/src/lib.tam` for seamless package importing.
- **Test Suite Scorecard**:
  - Unit tests: **351 / 351 PASSED (100%)** in `tamarin_tests.exe`.
  - Custom gauntlets: **82 / 82 PASSED (100%)** across all suites, including new `test_troop.ps1` (10/10). Total: **427 / 427 PASSED (100%)**.

---

## [0.14.0] - 2026-11-18

### Added & Hardened — Milestone v0.14.0: Asynchronous Concurrency, Coroutines & Zero-Allocation Fibers
- **Asynchronous Coroutine Functions (`ast/ast.h`, `parser/`, `sema/`, `ir/ir_builder.cpp`)**:
  - Implemented `async ReturnType name(Params...)` function declarations maintaining classic C syntax readability (no Rust-style `fn name(...)` syntax).
  - Outlined asynchronous worker functions `@__tamarin_async_worker_<name>(ptr %ctx, ptr %result)` while synthesizing a dispatcher function `@<name>(...)` returning a managed `Task<ReturnType>` handle.
  - Automatically packed call parameters into caller-allocated tuples passed via `%ctx`, eliminating dynamic heap allocations for parameter passing.
- **First-Class `Task<T>` Future Type & `await` Expressions (`sema/types.cpp`, `sema/type_checker.cpp`, `ir/ir_builder.cpp`)**:
  - Added `TypeKind::Task` to the semantic type system; represents an 8-byte pointer type (`ptr` in LLVM IR).
  - Implemented `await <task_expr>` unary expressions, unwrapping `Task<T>` to `T` (or `void`).
  - Added compile-time diagnostics rejecting non-task awaiting: `cannot await non-task type '...'`.
- **Zero-Allocation Steady-State Runtime (`ThreadTaskPool`) (`runtime/concurrency.h`, `runtime/concurrency.cpp`)**:
  - Defined `TamarinAsyncTask` featuring inline 128-byte context buffer (`inlineCtx`) and 128-byte result buffer (`inlineResult`), ensuring tasks with context/result $\le 128$ bytes incur zero dynamic heap allocations (`malloc`/`free`).
  - Implemented thread-local caching via `ThreadTaskPool` (capacity 512 tasks per OS thread) for near-instant acquisition and recycling without cross-thread lock contention.
- **Cooperative Work-Stealing Await & Deadlock Freedom (`runtime/concurrency.cpp`)**:
  - Implemented fiber-level cooperative work-stealing during `tamarin_async_task_await`.
  - Fast-path inline execution: If a task has not been picked up by a worker thread when awaited, the awaiting thread claims it via `started.compare_exchange_strong` and runs it inline, eliminating thread queue roundtrips and mutex latency.
  - If already claimed by another thread, the awaiting thread executes pending tasks from the thread pool (`executeOnePendingTask`), preventing worker thread starvation and eliminating deadlocks in recursive task trees (e.g. `fib(n)` or tree reduction).
- **Atomic Reference-Counted Task Lifecycle (`runtime/concurrency.cpp`)**:
  - Implemented a 2-reference lifecycle model (`refCount = 2`: 1 for queue/pool, 1 for task handle).
  - Both the worker thread and `tamarin_async_task_free` atomically decrement the refCount, guaranteeing zero use-after-free and safe recycling regardless of whether the task finishes asynchronously or runs inline on the caller thread.
- **Strict C ABI Boundary Enforcement (`sema/type_checker.cpp`)**:
  - Prohibited `async` functions at `@export` / `extern "C"` boundaries (`"async function '...' cannot have C linkage or be exported directly across C ABI boundary"`).
  - Prohibited `Task<T>` types as parameters, return types, or struct fields across C ABI boundaries (`"task type 'Task<...>' cannot cross C ABI boundaries directly"`).
- **Empirical Multi-Compiler Benchmarking Suite (`benchmarks/async_bench/`)**:
  - Benchmarked Tamarin v0.14 against C++20 Clang 23 (`co_await`), C++20 MSVC 19.44 (`co_await`), and Rust 1.99 (`async/await`) across 66,383 async frames (50,000 task churn + depth-14 recursive tree reduction).
  - Verified 100% exact checksum agreement across all 4 compilers (`-142347304`).
  - Execution speed victory: Tamarin finishes in **0.671 ms**, beating Rust (**0.961 ms** by 1.43x), Clang C++20 (**1.185 ms** by 1.77x), and MSVC C++20 (**1.916 ms** by 2.86x).
  - Binary footprint: Tamarin is **33.5 KB** (**4.7x smaller than Rust** at 157.5 KB, **8.2x smaller than Clang C++20** at 274.0 KB).
  - Compile-time speed: Tamarin compiles in **275.6 ms** (**14.4x faster than Clang C++20** at 3,954.6 ms, **5.7x faster than MSVC C++20** at 1,583.7 ms, **1.4x faster than Rust** at 382.8 ms).
- **Test Suite Scorecard**:
  - Unit tests: **345 / 345 PASSED (100%)** in `tamarin_tests.exe`.
  - Custom gauntlets: **72 / 72 PASSED (100%)** across all suites, including new `test_async_coroutines.ps1` (10/10). Total: **417 / 417 PASSED (100%)**.

---

## [0.13.0] - 2026-11-18

### Added & Hardened — Milestone v0.13.0: Stack-Allocated Borrow-Checked Capturing Closures & C ABI Safety
- **Stack-Allocated Borrow-Checked Capturing Closures (`ast/ast.h`, `parser/`, `sema/`, `ir/ir_builder.cpp`)**:
  - Implemented fat-pointer closure model: function/closure types (`fn(ArgTypes...) -> ReturnType`) and lambdas (`(Type arg) => expr` and `(Type arg) => { ... }`) are represented as a 16-byte aggregate `{ ptr fn_ptr, ptr env_ptr }`.
  - For capturing closures: synthesized an anonymous caller-stack environment struct containing pointers to captured stack slots, allocated via `Alloca` in the caller frame, and passed as a hidden first argument (`ptr %env`) to the lowered lambda function.
  - **Zero Heap Overhead**: Guaranteed zero heap allocation, zero reference-counting metadata, and zero garbage collection overhead.
- **Strict Capture Analysis & Ownership Protection (`sema/type_checker.cpp`)**:
  - Detected free variables from enclosing function frames.
  - Classified captures into immutable/read-only borrows and mutable borrows based on in-body mutation or address-of (`&`) expressions.
  - Prohibited implicitly copying resource-owning types (`hasNonTrivialDestructor() == true`, e.g., `Vec<T>`, `String`, `Result<T, E>`) into closures without explicit `move(...)` semantics.
- **Provenance-Based Escape Prevention (`sema/type_checker.cpp`)**:
  - Attached the union of all captured stack variables' `ProvenanceSet` (local stack storage and shallowest lexical scope depth) to the resulting closure value.
  - Enforced that capturing closures (`env_ptr != null`) cannot escape: rejected `return closure;`, assignment to an outer variable whose scope outlives any captured local, and storing into escaping structs or out-parameters. Stateless closures (`env_ptr == null`) may escape freely.
- **Temporary Active Borrows & Concurrency Race Freedom (`sema/type_checker.cpp`)**:
  - Registered active borrow records on captured variables while a closure binding is live, rejecting conflicting direct mutations or double-mutable aliasing.
  - Added temporary active borrow registration during `checkCall` for inline lambda arguments, disallowing conflicting aliasing in the same call (e.g. `foo(&x, () => { x = 1; })`).
  - Prohibited closures with mutable captures (`env_ptr != null`) from being passed to or executed inside `parallel for` loops, preventing data races at compile time.
- **Uniform Coercion & Thunkless Inlining (`ir/ir_builder.cpp`)**:
  - Uniformly coerced stateless lambdas and named C-style functions into `{ fn_ptr, null }`, enabling higher-order functions to accept both capturing closures, stateless lambdas, and named functions cleanly.
- **Strict C ABI Boundary Enforcement (`sema/type_checker.cpp`, `backend/c_header_generator.cpp`)**:
  - Rejection of `TypeKind::Function` in `@export` functions, `extern "C"` declarations, and `@c_layout` struct fields with a targeted diagnostic: `"closure type 'fn(...) -> ...' has a 16-byte fat-pointer layout and cannot cross C ABI boundaries directly"`.
  - Rejection of unsafe closure-to-pointer casting (`(void*)closure`).
  - Removed unsafe `void*` mapping in `CHeaderGenerator::toCType`, and updated `--emit-header` generation to validate C ABI conformity before emitting C99 headers.
- **Test Suite Expansion**:
  - Expanded test suite to **340/340 unit tests** (`tamarin_tests.exe`) and **395/395 total tests** across all verification suites (including new `test_closures.ps1` covering positive higher-order collections, function coercion, stack escape rejection, mutation rejection, and parallel race rejection).

---

## [0.12.0] - 2026-11-18

### Added & Hardened — Milestone v0.12.0: Rust-Grade Compile-Time Memory & Thread Safety
- **Active Borrow Tracking & Aliasing-XOR-Mutability (`sema/type_checker.cpp`)**:
  - Implemented lexical `ActiveBorrow` tracking in `TypeChecker`.
  - Enforced aliasing-XOR-mutability: a variable can have any number of immutable references (`const T&`) or exactly one mutable reference (`T&`), but never both simultaneously.
  - Rejected call-site double mutable aliasing (`foo(&x, &x)`), mutation while borrowed, and transitive pointer reborrow conflicts.
- **Definite Assignment & Per-Element Initialization (`sema/type_checker.cpp`)**:
  - Expanded `assignedStack_` tracking to include fine-grained per-element array initialization tracking (`arr[i]`).
  - Rejected reads of uninitialized scalar variables and uninitialized array elements.
  - Isolated variable initializations within loops that may execute 0 times from leaking initialized status to outer scopes.
- **Destructive Move Semantics & ADT Drop Glue (`sema/type_checker.cpp`, `ir/ir_builder.cpp`)**:
  - Introduced explicit `move(x)` expression for ownership transfer of resource-owning types (`Vec<T>`, `String`, types with destructors).
  - Maintained `movedStack_` across lexical scopes and control flow branches, rejecting use-after-move and borrow-after-move.
  - Implemented automatic drop glue for tagged union algebraic data types (`Result<T, E>`, `Option<T>`), automatically invoking destructors on active variant payloads when reassigned or exiting scope.
- **Runtime Spatial Bounds Trapping under `-O3` (`ir/ir_builder.cpp`, `backend/llvm_codegen.cpp`)**:
  - Retained deterministic spatial bounds checking and integer overflow indexing trapping even under `-O3` optimizations.
  - Emitted `@__tamarin_bounds_error` with `cold noreturn` attributes, keeping cold failure traps out of hot CPU cache lines while ensuring zero undefined memory corruption.
- **Dual Binary Linkage Modes (`--dynamic` vs `--static`) (`driver/driver.cpp`)**:
  - `--dynamic`: Links against native Windows Universal C Runtime (`ucrtbase.dll`), producing tiny **28.5 KB** executables (5.6x smaller than Rust, 30x smaller total deployment than Clang OpenMP).
  - `--static`: Self-contained executable linked against `libcmt.lib` with zero external runtime DLL dependencies (**181.0 KB**).
- **Adversarial Soundness Gauntlets**:
  - Added multi-tiered custom test suites in `tests/custom/`: `test_rust_soundness.ps1` (16 tests), `test_rust_dark_corners.ps1` (14 tests), `test_boss_corners.ps1` (6 tests), and `test_move_semantics.ps1` (7 tests).

---

## [0.11.0] - 2026-11-17

### Added & Hardened — Milestone v0.11.0: First-Class Parallel Reductions, Atomic Memory Primitives, and Lock-Free Synchronization
- **First-Class Parallel Reduction Syntax & AST (`ast/ast.h`, `lexer/`, `parser/`)**:
  - Added keywords `reduction`, `reduce`, and `atomic` to the lexer and token stream.
  - Added AST reduction structures `enum class ReductionOp` and `struct ReductionClause` supporting operators: `+` (Sum), `*` (Product), `min` (Minimum), `max` (Maximum), `&` (Bitwise AND), `|` (Bitwise OR), and `^` (Bitwise XOR).
  - Implemented parser for `parallel (reduction(op:var), ...) for (...) { ... }` supporting comma-separated multi-variable reductions alongside optional loop grain size specifications.
- **Sema Data-Race Safety Relaxation with Invariant Validation (`sema/type_checker.cpp`)**:
  - Enhanced type checking with active reduction tracking (`activeReductionVars_`).
  - Allowed in-place mutation and compound assignment (`+=`, `-=`, `*=`, etc.) of reduction variables inside `parallel for` loops while strictly preserving data-race rejections for non-reduction outer scalar variables.
  - Validated that reduction variables must be integer or floating-point types, and prevented duplicate reduction registrations on the same variable.
- **Thread-Local Accumulator Outlining with Zero False Sharing (`ir/ir_builder.cpp`)**:
  - Lowered reductions by allocating thread-local accumulators (`allocaRed`) in the worker frame initialized to the algebraic identity value (`0` for `+`/`|`/`^`, `1` for `*`, max/min limits for `min`/`max`).
  - Within worker loops, thread-local accumulators are promoted to SSA registers via LLVM `mem2reg`, providing 100% in-register loop execution with zero memory traffic and zero false sharing across CPU cores.
  - At worker chunk completion (`exitBlock`), performed an atomic reduction into parent frame variables via hardware atomic primitives (`atomicrmw` for `+`, `min`, `max`, `&`, `|`, `^`, and atomic CAS loop for `*`).
- **Hardware Atomic Memory Primitives & Intrinsics (`ir/`, `backend/llvm_codegen.cpp`)**:
  - Added TIR opcodes: `Opcode::AtomicLoad`, `Opcode::AtomicStore`, `Opcode::AtomicRMW`, and `Opcode::AtomicCmpXchg`.
  - Emitted LLVM sequential consistency (`seq_cst`) atomic instructions: `load atomic`, `store atomic`, `atomicrmw [add|sub|min|max|and|or|xor]`, and `cmpxchg`.
  - Exposed builtins: `__builtin_atomic_load`, `__builtin_atomic_store`, `__builtin_atomic_add`, `__builtin_atomic_sub`, and `__builtin_atomic_cmpxchg`.
- **Standard Concurrency Library (`std.sync` in `lib/std/sync.tmn`)**:
  - Implemented `Atomic<T>` with methods: `.init(initial)`, `.load()`, `.store(val)`, `.fetch_add(val)`, `.fetch_sub(val)`, and `.compare_exchange(expected, desired)`.
  - Implemented `SpinLock` using atomic compare-exchange and atomic store.
  - Implemented `Channel<T>`: thread-safe bounded circular queue supporting `init(cap)`, `try_send(val)`, `send(val)`, `recv()`, and `release()`.
- **100M-Element Comparative Parallel Reduction Benchmark (`benchmarks/reduction_bench/`)**:
  - Benchmarked 100,000,000 parallel operations (100 parallel work chunks x 1,000,000 iterations of 8-wide AVX2 vector additions and 4-wide dot products with dual reductions) against C++ Clang OpenMP (static and dynamic scheduling) and idiomatic Rust Scoped Threads (`thread::scope` returning `(local_sum, local_dot)` via JoinHandles with zero atomic contention).
  - **Bit-Exact Checksum Match**: Tamarin, C++, and Rust produce the exact identical checksum: `SUM=-694967296; DOT=500000000`.
  - **Runtime Speed**: Tamarin (**14.32 ms**) closely matches idiomatic Rust Scoped Threads (**13.76 ms**), while outperforming C++ Clang OpenMP `schedule(dynamic)` (**18.28 ms**, **1.28x faster**) and C++ Clang OpenMP `schedule(static)` (**19.51 ms**, **1.36x faster**).
  - **Compilation Speed**: Tamarin compiles in **317.5 ms** (**2.16x faster than Rust** at 687.1 ms).
  - **Binary Footprint**: Tamarin binary is **28.5 KB** (**5.6x smaller than Rust** at 161.0 KB, **5.0x smaller than C++** at 142.5 KB).
- **Test Suite**:
  - 100% test pass rate across **314/314 compiler tests** (5 new tests covering reduction sum, reduction min/max, reduction bitwise OR, reduction multiplication CAS loop, and `std.sync` atomics/locks/channels).

---

## [0.10.0] - 2026-11-17

### Added & Hardened — Milestone v0.10.0: Safe Structured Concurrency & Parallel Execution
- **Zero-Allocation Work-Stealing Runtime Thread Pool (`include/tamarin/runtime/concurrency.h`, `src/runtime/concurrency.cpp`)**:
  - Implemented lock-free work-partitioning runtime thread pool integrated directly into `tamarin_runtime.lib`.
  - Added C ABI runtime primitives: `tamarin_parallel_for`, `tamarin_task_scope_begin`, `tamarin_task_spawn`, `tamarin_task_scope_end`, and `tamarin_concurrency_get_num_threads`.
  - Worker threads sleep on atomic condition variables when idle, waking instantly with near-zero scheduling overhead.
- **Syntax, Grammar & AST Extensions**:
  - Added keywords `parallel`, `task_scope`, and `spawn` to lexer and token stream.
  - Added AST statement structures `ParallelForStmt`, `TaskScopeStmt`, and `SpawnStmt` with grain size and structured block scoping.
  - Implemented grammar parsers for `parallel [(grain)] for (init; cond; inc) { ... }`, `task_scope { ... }`, and `spawn { ... }`.
- **Compile-Time Data-Race Freedom & Scoped Safety (`Sema`)**:
  - Tracked `parallelLoopScopeDepth_` and `taskScopeDepth_` within semantic analysis.
  - Enforced that outer scalar variables cannot be mutated inside a `parallel for` body without lane-local/reduction isolation, preventing data races at compile time.
  - Prohibited orphaned `spawn` statements outside an enclosing `task_scope`, ensuring all spawned asynchronous tasks are guaranteed to complete before exiting the scope (eliminating thread leaks and dangling stack references).
- **TIR Lowering & Outlining Infrastructure (`IRBuilder`)**:
  - Implemented automatic loop body outlining into separate LLVM worker functions (`@__tamarin_pfor_worker_N` and `@__tamarin_task_worker_N`).
  - Implemented zero-heap stack-allocated context packing (`alloca [K x ptr]`) in parent function frame, passing pointers directly to worker functions.
  - Preserved canonical loop structures in worker functions, allowing downstream Domain-Aware TIR passes (RangeAnalysis, BCE) and LLVM vectorization to optimize worker threads automatically.
- **LLVM Code Generation**:
  - Declared and lowered calls to `tamarin_parallel_for`, `tamarin_task_scope_begin`, `tamarin_task_spawn`, and `tamarin_task_scope_end`.
  - Added seamless pointer-preserving store codegen for captured context arrays, enabling bit-perfect interaction with LLVM 15+ opaque pointer models.
- **High-Performance Multi-Threaded Benchmark (`benchmarks/parallel_bench/`)**:
  - Benchmarked 100 Million parallel vector operations (100 parallel work chunks x 1,000,000 iterations of 8-wide AVX2 vector additions and 4-wide dot products) against C++ Clang OpenMP (testing both `schedule(static)` and `schedule(dynamic)`) and Rust Scoped Threads (`std::thread::scope`).
  - **Bit-Exact Numerical Checksum Match**: Tamarin, C++, and Rust produce the exact identical checksum: `SUM=-694967296; DOT=500000000`.
  - **Runtime Speed**: Tamarin (**13.15 ms**) beats C++ Clang OpenMP with `schedule(static)` (**15.55 ms**, **18.2% faster**) and `schedule(dynamic)` (**13.99 ms**, **6.4% faster**), closely matching Rust Scoped Threads (**12.29 ms**).
  - **Compilation Speed**: Tamarin compiles in **306.0 ms** (**1.78x faster than Rust** at 545.2 ms, **1.2x to 1.3x faster than Clang**).
  - **Binary Size & CRT Architecture**: Tamarin binary is **30.0 KB** (**4.8x smaller than C++** at 143.5 KB, **5.3x smaller than Rust** at 159.0 KB). Tamarin achieves this compact footprint by dynamically linking the system C runtime (`ucrtbase.dll`/`kernel32.dll`), whereas Clang (144 KB + `libomp.dll`) and Rust (159 KB) bundle static CRT, personality routines, panic handling, and unwinding machinery.
- **Test Suite**:
  - 100% test pass rate across **309/309 compiler tests** (5 new tests covering parallel array filling, parallel SIMD vector execution, task scope execution, sema outer scalar race rejection, and orphan spawn rejection).

---

## [0.9.0] - 2026-11-17

### Added & Hardened — Milestone v0.9.0: Domain-Aware TIR Compiler Passes & Native SIMD Vectorization
- **TIR Optimization Pass Subsystem (`tamarin/ir/opt/`)**:
  - Implemented modular compiler pass infrastructure with `TIRFunctionPass`, `TIRModulePass`, and `PassManager`.
  - Configured default optimization pipelines for `-O1`, `-O2`, and `-O3` integrated directly into the `Driver::build()` and `Driver::run()` compilation flow.
- **Symbolic Range Analysis Engine (`RangeAnalysis`)**:
  - Implemented comprehensive `Interval` algebra: addition, subtraction, multiplication, interval intersection, and union operations.
  - Automatically identifies canonical loop structures (`for (int i = 0; i < len(arr); i++)` and `i < N <= size`).
  - Unwraps implicit type casts, extracts induction step invariants, and resolves underlying storage origins through pointer provenance.
- **Domain-Aware Bounds-Check Elimination (BCE)**:
  - Formulates proof of bounds safety: converts redundant `Opcode::BoundsCheck` instructions into zero-overhead `Opcode::Nop` instructions at compile time in canonical loops and constant slice indexing.
  - Retains complete memory safety while eliminating all runtime branching penalties in hot loops.
- **Memory Disjointness & Alias Analysis (`AliasAnalysis`)**:
  - Proves non-aliasing across distinct stack allocations, dynamic regions, slabs, and reference parameters.
  - Establishes provenance foundations for aggressive store forwarding, dead store elimination, and vectorization.
- **First-Class Native SIMD Primitives**:
  - Added native SIMD vector keywords and AST types: `float4`, `float8`, `int4`, `int8`, and `int16`.
  - Supported splat initialization (`float4(1.0f)`) and component-wise constructors (`float4(1.0, 2.0, 3.0, 4.0)`).
  - Supported component swizzling (`.x`, `.y`, `.z`, `.w`, `.r`, `.g`, `.b`, `.a`), vector bracket indexing (`v[i]`), and pairwise element arithmetic (`+`, `-`, `*`, `/`).
  - Added hardware-accelerated vector reduction builtins `reduce_add(v)` and `dot_product(v1, v2)`.
- **Native LLVM Vector Codegen**:
  - Lowered SIMD vector types to LLVM native vector types `<4 x float>`, `<8 x float>`, `<4 x i32>`, `<8 x i32>`, `<16 x i32>`.
  - Lowered constructors and reductions to `@llvm.vector.reduce.fadd` and `@llvm.vector.reduce.add` intrinsics, emitting optimal SSE/AVX/AVX2/AVX-512 instructions.
- **High-Performance SIMD Benchmark (`benchmarks/simd_bench/`)**:
  - Benchmarked 100 Million 8-wide AVX2 vector accumulations and 4-wide dot product reductions against C++ Clang and Rust.
  - **Bit-Exact Checksum Match**: Tamarin, C++, and Rust produce the exact identical checksum: `SUM=721420288; DOT=134217728`.
  - **Runtime**: Tamarin (**75.56 ms**) matches C++ Clang AVX2 (**75.03 ms**) and Rust AVX2 (**74.84 ms**).
  - **Compilation**: Tamarin compiles in **307.1 ms** (**1.24x faster than C++ Clang**).
  - **Binary Size**: Tamarin binary is **18.5 KB** (**7.6x smaller than C++ at 141.5 KB**, **7.2x smaller than Rust at 133.0 KB**).
- **Test Suite**:
  - 100% test pass rate across **304/304 compiler tests** (9 new tests covering BCE, Alias Analysis, Float4/8, Int4/8/16 arithmetic and reductions).

---

## [0.8.6] - 2026-11-17

### Added & Hardened — Milestone v0.8.6: Native Fast-Math, 64-Byte Cache Alignment & 3D N-Body Simulation Leadership
- **First-Class Fast-Math Infrastructure (`--fast-math`)**:
  - Added CLI flag `--fast-math` and `-ffast-math` to `tamarin build` and `tamarin run` in `driver.h`, `main.cpp`, and `driver.cpp`.
  - Configured LLVM codegen to emit `fadd fast`, `fsub fast`, `fmul fast`, `fdiv fast` and `call fast float @llvm.sqrt.f32` / `@llvm.sqrt.f64`.
  - Enables aggressive floating-point associative reassociation, reciprocal square root estimation (`vrsqrtps`, `vrsqrt14ps`), and auto-vectorization across parallel reduction accumulators.
- **Hardware Sqrt Intrinsics (`sqrtf` and `sqrt`)**:
  - Implemented built-in hardware intrinsics `__tamarin_sqrtf` and `__tamarin_sqrt` mapping to `@llvm.sqrt.f32` and `@llvm.sqrt.f64`.
  - Exported `sqrtf(float x) -> float` and `sqrt(double x) -> double` in standard library `lib/std/math.tmn`.
- **64-Byte Cache-Line Alignment for Stack Arrays & SoA Bundles**:
  - Emitted `align 64` on all stack `ArrayAlloc` and `SoAArrayAlloc` instructions (`alloca [N x T], align 64` and `alloca { ... }, align 64`).
  - Matches 512-bit AVX-512 register width and CPU cache line alignment, eliminating unaligned vector memory penalties and cache-line crossing splits.
- **Loop Metadata Pipeline (`@unroll`)**:
  - Updated loop metadata emission: `@unroll(1)` emits `llvm.loop.unroll.enable` and `llvm.loop.vectorize.enable`; `@unroll(K)` emits `llvm.loop.unroll.count` with `llvm.loop.vectorize.enable`.
  - Disambiguated loop metadata IDs from branch weight profiling metadata (`!prof !1`).
- **Demanding 3D N-Body Universe Simulation Benchmark Suite (`benchmarks/simulation_bench/`)**:
  - Evaluated **335,544,320 pairwise force evaluations** (**6.7+ GigaFLOPs**) across Tamarin, Clang C, Clang C++, MSVC C, MSVC C++, and Rust (1.99.1).
  - **Normal / Safe Tier (Strict IEEE-754, Checksum `7250061`)**:
    - **Tamarin Normal (**487.55 ms**)** is **1.43x faster than C Clang (698.07 ms)**, **1.47x faster than C++ Clang (718.01 ms)**, **1.58x faster than Rust (772.57 ms)**, and **1.73x faster than MSVC (842.39 ms)**.
  - **Expert / Fast-Math Tier (Apples-to-Apples Vectorization Across All Compilers)**:
    - **Tamarin Expert (**62.77 ms**)** is **1.14x faster than C Clang Expert (71.54 ms)**, **1.13x faster than C++ Clang Expert (70.86 ms)**, **1.26x faster than Rust AVX2 (79.15 ms)**, **1.52x faster than MSVC (95.18 ms)**, and **7.21x faster than Rust unchecked (452.72 ms)**!
  - **Binary Footprint Moat**: Tamarin binaries (**22.0 - 23.5 KB**) are **6.0x - 8.1x smaller than C**, **6.1x - 7.1x smaller than Rust**, and **10.1x - 12.2x smaller than C++**.
  - **Compilation Speed Dominance**: Tamarin compiles in **330 - 350 ms** (**1.45x faster than C Clang**, **3.0x faster than C++ Clang**, **4.3x faster than Rustc**, and **6.0x faster than MSVC**).
- **Test Suite**:
  - Expanded unit test coverage with `CodeGen.BuiltinSqrtAndSqrtfExecution`. All **295 compiler tests passing (100%)**.

---

## [0.8.5] - 2026-11-17

### Added & Hardened — Milestone v0.8.5: Core Standard Library, Memory Primitives & Performance Leadership
- **Production Standard Library Modules (`lib/std/`)**:
  - `std.mem`: Dynamic memory allocation (`alloc<T>`), reallocation (`realloc<T>`), deallocation (`free<T>`), block copying (`copy<T>`), memory move (`move<T>`), setting (`set<T>`), and unchecked slice indexing (`get_unchecked<T>`, `set_unchecked<T>`).
  - `std.collections.vec`: High-performance growable contiguous dynamic vector (`Vec<T>`) with geometric expansion, `push`, `push_unchecked`, `pop`, `reserve`, `get`, `set`, `at`, `size`, `cap`, `is_empty`, `as_slice`, and `release`.
  - `std.string`: Production dynamic UTF-8 string with growth buffer, `append`, `push_char`, `starts_with`, `ends_with`, `equals`, `to_int`, `view`, and `release`.
  - `std.algorithms`: Cache-optimized median-of-three introsort with tail-call elimination, small-array insertion sort, $O(N)$ 8-bit LSD signed Radix Sort (`radix_sort`), `binary_search`, `reverse`, `fill`, and `swap`.
- **Dynamic Heap Memory Primitives in Compiler Core**:
  - Added IR opcodes, semantic analysis builtins, and LLVM code generation lowering for `MemAlloc`, `MemRealloc`, `MemFree`, `MemCopy`, `MemMove`, and `MemSet` (`@realloc`, `@llvm.memcpy.p0.p0.i64`, `@llvm.memmove.p0.p0.i64`, `@llvm.memset.p0.i64`).
  - `__builtin_alloc<T>` returns bounded, zero-overhead `ArrayView<T>` (`T[]`) with compiler bounds-checking and `len(s)` support.
- **Zero-Cost Custom Struct Indexing Operator `[]`**:
  - Transparent syntactic lowering: `v[i]` lowers to `v.get(i)` and `v[i] = val` lowers to `v.set(i, val)`.
  - Inlined by LLVM at `-O2`/`-O3` directly to raw pointer dereferences matching C raw arrays.
- **Cold Bounds Branch Metadata & Aggressive LLVM Vectorization**:
  - Declared `@__tamarin_bounds_error()` as `cold noreturn nounwind`.
  - Attached branch weight metadata `!prof !1` (`!1 = !{!"branch_weights", i32 1, i32 1048576}`) to all bounds checking branches, moving failure blocks out of hot loop cache lines.
  - Configured aggressive loop vectorization and unrolling flags in Clang invocation: `-mllvm -inline-threshold=2500 -mllvm -unroll-threshold=800 -mllvm -vectorize-loops=true -mllvm -vectorize-slp=true`.
- **Unchecked Memory Builtins**:
  - Added `__builtin_get_unchecked<T>(slice, idx)` and `__builtin_set_unchecked<T>(slice, idx, val)` emitting `ArrayLoad` and `ArrayStore` without bounds checking branches for algorithmic and collection kernels.
- **Benchmark Leadership vs C, C++, and Rust**:
  - **Dynamic Vector (10M integers)**: Tamarin Expert (**8.79 ms**) beats Rust Expert (**11.18 ms by 1.27x**) and matches C Expert (**8.75 ms**).
  - **In-Place Sort (2M integers)**: Tamarin RadixSort (**11.13 ms**) beats Rust Expert (**23.30 ms by 2.1x**), beats C++ `std::sort` (**107.47 ms by 9.6x**), and beats C `qsort` (**178.82 ms by 16.1x**).
  - **String Operations (500k appends + 1M conversions)**: Tamarin Expert (**0.84 ms**) matches C Expert (**0.62 ms**) and Rust Expert (**0.71 ms**).
- **Test Suite**:
  - Added 4 integration test suites: `Stdlib.ImportStdMem`, `Stdlib.ImportStdCollectionsVec`, `Stdlib.ImportStdString`, `Stdlib.ImportStdAlgorithms`.
  - Total test count expanded to **294 compiler tests** (100% pass rate).

---

## [0.8.0] - 2026-11-16

### Added & Hardened — Milestone v0.8: Parametric Polymorphism, Zero-Cost Generics & Trait System
- **Parametric Polymorphism & Static Monomorphization**:
  - Added generic functions (`fn foo<T>(T val)`), generic structs (`struct Vec<T>`), and generic enums with type parameters.
  - Multi-parameter type deduction (`Pair<A, B>`) inferring concrete types from call arguments and struct fields without explicit type arguments.
  - Specialized AST monomorphization engine in `src/sema/monomorphizer.cpp` generating specialized clones with mangled signatures (`foo__int`, `Vec__int`).
- **Trait System & Static Dispatch**:
  - Added `trait` contracts (`trait Summary { ... }`) and explicit implementation blocks (`impl Summary for Type { ... }`).
  - Added compile-time trait bound validation (`where T: Trait` or `fn test<T: Trait>(T val)`).
  - Static trait method dispatch with zero virtual dispatch or dynamic table overhead.
- **Compiler Architecture Hardening**:
  - Scoped symbol table isolation in `TypeChecker::instantiateGenericFunction` preventing cross-specialization pollution.
  - Fixed array member LValue GEP lowering and `ArrayView` conversion in generic struct methods.
  - Cross-module COMDAT deduplication using LLVM `linkonce_odr` linkage.
- **Test Suite**:
  - Added 7 end-to-end generic execution tests in `tests/codegen/codegen_test.cpp`: `CodeGen.MonomorphizedGenericFunctionExecution`, `CodeGen.MonomorphizedGenericStructExecution`, `CodeGen.TraitStaticDispatchExecution`, `CodeGen.MultiTypeParamAndSpecialization`, `CodeGen.GenericVectorCollection`, `CodeGen.TraitStaticDispatchWithMultipleImplementations`, `CodeGen.GenericStack`.
  - Total test count expanded to **286 compiler tests** (100% pass rate).

---

## [0.7.0] - 2026-11-15

### Added & Hardened — Milestone v0.7: Algebraic Data Types, Pattern Matching & Safe Error Handling
- **Algebraic Data Types (ADTs / Tagged Unions)**:
  - Added `enum` declarations with optional type parameters (`enum Option<T>`, `enum Result<T, E>`) and typed payload variants (e.g., `Color::Rgb(u8, u8, u8)`, `Shape::Circle(f64)`, `Message::Move(i32, i32)`) to the Tamarin language specification.
  - Implemented token additions `match` and `=>` (`TokenKind::Match`, `TokenKind::FatArrow`) in `lexer.h`, `lexer.cpp`, and `token.cpp`.
  - Added AST representations in `ast.h` and `ast_printer.cpp`: `EnumDecl`, `EnumVariant`, `MatchExpr`, `MatchStmt`, `MatchCase`, `MatchPattern`, `MatchPatternKind` (`Wildcard`, `Literal`, `Variant`, `Binding`), and `TryExpr` (`?`).
  - Recursive descent parser in `parser.cpp` parses generic enum declarations, type arguments (`<T>`, `<T, E>`), nested template brackets (`>>` split to two `>` tokens), and flexible match syntax (`match expr` and `match (expr)`).
- **Compile-Time Exhaustive Pattern Matching**:
  - Implemented compile-time exhaustiveness checking in `type_checker.cpp`: all enum variants must be covered or a catch-all wildcard (`_`) pattern must be supplied.
  - Added variable binding extraction in pattern arms, exposing payload values safely as immutable locals in the arm's block or expression.
  - Semantic analysis validates type compatibility between matched expressions and pattern variants, reporting actionable compile errors on missing variants or mismatched payloads.
- **Built-in `Option<T>`, `Result<T, E>`, and Safe `?` Operator**:
  - Integrated built-in generic `Option<T>` (`Some(T)`, `None`) and `Result<T, E>` (`Ok(T)`, `Err(E)`) with automatic contextual type resolution.
  - Added the postfix `?` try-operator on expressions returning `Result<T, E>` or `Option<T>`. Lowers to safe early-return propagation of `Err(e)` or `None` if the discriminant indicates failure, and extracts the payload value into an SSA register on success.
- **Zero-Cost Niche Optimization for Handles (`Option<Handle<T>>`)**:
  - Implemented niche optimization in semantic type sizing and LLVM code generation: `Option<Handle<T>>` has zero discriminant overhead.
  - Maps to an 8-byte native `i64` where `0` represents `None` and any non-zero value represents `Some(Handle<T>)`, matching raw pointer semantics with zero memory overhead and instant register comparisons.
- **TIR & LLVM Backend Lowering**:
  - Added IR opcodes `TagGet`, `TagSet`, `VariantPayloadPtr`, and `Switch` in `ir.h` and lowering in `ir_builder.cpp`.
  - Tagged unions lower to LLVM packed struct types `%enum.<Name> = type { i32, [MaxPayload x i8] }` (or `{ i32 }` for pure unit enums).
  - LLVM backend emits native multi-way `switch i32 %tag, label %default [ ... ]` dispatch and zero-overhead opaque pointer offsets (`getelementptr inbounds i8, ptr %slot, i64 4`).
- **Comprehensive Verification & Test Suite**:
  - Added parser unit test `TAMARIN_TEST(Parser, ParseEnumAndPatternMatch)`.
  - Added semantic analyzer test `TAMARIN_TEST(Sema, EnumAndPatternMatchExhaustiveness)`.
  - Added 5 end-to-end execution tests in `tests/codegen/codegen_test.cpp`:
    - `CodeGen.PatternMatchingEnumUnitVariants`
    - `CodeGen.PatternMatchingAlgebraicDataTypes`
    - `CodeGen.PatternMatchingOptionAndResult`
    - `CodeGen.SafeErrorHandlingTryOperator`
    - `CodeGen.NicheOptimizedOptionHandle`
  - Total compiler test suite increased to **274 tests** with a **100% pass rate** (274/274 passed).

---

## [0.6.0] - 2026-11-15

### Added & Hardened — Milestone v0.6: Recyclable Memory: Slabs, Pools & Generational Handle Safety
- **Core Generational Slab Storage (`Slab<T>` & `Handle<T>`)**:
  - Added `Handle<T>` and `Slab<T>` / `slab(T, capacity)` to the Tamarin language specification, lexer (`lexer.cpp`, `token.cpp`), AST (`ast.h`, `ast_printer.cpp`), parser (`parser.cpp`), semantic type checker (`types.h`, `types.cpp`, `type_checker.cpp`), intermediate representation (`ir.h`, `ir_builder.cpp`), and LLVM code generator (`llvm_codegen.cpp`).
  - Implemented 64-bit compact generational handles (`{ u32 slot, u32 generation }`), packing slot index and generation counter into a single native register without pointer payload or fat-pointer bloat.
  - Implemented **Generational Parity Invariant**:
    - Free slots hold **EVEN** generations ($0, 2, 4, ...$), starting at 0.
    - Occupied slots hold **ODD** generations ($1, 3, 5, ...$).
    - Allocation increments the slot generation from even to odd, issuing the handle.
    - Deallocation (`remove`) increments the slot generation from odd to even, invalidating any issued handle immediately.
    - Handle validation is a single native comparison: `cmp [slab.gen + slot*4], handle.gen`. No free slot can ever match any issued handle, and stale handles mismatch immediately in $O(1)$ time (~5.97 ns).
- **Generational Slot Retirement & ABA Protection**:
  - Implemented generational retirement at generation overflow threshold $0xFFFFFFFE$. Slots reaching this threshold are marked retired ($0xFFFFFFFF$) and permanently excluded from the free list, mathematically eliminating ABA aliasing over the entire lifetime of the process.
- **Dynamic Growth & Page Provider Integration**:
  - Slabs utilize primary chunk storage backed by the OS virtual memory page provider (`MemoryPageProvider`).
  - When initial capacity is exhausted, slabs automatically allocate secondary chunks (`TamarinSlabChunk`) using the thread-local growth cache (`ThreadChunkCache`) and page commit path, ensuring unbounded capacity growth while preserving valid handle dereferencing across all chunk boundaries.
- **Recyclable Storage RAII & Destructor Lifecycle**:
  - Non-trivial destructors for structs/classes in recyclable slabs are safely tracked and executed in reverse LIFO order upon explicit slot removal (`slab.remove()`), full slab clearing (`slab.clear()`), or slab scope exit (`slab` destructor).
  - Destructors are never invoked on recycled or inactive slots.
- **Milestone v0.6 Test Coverage & Empirical Validation**:
  - Added comprehensive parser tests (`Parser.ParseHandleAndSlabSyntax`) and semantic analysis tests (`Sema.SlabValidOperationsAndMethods`, `Sema.SlabTypeMismatchRejections`).
  - Added end-to-end execution tests (`CodeGen.SlabBasicInsertGetAndRemove`, `CodeGen.SlabSlotRecyclingAndGenerationalParity`, `CodeGen.SlabDoubleFreeRejection`, `CodeGen.SlabDynamicGrowthAndPageProvider`, `CodeGen.SlabNonTrivialDestructorLifecycle`).
  - All **266/266 compiler tests passing** (100% pass rate).
- **Milestone v0.6 Performance Benchmarks**:
  - Authored `benchmarks/tamarin/11a_slab_churn.tmn` measuring high-frequency allocation, recycling, and lookup of 50,000 entities in recyclable slab storage (executing 200 rounds of 1024 entities in 3.72 ms, under 10 ns per entity lifecycle).
  - Authored `benchmarks/tamarin/11b_handle_deref.tmn` measuring 2,048,000 generational handle dereferences and mutations (12.23 ms, ~5.97 ns per validation and dereference, achieving zero-cost safety $\le 1.10\times$ raw pointer speed).

---

## [0.5.9] - 2026-11-10

### Added & Hardened
- **Expert Optimization Annotations & Compiler Features**:
  - Added `@inline` and `@always_inline` function attributes to AST (`FunctionDecl`), recursive descent parser (`parser.cpp`), TIR (`IRFunction`), and LLVM backend (`llvm::Attribute::AlwaysInline`). Guarantees zero function call overhead on hot computational kernels.
  - Added `@unroll` and `@unroll(N)` loop annotations to AST (`WhileStmt`, `ForStmt`), parser (`parser.cpp`), TIR (`Instruction`), and LLVM backend (`!llvm.loop` metadata with `llvm.loop.unroll.count` and `llvm.loop.unroll.enable`). Directs LLVM's loop unroller to perform precise compile-time loop unrolling.
  - Added native bit-manipulation builtins `ctz`, `clz`, `popcount` (and `__builtin_*` aliases) in semantic type checking (`type_checker.cpp`), TIR lowering (`ir_builder.cpp`), and LLVM intrinsic calls (`@llvm.cttz`, `@llvm.ctlz`, `@llvm.ctpop`), compiling down to native `tzcnt`/`bsf`, `lzcnt`/`bsr`, and `popcnt` instructions.
- **Fast Region Zero-Initialization**:
  - Lowered `__tamarin_region_init` to emit a single 120-byte `llvm.memset.p0.i64` zeroing the region header followed by a single store for `capacity`. Replaced 14 individual scalar pointer stores with AVX2/SSE vectorized zeroing, reducing empty region creation/destruction latency (`10d_empty_region`) to ~0.000 ms.
- **Geometric Range Commitment (Page Doubling) for Large Allocations**:
  - In `page_commit_path`, implemented geometric commit doubling (`target_com = min(cur_res, max(need_page_round, cur_com * 2))`), eliminating up to 97% of OS `VirtualAlloc(MEM_COMMIT)` kernel transitions during large-region allocation (`10b_large_single`), cutting latency from 0.25 ms to **0.088 ms**.
- **Thread-Local Sidecar Chunk Cache (`ThreadSidecarCache`)**:
  - Implemented 8-slot thread-local cache `ThreadSidecarCache` in `src/runtime/page_provider.cpp` and `include/tamarin/runtime/page_provider.h` with thread-exit deallocation cleanup.
  - Lowered `__tamarin_region_reserve_dtor` to acquire sidecar chunks from `ThreadSidecarCache` and `__tamarin_region_destroy` to return them to `ThreadSidecarCache`, eliminating heap allocations on sparse destructor workloads (`10e_sparse_dtors`, latency **0.038 ms**, **5.7x faster** than Rust Expert).
- **Virtual Dispatch Vtable Invariant Metadata**:
  - Attached `!invariant.load !0` metadata to vtable pointer loads and function pointer loads in `CallVirtual`, informing LLVM that method addresses remain invariant across call sites and enabling loop hoisting.
- **High-Performance Inliner Threshold & Native Target CPU Support**:
  - Passed `-mllvm -inline-threshold=1000` to LLVM backend at `-O2` and `-O3`.
  - Added function attributes `mustprogress nounwind` to emitted functions, improving LLVM's alias analysis and dead-loop elimination.
- **Complete Suite of 17 Expert Benchmarks**:
  - Authored and verified 17 expert benchmarks in `benchmarks/tamarin/` (`01_integer_expert.tmn` through `10f_reuse_expert.tmn`) with 100% exact bit-for-bit checksum matches and zero contract violations.
- **Dual Performance Standards Proven & Expert Benchmark Optimization**:
  - Investigated and resolved manual loop unrolling bottlenecks in expert benchmarks (`04_structures_aos_expert.tmn`, `04_structures_soa_expert.tmn`, `08_memory_ops_expert.tmn`, `03_arrays_expert.tmn`, `10c_chunk_growth_expert.tmn`, `05_function_calls_expert.tmn`, `02_float_expert.tmn`).
  - Restored canonical induction variable loops to re-enable 100% LLVM Scalar Evolution (SCEV) bounds-check elimination and 256/512-bit AVX2/AVX-512 Loop Vectorization.
  - Achieved dramatic speedups: `04_structures_aos_expert` dropped from 3.75 ms to **0.536 ms** (7.0x faster), `08_memory_ops_expert` dropped from 1.58 ms to **0.852 ms** (1.85x faster), beating Clang/MSVC/Rust Expert competitors.
  - Standard 1 (Normal TAMARIN): Ordinary, readable TAMARIN code performs at least as well as expert C and Rust across computational and memory workloads without manual optimization tricks.
  - Standard 2 (Expert TAMARIN): Advanced TAMARIN code outperforms equivalent expert C and Rust implementations across the benchmark suite (Geometric Mean 0.94x vs Normal Tamarin, 0.84x vs Rust Expert).
- **Verification & Test Suite**:
  - 258/258 compiler tests passing in Debug.
  - 258/258 compiler tests passing in Release.
  - 18/18 runner validation tests passing.
  - 9/9 Rust soundness tests passing.
  - Full 17-workload benchmark campaign executed with 30 repetitions and 5 warmups (100 reps for startup) across all 13 configurations.

---

## [0.5.8] - 2026-11-10

### Added & Hardened
- **Large-Region & Chunk-Growth Performance Overhaul**:
  - Implemented `ThreadChunkCache` in `src/runtime/page_provider.cpp` and `include/tamarin/runtime/page_provider.h` with 8 thread-local reusable slots, best-fit selection, and an aggregate 4 MB memory cap.
  - Wired LLVM lowering in `__tamarin_region_alloc` and `chunk_growth_path` to acquire cached chunks before invoking OS `VirtualAlloc(MEM_RESERVE)`.
  - Reused committed page ranges without redundant syscalls; commits only missing byte ranges when needed.
  - Lowered `__tamarin_region_destroy` to route primary chunks $> 64$ KB and all chained growth chunks to `__tamarin_runtime_chunk_cache_put`.
  - Achieved dramatic speedups on large-region workloads:
    - `10_region_alloc`: **3.35 ms** vs 22.37 ms in v0.5.7 (**6.7x speedup**, beating C Clang Expert at 3.98 ms and Rust Expert at 3.58 ms).
    - `10b_large_single`: **0.25 ms** vs 0.78 ms in v0.5.7 (**3.1x speedup**).
    - `10c_chunk_growth`: **0.1765 ms** vs 0.67 ms in v0.5.7 (**3.8x speedup**, beating C Clang Expert at 0.2036 ms and Rust Expert at 0.1808 ms).
- **Rust Expert Arena Soundness & Allocation Overhead Fix**:
  - Derived `Clone, Copy` on `Chunk`.
  - Inlined `first_chunk: Chunk` directly on the stack inside `PooledArenaState`, eliminating the per-instance heap `Vec` allocation (`vec![first_chunk]`).
  - Allocated secondary chunks (`extra_chunks: Option<Vec<Chunk>>`) and destructor callbacks (`dtors: Option<Vec<Box<dyn FnOnce()>>>`) lazily on demand.
  - Built a 4-slot `PooledChunkStorage` with best-fit matching and registered thread-exit `Drop` cleanup to deallocate all cached chunks on thread termination.
  - Added comprehensive unit tests in `benchmarks/rust/tests/soundness_tests.rs` (all 9 tests pass).
- **Repaired Expert Benchmarks & Methodology**:
  - `10d_empty_region`: Instantiated and destroyed genuine `ReusableArena` and `PooledArena` instances across all 50,000 iterations.
  - `10e_sparse_dtors`: Destructors perform observable volatile negations on object IDs across Tamarin, C, and Rust, verifying exact bit-for-bit checksum match `7998000`.
  - `04_structures_aos_expert`: Fixed the unrolled loop regression in C and bounds checking overhead in Rust using clean contiguous pointer / slice iteration, eliminating the 7x regression (~0.51 ms).
  - `08_memory_ops`: Added hardware memory barriers (`__asm__ __volatile__` / `_ReadWriteBarrier` / `black_box`) to prevent dead-store elimination between passes.
  - Automated saving of 119 native assembly files (`.s` / `.asm`) across all workloads and expert configurations in `benchmarks/assembly/`.
- **Target CPU Configuration Flag**:
  - Added `--target-cpu <cpu>` CLI option to the Tamarin compiler driver (`driver.h`, `driver.cpp`, `main.cpp`).
  - Added `TAMARIN_TARGET_CPU` environment variable support.
  - Defaults to generic x86-64 when unspecified, preventing silent host-specific executable emission while preserving explicit `-march=native` optimizations.
- **Verification & Test Suite**:
  - 258/258 compiler tests passing in Debug.
  - 258/258 compiler tests passing in Release.
  - 18/18 runner validation tests passing.
  - 9/9 Rust soundness tests passing.
  - Complete 17-workload benchmark campaign executed with 30 repetitions and 5 warmups (100 reps for startup) across all 12 configurations with exact checksum validation.

---

## [0.5.7] - 2026-11-10

### Added & Hardened
- **Restored Complete 17-Workload Benchmark Suite**:
  - Restored and executed the complete suite of 17 workloads across 12 compiler configurations: `01_integer`, `02_float`, `03_arrays`, `04_structures_aos`, `04_structures_soa`, `05_function_calls`, `06_branching`, `07_virtual_dispatch`, `08_memory_ops`, `09_startup`, `10_region_alloc`, `10a_high_churn`, `10b_large_single`, `10c_chunk_growth`, `10d_empty_region`, `10e_sparse_dtors`, and `10f_reuse`.
- **Dual Competitor Variants for Every Workload (Idiomatic vs Expert)**:
  - Provided idiomatic and expert implementations in both C and Rust for every workload:
    - `01_integer_expert`: CTZ-accelerated Collatz (`__builtin_ctzll` / `trailing_zeros()`), 4-way unrolled sieve.
    - `02_float_expert`: 4-way unrolled Mandelbrot with strict IEEE-754 precision, AVX2 vectorized matrix multiplication.
    - `03_arrays_expert`: 8-way unrolled scan and array transforms with pipelined LCG updates.
    - `04_structures_aos_expert`: 4-way unrolled particle dynamics simulation.
    - `04_structures_soa_expert`: AVX2 256-bit SIMD vectorized particle integration (`_mm256_fmadd_ps` / `_mm256_add_ps`).
    - `05_function_calls_expert`: Tail-call recursion elimination for Fibonacci and pipelined accumulator transforms.
    - `06_branching_expert`: Branchless conditional evaluation and branchless two's-complement negation.
    - `07_virtual_dispatch_expert`: Polymorphic inline caching (PIC) with devirtualization fast-paths.
    - `08_memory_ops_expert`: Bitwise ASCII XOR (`^ 0x20`) case conversion and unrolled substring scanning.
    - `09_startup_expert`: Minimal zero-overhead process entry point.
- **Fair Reusable Arena Competitor Implementations**:
  - Authored `benchmarks/c/reusable_arena.h` and `benchmarks/rust/src/lib.rs` `PooledArena` providing genuine reusable thread-local arena pools with zero steady-state heap allocations during high-churn workloads (`10_region_alloc_expert`, `10a_high_churn_expert`, etc.).
  - Retained idiomatic `malloc`/`free` and `Box::new`/`drop` as a separate, honest baseline.
- **Compiler Bottleneck 1 — Bounds Checking Lowering & SCEV Elimination**:
  - Canonicalized array bounds checking in `src/backend/llvm_codegen.cpp` to single unsigned comparisons (`icmp uge i32/i64 %idx, len`).
  - In fixed-size loops with 32-bit indices, emitted `icmp uge i32 %idx, fixedLen`, enabling LLVM's `IndVarSimplify` pass to prove bounds checks unreachable and eliminate them in inner loops.
  - Achieved a **5.3x speedup** on `01_integer` (latency dropped from 20.31 ms to 3.81 ms), beating Clang C -O3 (4.08 ms) and Rust Release (4.24 ms).
- **Compiler Bottleneck 2 — Target CPU Architecture Parity (`-march=native`)**:
  - Integrated host CPU feature detection into `optimizeLLVMIR`, `compileToAssembly`, and `compileToExecutable` using `-march=native` on `-O2` and `-O3` (with `TAMARIN_ARCH` override).
  - Ensured fair comparison with Clang and Rust target CPU flags, unlocking AVX, AVX2, and FMA autovectorization.
- **Compiler Bottleneck 3 — Region Telemetry Fast-Path Elimination**:
  - Guarded diagnostic counters (`rf_pad`, `rf_usd`, `rf_ac`) in `__tamarin_region_alloc`'s bump-pointer fast path with `@__tamarin_flag_memory_stats`.
  - When `--memory-stats` is off, LLVM eliminates all diagnostic loads/stores, reducing the bump allocation fast path to 2 instructions (`store` and `getelementptr`).
- **Peak Resident Working-Set Instrumentation**:
  - Instrumented `benchmarks/runner.py` with Windows PSAPI `GetProcessMemoryInfo` measuring exact peak resident working-set RAM (`peak_ws_kb`) across all workloads and configurations.
- **Geometric Mean & Statistical Automation**:
  - Integrated automated geometric-mean ratio calculations against both idiomatic and expert implementations with 1,000-resample bootstrap 95% confidence intervals and fail-fast checksum validation.

---

## [0.5.6] - 2026-11-10

### Added & Hardened
- **Fixed Reservation-Cache Metadata & Partial Commitment**:
  - Corrected `__tamarin_runtime_rcache_acquire` to return both reservation capacity and previously committed bytes (`out_reserved`, `out_committed`).
  - Rewrote LLVM IR region allocation lowering in `lazy_first_alloc` to check `already_com >= needed_commit`.
  - When acquired block is already sufficiently committed, unconditionally skips `@__tamarin_page_commit` OS kernel calls.
  - When acquired block requires more committed memory, commits only the missing byte range `[already_com, needed_commit)`.
- **Fast Small-Region Pool**:
  - Implemented a bounded, thread-local pool of reusable, already-committed small-region blocks (`MAX_SLOTS = 4`).
  - Creating and destroying small regions executes via fast O(1) pointer pop/push operations with zero `VirtualAlloc` or `VirtualFree` calls in steady state.
  - Large regions (> `allocationGranularity`) and subsequent chunk-growth blocks strictly bypass the small-region pool and release directly to the OS.
- **Strict Aggregate Committed-Memory Cap**:
  - Applied the aggregate committed-memory cap (`memory_cap`, default 128 KB, configurable via `TAMARIN_RCACHE_MEMCAP`) across the entire thread-local pool, rather than per slot.
  - If retaining a block would cause aggregate committed pool memory to exceed `memory_cap`, the block's physical pages are decommitted to 0 before caching.
- **Zero-Initialization Guarantee on Reused Blocks**:
  - Maintained airtight zero-initialization invariant: on region exit, `__tamarin_runtime_rcache_put` zeroes all previously used bytes via userland `memset`.
  - Blocks residing in the pool are always 100% clean, zeroed memory.
- **OS Virtual Memory Operation Telemetry & Proof Counters**:
  - Added thread-safe atomic counters for actual OS virtual memory operations: `reserve_count`, `commit_count`, `decommit_count`, `release_count`.
  - Exported C API query and reset functions: `__tamarin_runtime_get_reserve_count`, `__tamarin_runtime_get_commit_count`, `__tamarin_runtime_get_decommit_count`, `__tamarin_runtime_get_release_count`, `__tamarin_runtime_reset_counters`.
  - Verified that after 1 warmup iteration, the 50,000-iteration `10a_high_churn` steady-state loop performs **exactly 0 OS virtual memory calls**.
- **Benchmark Runner & Tooling Integration**:
  - Added `--rcache-policy` and `--rcache-memcap` command-line flags to `benchmarks/runner.py`.
  - Explicitly records `rcache_policy` and `rcache_memcap` in benchmark metadata and outputs.
  - Enabled side-by-side comparison between Policy 0 (decommit-on-reuse) and Policy 1 (fast small-region pool).
- **Extreme Performance Improvement**:
  - `10a_high_churn` (50,000 regions): reduced median latency from 103.21 ms down to **0.7564 ms** (**137x speedup**), beating Clang C (2.23 ms, **2.95x faster**) and Rust (3.21 ms, **4.24x faster**).
  - `10_region_alloc` (5,000 nested regions): reduced latency from 44.04 ms to **21.79 ms** (**2.02x speedup**), outperforming Rust (27.47 ms).
  - `10f_reuse`: reduced latency from 2.20 ms to **0.0335 ms** (**66x speedup**), 3x faster than C (0.1031 ms).

### Tests
- Expanded test suite from 253 to 258 automated compiler tests (100% passing in Debug and Release):
  - `CodeGen.SmallRegionPoolSteadyStateZeroVirtualMemoryOps`
  - `CodeGen.SmallRegionPoolZeroInitializationGuaranteed`
  - `CodeGen.SmallRegionPoolAggregateMemoryCapEnforced`
  - `CodeGen.SmallRegionPoolPolicy0DecommitBehavior`
  - `CodeGen.LargeRegionBypassesSmallRegionPool`

## [0.5.5] - 2026-11-10

### Added & Hardened
- **Thread-Safe Reservation Caching**:
  - Replaced process-global reservation cache slots with genuine thread-local caches (`thread_local ThreadReservationCache`).
  - Completely eliminated cross-thread race conditions, double-acquisition, and memory corruption when multiple concurrent threads execute region lifecycles.
  - Implemented automatic thread-exit cleanup via RAII destructor, safely releasing (`VirtualFree(MEM_RELEASE)`) any cached reservations when a thread terminates.
  - Added multithreaded stress regression tests verifying 8 concurrent threads executing 8,000 region lifecycles with verified arithmetic checksums and zero double-use or leaks.
- **Target Memory Page Provider Runtime Integration**:
  - Packaged the C++ `MemoryPageProvider` implementation into a standalone static runtime library target (`tamarin_runtime`) and linked it into all generated executables and shared libraries.
  - Exported standard C ABI runtime interface (`__tamarin_runtime_page_reserve`, `__tamarin_runtime_page_commit`, `__tamarin_runtime_page_decommit`, `__tamarin_runtime_page_release`, `__tamarin_runtime_page_size`, `__tamarin_runtime_allocation_granularity`, `__tamarin_runtime_working_set_bytes`).
  - Removed all hardcoded 4,096-byte page and 65,536-byte allocation granularity assumptions from generated LLVM IR; reservation, chunk sizing, and alignment calculations now dynamically query the linked provider.
  - Added regression tests proving the generated allocator dynamically respects provider-reported values and environment overrides.
- **Implicit Base-Class Construction**:
  - Fixed `IRBuilder::lowerConstructor()` to automatically construct base subobjects even when the base class does not declare an explicit zero-argument constructor, provided the base subobject requires non-trivial construction.
  - Base subobject members are strictly initialized before derived members and before the derived constructor body executes.
  - Preserves deterministic reverse-destruction order (`~Derived()` body -> `~Derived()` members -> `~Base()` members -> `~Base()` body), ensuring no destructor runs on an unconstructed object.
- **Sound Rust Benchmark Allocator Reference**:
  - Completely restructured the Rust benchmark allocator into a standalone, thoroughly tested library (`tamarin_benchmarks_rust::Arena`).
  - Constrained the safe public `alloc` / `alloc_slice` methods to a sound `Zeroable` trait, preventing arbitrary `Copy` types with non-zero valid invariants from being unsoundly zero-initialized, and introduced `alloc_uninit` for uninitialized allocations.
  - Enforced strict power-of-two alignment validation, alignment to at least `align_of::<T>()`, chunk alignment to `max(align, 16)`, and checked arithmetic across all pointer offsets and capacity growth.
  - Added 7 automated Rust soundness tests covering alignment validation, non-zeroable type handling, chunk exhaustion, and destructor ordering.
- **Accurate Memory Telemetry**:
  - Integrated real Win32 process working-set measurement (`K32GetProcessMemoryInfo`) into `MemoryPageProvider::getResidentWorkingSet()`.
  - Updated runtime region telemetry format to explicitly distinguish between `Process working set (resident RAM)` and `Committed bytes / commit charge`.
  - Guaranteed commit charge is never falsely reported as physical or resident memory.
- **High-Churn Performance Investigation & Retention Optimization**:
  - Conducted deep profiling of Windows NT virtual memory syscall costs across 50,000 region lifecycles: measured `VirtualAlloc(MEM_COMMIT)` (27.56 ms), `VirtualFree(MEM_DECOMMIT)` (22.87 ms), syscall pair (43.77 ms), and userland `memset` (0.0035 ms, ~12,000x faster than syscall).
  - Implemented configurable retention caching policy (Policy 1) that retains committed reservations up to a strict configurable memory cap (`TAMARIN_RCACHE_MEMCAP`, default 128 KB) and restores kernel zero guarantees via userland `memset`, cutting high-churn iteration latency from 101.40 ms down to 26.66 ms (**3.80x speedup**) while matching Rust's performance on `10_region_alloc` (27.45 ms vs 27.54 ms).

### Tests
- Expanded test suite from 248 to 253 automated compiler tests:
  - `CodeGen.ImplicitBaseConstructorNonTrivialMemberOrder`
  - `CodeGen.GeneratedAllocatorUsesProviderReportedValues`
  - `CodeGen.MemoryTelemetryProcessWorkingSet`
  - `CodeGen.ReservationCacheDirectMultithreadedIsolation`
  - `CodeGen.ReservationCacheMultithreadedStress`
- Added 7 automated Rust unit/soundness tests in `benchmarks/rust/tests/soundness_tests.rs`.
- 100% passing across Debug and Release configurations (253 / 253 compiler tests).
- All 21 benchmark configurations verified bit-for-bit with exact arithmetic checksums.

## [0.5.4] - 2026-11-10

### Added & Hardened
- **Object Construction Correctness & Ordering Parity**:
  - Implemented default constructor invocation in declaration order (`0..N-1`) for class arrays (`Token tokens[N];`) both inside regions and on stack frames.
  - Implemented element destruction in reverse declaration order (`N-1..0`).
  - Implemented in-place element construction for explicit array initializers (`Token tokens[3] = { Token(1), Token(2), Token(3) };`), avoiding shallow bitwise copying of resource-owning types.
  - Implemented subobject member construction in declaration order before executing enclosing constructor bodies for composite aggregates, ensuring members are properly initialized before destruction.
  - Enforced strict construction-before-destruction invariant: no destructor obligation is registered or executed against an object unless that object's constructor has successfully finished.
- **OOM-Safe Two-Phase Sidecar Registration**:
  - Replaced post-construction sidecar registration with two-phase protocol: `RegionReserveDestructor` pre-reserves an inactive sidecar slot before constructor execution begins, and `RegionActivateDestructor` activates it with exact count and stride after construction succeeds.
  - Completely eliminates the sidecar OOM registration window where an object was constructed but had no recorded cleanup metadata.
- **Adaptive Sidecar Sizing**:
  - Reduced initial sidecar allocation from 32 records (1,304 bytes) to 4 records (184 bytes), achieving an **85.9% reduction in sidecar memory overhead** for regions with few resource-owning objects.
  - Geometric expansion policy doubles capacity on chunk exhaustion ($4 \to 8 \to 16 \to 32 \to 64 \to 128$) up to 128 records per chunk under sustained sidecar pressure.
  - Trivial objects incur 0 bytes of sidecar overhead, 0 headers, and 0 system allocations.
- **Target Memory Page Provider Runtime Integration**:
  - LLVM IR region code generation now connects directly to the native `MemoryPageProvider` runtime interface (`@__tamarin_page_reserve`, `@__tamarin_page_commit`, `@__tamarin_page_decommit`, `@__tamarin_page_release`).
  - Added dynamic query of OS page size (4 KB) and allocation granularity (64 KB).
  - Explicitly documented and tested eager-commit semantics of `FallbackPageProvider`.
- **Bounded Region Reservation Caching**:
  - Implemented bounded reuse cache of up to 2 region virtual address reservations.
  - Active chunks are decommitted to 0 bytes physical commit charge via `@__tamarin_page_decommit` before retention, eliminating kernel address space reservation overhead on high-churn regions while keeping commit charge zero between region scopes.
- **Honest Memory Telemetry & Reporting**:
  - Renamed misleading telemetry metrics: `"Physical committed"` is now `"Committed bytes / commit charge"` and `"Page commits"` is now `"Commit operation count"`.
  - Added telemetry fields for alignment padding, unused committed bytes, and virtual reservation count.
  - Disentangled storage provenance from reference/view types in static memory planning reports (`--emit-memory-report`).
- **Defensible Allocator Benchmarks & Sound Rust Reference**:
  - Replaced unsound shared-reference mutation in `benchmarks/rust/src/bin/10_region_alloc.rs` with `std::cell::UnsafeCell<ArenaState>`.
  - Updated primary `10_region_alloc` to exercise sustained chunk growth beyond 64 KB (`growth[18000]` = 72 KB) across Tamarin, C, and Rust with verified identical checksums (`135693757500`).
  - Expanded benchmark suite with dedicated allocator workloads: `10a_high_churn`, `10b_large_single`, `10c_chunk_growth`, `10d_empty_region`, `10e_sparse_dtors`, and `10f_reuse`.

### Tests
- Expanded test suite from 240 to 248 automated compiler tests:
  - `PageProvider.DynamicQueryAndInvalidRanges`
  - `PageProvider.FallbackSemanticsDetailed`
  - `CodeGen.ClassArrayDefaultConstructionAndDestructionOrder`
  - `CodeGen.ClassArrayExplicitConstructorsOrder`
  - `CodeGen.AggregateNonTrivialMemberConstructorOrder`
  - `CodeGen.InheritanceBaseAndDerivedConstructorDestructorOrder`
  - `CodeGen.RegionSparseSidecarAdaptiveGrowth`
  - `CodeGen.StackArrayDefaultConstructionAndDestructionOrder`
- 100% passing across Debug (248/248) and Release (248/248) configurations.
- Expanded runner validation tests to 18 automated tests in `tests/benchmarks/test_runner_validation.py` (100% passing).

---

## [0.5.3] - 2026-11-09

### Added & Hardened
- **Adaptive Memory Engine & Page-Backed Lazy-Commit Regions**:
  - Transitioned region memory management from fixed heap allocations (`malloc`) to virtual memory page-backed allocation using OS address space reservation (`MEM_RESERVE`) and page-granular on-demand commitment (`MEM_COMMIT`).
  - Empty regions reserve 0 virtual bytes, commit 0 physical pages, and make 0 operating system calls.
  - Page-granular commitment commits memory in 4 KB multiples, reducing physical RAM consumption by up to 93.75% for small temporary buffers.
  - Reclaims all virtual address reservations and committed pages in a single OS syscall (`VirtualFree(ptr, 0, MEM_RELEASE)`) upon region exit.
  - Eliminated redundant per-object `memset` instructions on freshly committed pages by leveraging kernel-guaranteed zero-initialization on newly committed virtual memory.
- **Internal Virtual Memory Provider (`MemoryPageProvider`)**:
  - Introduced `MemoryPageProvider` interface with `reserve`, `commit`, `decommit`, `release`, `pageSize`, and `allocationGranularity`.
  - Implemented `WindowsPageProvider` using Win32 `VirtualAlloc`, `VirtualFree`, and `GetSystemInfo`.
  - Implemented `FallbackPageProvider` for non-virtual-memory or portable environments.
- **Sparse RAII Cleanup Sidecars**:
  - Lifted v0.5.0/v0.5.2 compile-time rejection of non-trivial destructors in regions.
  - Trivial objects (POD structs, primitive arrays) pay 0 bytes of header, 0 bytes of metadata, and 0 sidecar records.
  - Non-trivial objects with user-defined destructors register in an out-of-band Sparse Sidecar, running in strict reverse completed-construction order (LIFO) before region memory is reclaimed.
  - Batch registration for fixed-size arrays of resource owners: an array of $N$ objects registers a single 40-byte sidecar record and executes element destructors in reverse index order.
  - Top-level aggregates with resource-owning fields register once; the compiler-synthesized destructor transitively destroys members in reverse declaration order.
- **Static Memory Planning Reporter (`--emit-memory-report`)**:
  - Added CLI flag `--emit-memory-report` producing a structured table detailing declaration name, type, size, alignment, storage tier (Stack, Region, Global), lifetime, destructor classification (None, Direct RAII, Sparse Sidecar), borrow status, and payload contributions.
- **Runtime Memory Telemetry (`--memory-stats`)**:
  - Added CLI flag `--memory-stats` emitting high-precision runtime region telemetry to `stderr` on region exit (hint capacity, virtual reserved, physical committed, handed-out payload, allocation count, chunk count, page commit count, destructor count).
- **IRBuilder Region Allocation Fix**:
  - Corrected `IRBuilder::lowerVarDecl()` so that all arrays and aggregates declared lexically inside a region block reliably emit `Opcode::RegionAlloc` rather than falling back to stack allocation due to popped local symbol table scopes.

### Tests
- Expanded test suite from 222 to 240 automated tests:
  - 2 new unit tests for `WindowsPageProvider` and `FallbackPageProvider`.
  - 11 new end-to-end codegen execution tests covering empty region zero-allocation, single destructor execution, multiple destructor LIFO ordering, array batch destruction, aggregate member destruction, interleaved trivial and non-trivial types, early return cleanup, loop cleanup, multi-page boundary crossing with zero-init, chunk growth beyond 64 KB, and runtime telemetry output.
  - 1 new driver test verifying static memory planning report generation.
- 100% passing across Debug (240/240) and Release (240/240) configurations, plus 16 automated benchmark runner validation tests.


### Added & Hardened
- **Scope-Safe Provenance Metadata & Binding IDs**:
  - Replaced name-keyed flat variable provenance tables with `BindingId`-keyed storage (`bindingProvenance_` and `bindingFieldProvenance_`), where each declared symbol receives a unique monotonic binding ID from `SymbolTable`.
  - Inner block declarations shadowing an outer identifier now maintain distinct provenance without corrupting the outer scope upon block exit.
- **Strict Interprocedural Borrow Forwarding**:
  - Corrected unknown external function calls to reject all non-static borrow arguments (`ParameterBorrow`, `ThisBorrow`, `LocalRegion`, `Unknown`) rather than checking only `LocalRegion`.
  - Passing borrowed parameters to external C functions or callee stubs without verified no-escape contracts is rejected at compile time.
- **Method Receiver Escape Analysis & Call Verification**:
  - Enforced receiver escape verification in `checkMethodCall()`; external or unverified methods cannot escape receiver storage to global or outer scopes.
  - Return borrows derived from receiver methods correctly propagate to caller expressions.
- **Cycle-Safe Borrow Type Graph Analysis**:
  - Rewrote `Type::canContainBorrow()` using an iterative DFS with Gray/Black state coloring, preventing infinite recursion on self-referential or mutually recursive aggregate types.
  - Strictly excluded compiler-managed `_vptr` pointers from borrow capability analysis.
- **Compile-Time Constant Expression Array Sizing**:
  - Added support for bitwise shifts (`<<`, `>>`) and bitwise logic (`&`, `|`, `^`) in `TypeChecker::evalIntConstExpr()`.
  - Added `evaluatedArraySize` field to `VarDeclStmt` and `GlobalVarDecl`, and plumbed it directly into `IRBuilder::lowerVarDecl()` so constant expressions like `4 * 1024` or `1 << 12` evaluate cleanly at compile time and allocate correct chunk capacities in regions and stacks.
- **Allocator Runtime Hardening & Exact Output Verification**:
  - Region capacity initialization rejects zero, negative, or overflow values deterministically, branching to the runtime `%oom` handler.
  - Corrected LLVM codegen OOM error message length to exactly 44 bytes to avoid emitting trailing null bytes to stderr.
  - Added checked 64-bit cumulative counter addition (`new_r_used`, `new_r_res`) to prevent integer wrap in region usage statistics.
- **Cross-Language Workload 10 Benchmark Realism**:
  - Hardened C baseline with NULL-check validations and overflow-checked capacity handling.
  - Updated Rust baseline with true linked-chunk dynamic growth matching Tamarin's chunked arena design.
  - Verified bit-for-bit checksum equality across all 8 configurations.

### Tests
- Expanded test suite from 201 to 222 automated tests (17 new semantic analysis tests, 4 new codegen execution tests).
- 100% passing across Debug and Release configurations, plus 16 automated benchmark runner validation tests.

## [0.5.1] - 2026-11-09

### Added & Hardened
- **Coherent Semantic Provenance Architecture (`tamarin::sema::ProvenanceSet`)**:
  - Replaced single integer `regionId` tracking with a multi-origin provenance model: `ProvenanceKind::None`, `StaticStorage`, `LocalRegion`, `ParameterBorrow`, `ThisBorrow`, and `Unknown`.
  - Type identity strictly separated from value provenance: types like `int[]` represent the value representation while `ProvenanceSet` on expressions and symbol bindings tracks storage origins.
  - Supports multiple independent origins, field-level provenance, array element address-of provenance, and cast propagation.
- **Function Parameter Escape Analysis & Effect Summaries**:
  - Function parameters of reference, view, and aggregate types receive symbolic parameter provenance (`ParameterBorrow`).
  - Storing borrowed parameters into globals, static fields, or outer-lived aggregates is rejected at compile time with source-located diagnostics.
  - Introduced `FunctionBorrowSummary`: infers no-escape contracts from function bodies, allowing safe temporary borrowing (`sum(int values[])`) without false-positive rejections.
  - External function declarations without verified borrow summaries conservatively reject passing region-backed references or views.
- **Method Receiver & Receiver-Field Provenance**:
  - Method receivers tagged with `ThisBorrow`; methods returning references or views derived from `this` propagate receiver provenance to caller method-call expressions.
  - Storing method receivers or receiver fields into static/global storage or outer lifetimes rejected at compile time.
- **Lexical Region Ancestry & Outlive Relations**:
  - Enforced `isRegionAncestor` and `doesRegionOutlive` over numeric region IDs; child regions cannot outlive parent regions; outer references cannot bind to inner region allocations.
- **Aggregate & Compound Init Provenance**:
  - Field-specific provenance tracking (`varFieldProvenance_`); compound initializers aggregate field-level provenances; container storage lifetime distinguished from referent borrow lifetime.
- **Allocator Runtime Hardening in LLVM Backend**:
  - Validated power-of-two alignment checking (1, 2, 4, 8, 16; else deterministic abort).
  - Allocation size validity and overflow protection on aligned offsets, required sizes, and chunk geometric doubling.
  - Cleaned up chunk metadata on allocation failure to avoid dangling pointers prior to OOM abort.
  - Fixed OOM error message length in `@__tamarin_region_oom_error` from 44 to 45 bytes (including trailing newline).
- **Structure-of-Arrays (SoA) Region Rejection (Option B)**:
  - `@layout(scan)` SoA arrays inside regions rejected during semantic analysis with clear diagnostic (`"region allocation does not support @layout(scan) SoA arrays in v0.5.1; use contiguous Array-of-Structures or stack allocation"`), eliminating silent stack-alloca fallback.
- **Benchmark Workload 10 Integrity**:
  - Hardened C baseline in `10_region_alloc.c` to use pointer-stable linked-chunk bump allocation matching Tamarin's monotonic arena.
  - Hardened Rust baseline in `10_region_alloc.rs` to use sound memory encapsulation and per-allocation zeroing rather than 64 KB upfront zeroing.
  - Verified bit-for-bit checksum equivalence across all 8 configurations.
- **Test Suite Expansion**:
  - Added 18 new compiler tests (total **201 tests**, 100% passing across Debug and Release configurations):
    - 13 new semantic analysis tests covering global escape, receiver escape, method returning region field, compound init escape, nested aggregate escape, field assignment, alias escape, external call escape, safe helper borrowing, method returning receiver view, SoA rejection, array size multiplication overflow, and constant capacity expressions.
    - 4 new codegen tests covering AoS element mutation/bounds, constant capacity expressions, nested region borrowing, and loop break/continue/early return cleanups.
    - 1 parser/ast top-level global variable declaration test.
  - All 16 automated benchmark runner validation tests passing.

---

## [0.5.0] - 2026-11-07

### Added
- **Lexical Memory Regions (`region [name] [(capacity)] { ... }`)**:
  - Introduced C-like lexical memory regions for high-performance, deterministic bump-pointer allocation.
  - Supports named and anonymous regions with default capacity (65,536 bytes) or compile-time positive integer constant expressions.
  - Reclaims all backing chunk memory in bulk upon block exit with zero per-object free overhead.
- **Monotonic Arena Runtime Engine**:
  - Implemented `@__tamarin_region_init(ptr, i64)`: Initializes stack-allocated `%__tamarin_region` descriptor with initial chunk capacity.
  - Implemented `@__tamarin_region_alloc(ptr, i64, i64)`: O(1) bump-pointer allocation fast path with zero-initialization (`memset 0`) and alignment (1, 2, 4, 8, 16 bytes).
  - Linked-chunk geometric growth (`@__tamarin_region_chunk`): Dynamically allocates and links additional chunks (`max(2 * cap, req_size)`) when allocations exceed the active chunk, preserving pointer stability of previously allocated objects.
  - Out-of-memory handler (`@__tamarin_region_oom_error`): Halts deterministically with exit code 1 and error diagnostic to standard error upon allocation failure.
  - Bulk destruction (`@__tamarin_region_destroy(ptr)`): Traverses and frees all backing chunks in a single pass.
- **Region-Backed Storage & Stack Scalars**:
  - Local fixed-size arrays (`T[N]`) and eligible aggregate structs declared inside regions are automatically allocated from the region's arena via `RegionAlloc`.
  - Scalar variables (integers, floats, pointers, loop indices) remain on the stack / CPU registers for peak execution throughput.
- **Strict Compile-Time Lifetime & Escape Safety**:
  - Added region provenance tracking to semantic analyzer (`SymbolTable` and `TypeChecker`).
  - Strict escape prevention: Rejects returning region-backed references, arrays, or views from functions.
  - Outlives enforcement: Rejects assigning region-backed references/views to outer variables, globals, or outer struct fields.
  - Conservative borrow semantics for function calls: Callees cannot escape borrowed region storage.
  - Destructor safety: Types with non-trivial destructors are rejected from region allocation at compile time with diagnostic `"region allocation does not support types with non-trivial destructors in v0.5.0"`.
- **Complete Control-Flow Cleanup**:
  - Seamless region destruction on early `return`, `break`, and `continue` across scope boundaries via TIR cleanup stack.
  - Full support for nested regions and per-iteration loop reclamation.
- **Test Suite Expansion**:
  - Expanded test suite by 20 new tests (total **183 tests**, 100% passing across Debug and Release configurations):
    - `Parser.ParseRegionSyntaxVariants`
    - `Sema.RegionValidPasses`
    - `Sema.RegionRejectNegativeOrZeroCapacity`
    - `Sema.RegionRejectNonConstantCapacity`
    - `Sema.RegionRejectNonTrivialDestructor`
    - `Sema.RegionRejectEscapeReturningView`
    - `Sema.RegionRejectAssignToOuterLifetime`
    - `Sema.RegionRejectGlobalEscape`
    - `Sema.RegionRejectNestedClassWithDestructor`
    - `CodeGen.RegionBasicExecution`
    - `CodeGen.RegionStructExecution`
    - `CodeGen.RegionMultipleSequentialAllocations`
    - `CodeGen.RegionChunkGrowthExecution`
    - `CodeGen.RegionNestedExecution`
    - `CodeGen.RegionLoopReclamation`
    - `CodeGen.RegionEarlyReturnCleanup`
    - `CodeGen.RegionBreakContinueCleanup`
    - `CodeGen.RegionZeroInitialization`
    - `CodeGen.RegionAddressStability`
    - `CodeGen.RegionMultipleSequential`
  - All 16 automated benchmark runner validation tests passing.
- **Benchmark Suite Expansion (Workload 10: `10_region_alloc`)**:
  - Added `10_region_alloc` comparing monotonic arena bump allocation across Tamarin, C (Clang & MSVC), and Rust.
  - Simulates 5,000 iterations of nested region allocations (10,000 total regions), validating identical outputs (`PARTICLES: 124999750000`, `SCRATCH: 6694250000`, `NESTED: 3877875000`, `CHECKSUM: 135571875000`).
  - Tamarin -O3 achieves 4.60 ms (faster than MSVC C /O2 at 8.14 ms, competitive with Rust at 3.70 ms).

---

## [0.4.2] - 2026-11-07

### Added & Hardened
- **Iterative 3-State Cycle-Safe Graph Traversal**:
  - Replaced recursion depth limits (`vList.size() > 128`) in `Type::hasNonTrivialDestructor()` with an iterative, cycle-safe DFS using an explicit heap-allocated stack frame vector and 3-state White/Gray/Black coloring.
  - Eliminated false-positive non-trivial destructor classifications on deep trivial types at depths 70, 129, 256, 512+.
  - Handled shared subgraphs with $O(1)$ memoization and guaranteed zero host stack recursion.
- **IEEE-754 Bitcast Floating-Point Verification**:
  - Added built-in intrinsic `__f64_to_bits(double) -> u64` (and alias `__double_to_bits`) lowering to LLVM `bitcast double %d to i64` (`movq %xmm0, %rax`).
  - Workload `02_float` verified bit-for-bit across all 9 compiler configurations using exact IEEE-754 64-bit integer values: `MANDEL: 4703990612284669952`, `MATMUL: 4693648769122566158`, `CHECKSUM: 4705341001542139908`.
- **Timer Frequency Pre-Fetch & Boundary Alignment**:
  - Reordered `@__tamarin_benchmark_start` and C `timer_start()` to query and validate `QueryPerformanceFrequency` before reading start counters, eliminating frequency query latency from the timed interval.
  - Hoisted array storage allocation and zero-initialization memsets (`is_prime[20000]`) outside the timer boundary in `01_integer` across C, Rust, and Tamarin.
- **Strict Benchmark Contract Enforcement**:
  - Replaced aggregate-only checksums with complete field schemas across all workloads (including intermediate fields: `TOTAL_V` in `04_structures`, `SCAN`/`TRANSFORM`/`FINAL` in `03_arrays`, `FIB`/`ACC` in `05_function_calls`).
  - Implemented immediate fail-fast termination on non-zero exit codes, negative/NaN/Inf timing, malformed delimiters, missing fields, duplicate fields, unknown fields, or value mismatches in warmup or measured repetitions.
  - Added 16 automated tests in `tests/benchmarks/test_runner_validation.py` covering all failure modes.
- **Statistical Rigor & Configuration Coverage**:
  - Interleaved round-robin execution across all 9 configurations (`tamarin_o0`, `tamarin_o1`, `tamarin_o2`, `tamarin_o3`, `c_clang_o2`, `c_clang_o3`, `c_msvc_o2`, `rust`, `rust_lto`).
  - 30 Mode A repetitions (5 warmups) and 100 Mode B startup repetitions (10 warmups).
  - Computed 95% bootstrap confidence intervals for medians and speedup ratios using 1,000 resamples (fixed seed 42).
- **Test Suite Expansion**:
  - Added 5 new compiler unit tests covering deep destructor traversal (depths 70, 129, 256, 512), all arithmetic promotion combinations, and `__f64_to_bits` lowering.
  - Expanded test suite to **163 tests** (100% passing across Debug and Release configurations).

---

## [0.4.1] - 2026-11-07

### Added & Revalidated
- **Benchmark Integrity & Two-Mode Measurement Architecture**:
  - Implemented strict separation between pure computational throughput (Mode A) and process startup latency (Mode B).
  - Mode A measures pure compute inside the process via Windows `QueryPerformanceCounter` (`timer.h` in C, `__benchmark_start()` / `__benchmark_elapsed_ms()` in Tamarin, `std::time::Instant` in Rust), strictly excluding memory allocation, particle setup, and console I/O from timed intervals.
  - Mode B isolates parent-process startup latency (`time.perf_counter_ns()`) for minimal no-op programs across 50 measured repetitions.
  - Established interleaved round-robin repetition scheduling across all configurations to neutralize temporal noise, thermal throttling, and CPU boost clock decay.
- **Workload Equivalence & Arithmetic Integrity**:
  - Fixed signed 64-bit integer overflow undefined behavior in C benchmarks (`01_integer`, `05_function_calls`), standardizing on 64-bit unsigned wrapping arithmetic (`uint64_t` in C, `u64` in Rust, `u64` in Tamarin).
  - Standardized double-precision floating-point reporting in `02_float`, validating bit-for-bit exact checksum equivalence (`3765612.0000`).
  - Standardized particle simulations (`04_structures_aos` and `04_structures_soa`) to identical 5,000-element local stack allocations (120 KB), isolating the 200 simulation steps and proving an empirical **2.48x SIMD auto-vectorization speedup** for `@layout(scan)`.
- **Compiler Enhancements & Dynamic Dispatch Control**:
  - Implemented `@noinline` function attribute in parser, AST, TIR, and LLVM backend, guaranteeing genuine dynamic vtable dispatch in `07_virtual_dispatch` without compiler devirtualization.
  - Added built-in benchmark timing intrinsics `__benchmark_start()` and `__benchmark_elapsed_ms()` / `__benchmark_stop()`.
  - Added mathematically sound symmetric arithmetic promotion in `TypeContext::getCommonArithmeticType`, supporting mixed signed/unsigned and float/int promotion.
  - Hardened destructor traversal `Type::hasNonTrivialDestructor()` with `noexcept` safety, depth bounds, and cycle detection.
- **Automated Test Suite Expansion**:
  - Added 8 new unit and regression tests in `tests/sema/sema_test.cpp` and `tests/codegen/codegen_test.cpp`.
  - Expanded test suite to **158 tests** (100% passing across Debug and Release configurations).
- **Benchmark Runner Overhaul**:
  - Complete rewrite of `benchmarks/runner.py` with automatic toolchain discovery, isolated build output in `benchmarks/build/bin/`, non-zero exit validation, every-repetition checksum verification, and detailed JSON (`benchmark_data.json`) and Markdown (`benchmark_summary.md`) emission.

---

## [0.4.0] - 2026-11-07

### Added & Performance Engineered
- **Safety Gate Audit & Cycle Detection Traversal**:
  - Eliminated arbitrary recursion-depth limits (`depth > 64`) in `Type::hasNonTrivialDestructor()` that previously risked silently suppressing destructor execution in deep hierarchies.
  - Implemented graph traversal tracking visited types (`std::vector<const Type*>`); cycle detection conservatively treats cycles as non-trivial (`return true`).
  - Added unit test `CodeGen.DestructorDeepNestingExceeds64Levels` verifying that 70 levels of valid aggregate nesting containing a resource-owning member synthesize and execute destructors deterministically.
- **Compiler Correctness & ABI Hardening**:
  - `checkIntLiteral`: Large integer constants exceeding `INT32_MAX` or below `INT32_MIN` now correctly infer `i64` (`getI64()`).
  - `checkBinary`: Integer binary arithmetic and bitwise operators widen unequal operand types to the wider type (`lhsTy->size() >= rhsTy->size() ? lhsTy : rhsTy`), while bitwise shift operators preserve `lhsTy`.
  - Compound Index Assignment: Corrected `a[i] += val` to load element, apply operation, and store back instead of overwriting with the right-hand operand.
  - Struct Array Element Mutation: `getStructPointer` now emits `Opcode::GetArrayElemPtr` directly into the array buffer rather than making temporary stack copies, resolving silent data mutation loss on array-of-structs.
  - Windows x64 Calling Convention & `snprintf` ABI: Promoted sub-32-bit integers (`i8`, `i16`) to `i32`, promoted `f32` to `f64` for variadic printing, and added dedicated `@__tamarin_print_i64` / `@__tamarin_println_i64` helpers using `%lld`.
- **LLVM Code Generation Entry Alloca Hoisting**:
  - Hoisted all stack allocation instructions (`Alloca`, `ArrayAlloc`, `SoAArrayAlloc`) to the function entry basic block.
  - Prevents stack frame accumulation in loops at `-O0` (eliminating `STATUS_STACK_OVERFLOW` / `0xC00000FD`).
  - Enables LLVM's canonical `Mem2Reg` and `SRoA` passes to immediately promote entry allocas into SSA virtual registers.
- **CLI Pipeline Inspection Tools**:
  - `--emit-tir`: Emits Tamarin Intermediate Representation.
  - `--emit-llvm`: Emits unoptimized LLVM IR prior to optimization.
  - `--emit-optimized-llvm`: Emits fully optimized LLVM IR post-pipeline.
  - `--emit-asm`: Emits native x86-64 assembly instructions (`.s`).
- **Comprehensive Benchmark Suite & Results**:
  - 10 benchmark workloads (`01_integer`, `02_float`, `03_arrays`, `04_structures_aos`, `04_structures_soa`, `05_function_calls`, `06_branching`, `07_virtual_dispatch`, `08_memory_ops`, `09_startup`).
  - Cross-language benchmark runner (`benchmarks/runner.py`) testing Tamarin (`-O0`, `-O2`, `-O3`), Clang C (`-O2`, `-O3`), MSVC C (`/O2`), and safe Rust (`--release`).
  - High-precision monotonic Windows QPC timing, 2 warmups, 10 repetitions, and bit-for-bit algorithmic checksum verification.
  - Demonstrated that TAMARIN native code matches or exceeds Clang C and safe Rust across all workloads, with Structure of Arrays (SoA) layout delivering hardware-vectorized SIMD execution.
- **Performance Documentation**:
  - Added `docs/performance/benchmark_methodology.md`, `benchmark_results.md`, `compiler_optimization.md`, and `generated_code_analysis.md`.
- **Automated Test Suite**:
  - Expanded to **150 tests** (100% passing across Debug and Release configurations).

---

## [0.3.8] - 2026-11-07

### Added & Hardened
- **Effective Resource Ownership & Destructor Synthesizing**:
  - Implemented `Type::hasNonTrivialDestructor()` with cycle protection to define effective resource ownership across declared destructors, base class destructors, member field destructors, and array elements.
  - Automatically synthesizes destructors for intermediate classes and structs that contain or inherit resource owners without requiring empty user-defined destructors.
  - Enforced strict destruction order: derived user destructor body runs first, followed by member fields destroyed in reverse declaration order, followed by base class destructor invocation.
  - Fixed duplicate cleanup registration bug in `IRBuilder::lowerVarDecl` for record types and eliminated improper cleanup registration for reference types.
  - Added deterministic cleanup scope emission on loop control statements (`break`, `continue`) and early returns.
- **Bounded CRT Interoperability & Path Safety**:
  - Replaced unsafe raw pointer passing to CRT file functions (`fopen`, `remove`) with `@__tamarin_view_to_cstr`.
  - `@__tamarin_view_to_cstr` performs length bounds validation, allocation size overflow checks, heap buffer allocation, character-by-character validation, embedded null byte rejection, and null termination.
  - Guaranteed `free()` cleanup of all temporary heap buffers across all successful and error return paths.
- **Unified Length-Aware Console Output Architecture**:
  - Unified all console output (`print`, `println`) to route exclusively through `@__tamarin_write_stdout` -> Windows CRT `_write(1, ...)`, eliminating stdout stream interleaving and reordering caused by mixing buffered stdio with low-level file descriptor writes.
  - Formatted scalar values (integers, floats, booleans) into stack buffers using `snprintf` with `%d` for 32-bit signed integers; outputs via low-level write sink.
  - Chunked large writes into 64KB increments with retry loops to guarantee full data transmission without 32-bit truncation or partial-write data loss.
- **Caller-Owned Console Input with Truncation Discard Semantics**:
  - Rewrote `@__tamarin_get_input_buf` to consume and discard the remainder of any line that exceeds caller buffer capacity, guaranteeing subsequent reads receive the next logical line rather than leftover truncated bytes.
  - Handles CRLF (`\r\n`), lone CR (`\r`), LF (`\n`), empty lines, EOF before input (`-1`), capacity 0, and capacity 1 edge cases safely.
  - Added deprecation warning for legacy zero-argument `getInput()` and documented its temporary shared view semantics.
- **Resource Return-by-Value & Copy Rejection**:
  - Replaced isolated destructor checks with `hasNonTrivialDestructor()` across all semantic analysis paths.
  - Strictly rejects returning resource-owning types by value from functions and methods until safe move semantics and ownership transfer are implemented.
  - Audited and reinforced compile-time rejection of copy-initialization, assignment, pass-by-value, array element copying, and compound struct field copying for all resource-owning types.
- **Builtin Semantic Validation**:
  - Added comprehensive compile-time validation for builtin calls in `TypeChecker::checkCall`: exact argument count checking, parameter type enforcement, and const-qualification checks for `print`, `println`, `len`, `getInput`, `readLine`, `openFile`, `closeFile`, `readFile`, `writeFile`, `appendFile`, `fileExists`, `removeFile`, `fileRead`, `fileWrite`, and `fileFlush`.
- **Module Discovery & Test Suite**:
  - Removed all hardcoded development search paths (`"E:/Projects/Tamarin/lib"`) from test files and replaced with dynamic candidate discovery.
  - Expanded test suite to **148 tests** (23 new adversarial tests), covering destructor inheritance hierarchies, member reverse destruction, loop break/continue, early return, CRT path view safety, embedded null rejection, input line truncation discard, output ordering, and optimization equivalence (`-O0` through `-O3`).
  - 100% passing across Debug and Release configurations.

---

## [0.3.7] - 2026-11-07

### Added & Hardened
- **String Immutability & Const Array Views**:
  - `ArrayTypeNode` in AST and `Type` in sema extended with `isConst` flag (`const T[]`).
  - String literals now strictly infer `const u8[]` (`const char[]`).
  - Assignment to string literal elements (e.g. `"hello"[0] = 'x'`) or const array view elements (`const char s[] = ...; s[0] = 'a';`) is strictly rejected at compile time with a clear diagnostic.
  - Subtyping rules enforce that mutable array views widen to const views, but const views cannot silently convert to mutable views.
  - Parser updated to parse and preserve `const` on array type parameters and return types (`public const char[] getInput()`).
- **Safe Caller-Owned Input Buffer**:
  - Added caller-owned buffer input lowering `@__tamarin_get_input_buf` accepting `{ ptr, i64 } %buf, i32 %maxBytes`.
  - Reads bytes from standard input directly into the caller's pre-allocated buffer bounded by `min(maxBytes - 1, len(buf) - 1)` with null-termination and EOF handling (-1).
  - Normalizes CRLF and LF newlines without memory leaks or undefined buffer lifetime issues.
  - Retained 0-argument `getInput()` for backward compatibility.
- **Console Output Hardening**:
  - Lowered `print(str)` and `println(str)` to direct Windows CRT `_write(1, ptr, len)` calls using length-aware write semantics.
  - Eliminates format-string vulnerabilities: arbitrary user strings containing `%s`, `%n`, `%p`, `%d` are printed verbatim.
  - Eliminates null-byte truncation: views with embedded null bytes write their exact logical length safely.
- **Resource Ownership & Non-Copyable Classes**:
  - Classes declaring destructors (`~ClassName()`, e.g. `File`) are formally designated as non-copyable resource owners.
  - Semantic analysis strictly rejects copy initialization (`File f2 = f1;`), assignment (`f2 = f1;`), and passing resource types by value (`void use(File f)`).
  - References (`File&`, `const File&`) remain fully supported for non-copyable resource objects.
- **Standard Library Hardening & Dedup**:
  - Removed duplicate `File` implementation from `lib/std/io.tmn`; `std.file::File` is now the single canonical file resource owner.
  - Clamped negative and zero buffer lengths safely in `File::read` and `File::write`.
- **Numeric & Math Boundary Handling**:
  - `std.math::abs()` / `fabs()`: Handles `INT_MIN` saturation to `INT_MAX` rather than undefined overflow behavior; normalizes negative zero `-0.0f` to `+0.0f`.
  - `std.math::fmin()` / `fmax()`: Returns the non-NaN operand if one operand is NaN.
  - `std.math::clamp()` / `fclamp()`: Validates and corrects inverted clamp bounds `(lo > hi)`.
  - `std.math::ipow()`: Returns 0 for negative exponents with integer bases and returns 1 for power of 0.
- **Portable Search Roots**:
  - Removed hardcoded development path `"E:/Projects/Tamarin/lib"` from `src/driver/driver.cpp`.
  - Added dynamic Windows executable directory resolution via `GetModuleFileNameW` to discover `<exe_dir>/../lib` and `<exe_dir>/lib` automatically.
- **Automated Tests**:
  - Expanded test suite from 114 to **125 tests** across Lexer (6), Parser (6), Sema (60), CodeGen (26), Modules (14), and Stdlib (13).
  - 100% passing in both Debug and Release configurations.

---

## [0.3.6] - 2026-11-07

### Added
- **Static Module System & Imports**:
  - `import <module.path>;` declarations bringing imported declarations into a namespace alias matching the final component of the module path.
  - Explicit module aliases via `as`: `import <module.path> as <alias>;`.
  - Enforcement of top-level import placement: all import statements must precede ordinary declarations.
  - Duplicate import handling: identical imports with matching aliases are deduplicated safely; conflicting aliases report clear diagnostics.
- **Module Identity & Search Roots**:
  - Dotted module paths map deterministically to directory hierarchies (`math.vector` -> `<root>/math/vector.tmn`).
  - Strict path traversal rejection: module path components cannot contain `..`, `/`, or `\`.
  - Search root precedence ordering: entry dir, `-I <path>`, `TAMARIN_PATH`.
  - Canonical module caching ensuring each source file is loaded and parsed exactly once.
- **Topological Ordering & Circular Dependency Detection**:
  - 3-color DFS cycle detector traversing module dependency graphs.
  - Cycle diagnostics with formatted dependency chain error reporting.
  - Topological module ordering ensuring all imported dependencies are compiled and lowered before their consumers.
- **Access Control & Visibility**:
  - Top-level declarations are private to their module by default.
  - `public` modifier exposes top-level functions, structs, classes, and constants across module boundaries.
  - Strict visibility enforcement in semantic analysis.
  - Qualified identifier syntax `<alias>::<name>` supported across expressions, types, and calls.
- **Whole-Program Cross-Module CodeGen**:
  - Unified whole-program intermediate representation lowering in `IRBuilder::buildProgram`.
  - Single canonical type identity across modules in `TypeContext`.
  - Deterministic name mangling (`<module_path>_<name>`) while keeping `main` unmangled.
- **Testing**:
  - Automated test suite expanded to **107 tests** with 100% pass rate.

---

## [0.3.5] - 2026-11-07

### Added
- **Struct Methods & Member Receivers**:
  - Method declarations and definitions inside `struct` and `class`.
  - Const (`const`) and mutable instance methods with compile-time immutability enforcement.
  - Direct static dispatch for non-virtual methods without heap allocations.
- **Class System & Encapsulation**:
  - `class` declarations with default `private` member accessibility.
  - Access specifiers `public:`, `private:`, and `protected:`.
  - Constructors (`ClassName(...)`) and deterministic RAII destructors (`~ClassName()`).
- **Inheritance & Dynamic Polymorphism**:
  - Single inheritance syntax (`class Derived : public Base`).
  - Dynamic virtual dispatch: `virtual` and `override` method declarations, compiler-generated vtables, and `_vptr`.
  - Pure virtual methods (`virtual void method() = 0;`) and abstract classes / interfaces.
- **Safe References & Borrow Semantics**:
  - Reference types `T&` and `const T&`. Pass-by-reference without copying aggregate structures.
- **Advanced Memory Layouts**:
  - Structure-of-Arrays (SoA) for `@layout(scan)` and Array-of-Structures (AoS) for `@layout(point)`.
- **Testing**:
  - Automated test suite expanded to **88 tests**.

---

## [0.3.0] - 2026-11-07

### Added
- **User-Defined Struct Types**:
  - Support for `struct Name { Type field; ... };` declarations.
  - Nested structs and forward struct declarations.
- **Layout Directives**:
  - `@c_layout`, `@layout(point)`, and `@layout(scan)`.
- **Member Initializers & Compound Literals**:
  - C-style compound literals: `(Type){ .x = 1.0f, .y = 2.0f }`.
  - Designated member initializers and positional braced initializers.
- **Field Access & Mutation**:
  - Member access via `.` operator (`p.x`).
  - Const protection on struct field mutation.
- **Windows x86-64 ABI Layouts**:
  - Natural alignments, padding calculation, and total struct sizing.
- **Testing**:
  - Automated test suite expanded to **70 tests**.

---

## [0.2.1] - 2026-11-07

### Added
- Scope-Safe Variable Bindings and lexical shadowing support.
- Array Allocation Safety (rejection of VLAs, negative sizes, unsized arrays).
- Typed Array Operations and bounds checking for 8-, 16-, 32-, and 64-bit integers.
- Definite-Assignment Analysis for scalar variables.
- Numeric Semantics & Conversion Instruction Lowering Matrix.
- Short-circuit evaluation for `&&` and `||`.
- Automated test suite expanded to **59 tests**.

---

## [0.2.0] - 2026-11-07

### Added
- Pre- and post-optimization LLVM verification subsystem (`LLVMVerifier`).
- Optimization pipeline with CLI options `-O0`, `-O1`, `-O2`, `-O3`.
- Control-Flow Correctness: basic block termination tracking and reachability analysis.
- Array Bounds Hardening: negative index validation and 64-bit width reconciliation.
- Target configuration explicitly set to `x86_64-pc-windows-msvc`.
- Automated test suite expanded to **36 tests**.

---

## [0.1.0] - 2026-11-07

### Added
- Initial working TAMARIN compiler targeting Windows x86-64 via LLVM.
- C++23 CMake project configured with MSVC 2022.
- Lexer, Parser, AST, and Scoped Symbol Table.
- Exact numeric type checker with implicit integer promotion rules.
- Typed Tamarin IR (TIR) with explicit bounds checking and view operations.
- Array view fat pointer `{ ptr, i64 }` representation for array parameters.
- Builtin `len()` support.
- LLVM IR code generator and clang driver integration.
- 22 unit tests and 6 example programs.
