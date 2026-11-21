# Tamarin Compiler Architecture and Pipeline Specification

**Version:** 0.18.5  
**Classification:** Systems Engineering & Compiler Internals  

---

## 1. Architectural Overview

The Tamarin compiler (`tamarin`) is engineered as a modular C++23 compiler pipeline designed for high compilation throughput, verifiable semantic safety, and optimal native code generation via LLVM.

```text
+---------------------------------------------------------------------------------+
|                            Tamarin Compiler Architecture                        |
+---------------------------------------------------------------------------------+
   Source Files (.tmn)
          │
          ▼
   [ SourceManager ] ─────────► UTF-8 Memory Mapping & Line/Column Diagnostics
          │
          ▼
   [ Lexer & Parser ] ────────► Recursive Descent AST Construction
          │
          ▼
   [ Semantic Analysis (Sema) ]
     ├── Symbol Resolution & Scope Hierarchies
     ├── Hindley-Milner Type Inference & Static Type Checking
     ├── Monomorphization Engine (Zero-Cost Generics)
     ├── BorrowChecker (ProvenanceSet + ActiveBorrow Verification)
     ├── MoveChecker (Linear Resource Consumption & Drop Flag Synthesizer)
     └── DataRaceChecker (Affine Injectivity on parallel for)
          │
          ▼
   [ Tamarin IR (TIR) ] ──────► Static Single Assignment (SSA) Control Flow Graph
     ├── Pass: Range Analysis
     ├── Pass: Bounds Check Elimination (BCE)
     └── Pass: Alias Analysis & Redundancy Elimination
          │
          ▼
   [ LLVM Codegen & Optimizer ]
     ├── Target: Host Native CPU (-march=native, AVX2/AVX-512)
     ├── Optimization: LLVM O3 Pipeline + Fast-Math Vectorizer
     └── C99 Header Generator (--emit-header)
          │
          ├──────────────────────────────┬──────────────────────────────┐
          ▼                              ▼                              ▼
   Native Binary (.exe/.dll)      C99 Interface (.h)             LLVM IR (.ll)
```

---

## 2. Frontend Subsystems

### 2.1 `SourceManager` and Diagnostic Engine
- **Memory Mapping**: Source files are memory-mapped into contiguous read-only buffers with zero extra copies.
- **Diagnostics**: Diagnostics feature ANSI color highlighting, source code context carats, line/column positioning, and standard error classification codes (`E01xx` syntax, `E02xx` borrow checker, `E03xx` type errors, `E04xx` data race errors).

### 2.2 Lexer (`src/lexer/`)
- Handcrafted deterministic finite automaton (DFA) scanner.
- Zero heap allocations during token scanning; identifiers and string literals point directly into the source buffer via string views (`std::string_view`).
- Preserves token trivia and source locations for error reporting.

### 2.3 Parser and AST (`src/parser/`, `src/ast/`)
- Recursive descent parser with operator precedence Pratt parsing for binary expressions.
- Builds an Abstract Syntax Tree (AST) rooted in `TranslationUnitNode`.
- Supports exact C-style declarations (`int main()`, `float compute(float x)`), algebraic `enum`, `match` expressions, closures `(x) => expr`, and `@layout(scan)` struct annotations.

---

## 3. Semantic Analysis Engine (Sema)

The `Sema` phase (`src/sema/`) performs multi-pass semantic verification:

1. **Symbol Table & Scope Resolution** (`symbol_table.cpp`):
   Constructs lexical scope trees, resolves identifier bindings, and verifies visibility modifiers (`public` vs internal).
2. **Type Checking & Monomorphization** (`type_checker.cpp`, `monomorphizer.cpp`):
   Performs type unification, checks assignment compatibility, and monomorphizes generic functions and structs (`Result<T, E>`, `Vector<T>`).
3. **Borrow Checker** (`BorrowChecker`):
   Tracks `ProvenanceSet` roots and ensures the reader-writer borrow invariant: any number of immutable references (`&T` or `const T&`) OR exactly one exclusive mutable reference (`&mut T`).
   Performs structural escape analysis to ensure local stack addresses cannot escape through compound struct fields (`b.data`) or array indexing (`arr[0]`).
4. **Move Checker** (`MoveChecker`):
   Validates affine linear consumption: variables passed to `move(x)` are invalidated for subsequent reads. For variables moved along conditional paths, the compiler synthesizes stack-allocated drop flags. Tracks moved variable stacks across nested lexical scopes and strictly prohibits reading outer variables moved inside loop bodies (`for`, `while`) unless re-assigned within that iteration.
5. **Data Race Checker** (`DataRaceChecker`):
   Analyzes array index expressions in `parallel for` bodies to prove index injectivity $f(i) = c_1 \cdot i + c_2$ ($c_1 \ne 0$), rejecting race conditions at compile time.

---

## 4. Tamarin Intermediate Representation (TIR)

TIR (`src/ir/`) is a mid-level SSA intermediate representation designed to bridge the semantic gap between the high-level AST and low-level LLVM IR.

### 4.1 TIR Instruction Set
- **Control Flow**: `tir.branch`, `tir.cond_branch`, `tir.return`.
- **Memory Operations**: `tir.alloc`, `tir.load`, `tir.store`, `tir.move`, `tir.drop`.
- **Data Orientation**: `tir.soa_load`, `tir.soa_store` representing contiguous column vector operations for `@layout(scan)` arrays.
- **Concurrency**: `tir.task_spawn`, `tir.task_await`, `tir.parallel_for`.

### 4.2 Mid-Level Optimization Passes (`src/ir/opt/`)
- **Range Analysis** (`range_analysis.cpp`): Computes interval bounds $[L_i, U_i]$ for induction variables and array indexes across loop bodies.
- **Bounds Check Elimination** (`bounds_check_elimination.cpp`): Uses range analysis to prove array indices remain strictly within $[0, \text{Length}-1]$, eliminating redundant branch instructions.
- **Alias Analysis** (`alias_analysis.cpp`): Leverages `ProvenanceSet` guarantees to prove disjointness between memory loads and stores, enabling aggressive load forwarding and dead store elimination.

---

## 5. Backend and Code Generation

### 5.1 LLVM IR Lowering (`src/backend/llvm_codegen.cpp`)
- Converts TIR basic blocks and instructions into strongly typed LLVM IR.
- Configures target machine triples, data layouts, CPU features (e.g., `+avx2,+fma` under `--target-cpu native`), and floating-point models (`--fast-math`).
- Automatically emits metadata for loop auto-vectorization (`llvm.loop.vectorize.enable`).
- Preserves pointer return types for decayed fixed-size arrays and string slices (`isPointerValue_`), generating valid `ret ptr` opcodes rather than illegal scalar bitwidth conversions.

### 5.2 C99 Header Generation (`src/backend/c_header_generator.cpp`)
- When invoked with `--emit-header`, the backend scans all `@c_layout` structs and `@export` functions, generating an idiomatic, standards-compliant C99 header file with include guards and C ABI type definitions.

---

## 6. Runtime Library Architecture (`src/runtime/`)

Tamarin applications link against a lightweight, zero-overhead runtime (`tamarin_runtime`):

### 6.1 `PageProvider` (`src/runtime/page_provider.cpp`)
- Manages high-performance page-aligned virtual memory requests via OS APIs (`VirtualAlloc`/`VirtualFree` on Windows, `mmap`/`munmap` on POSIX).
- Maintains thread-local page caches for instant region bump-allocation without contending on OS kernel locks.

### 6.2 `ThreadTaskPool` (`src/runtime/concurrency.cpp`)
- Work-stealing thread pool configured to the number of physical CPU cores.
- Executes `parallel for` partitions across work queues using deque-based work stealing.
- Provides coroutine scheduling and lightweight fiber execution for `Task<T>`.

---

## 7. Troop Package Manager Architecture (`troop/`)

`troop` is the canonical package manager and build orchestrator for Tamarin projects.

```text
+---------------------------------------------------------------------------------+
|                            Troop Package Orchestrator                           |
+---------------------------------------------------------------------------------+
   troop.toml Manifest
          │
          ▼
   [ Manifest Parser ] ───────► TOML Semantic Deserialization (Package & Deps)
          │
          ▼
   [ Dependency Resolver ] ───► Semantic Versioning (SemVer 2.0.0)
     ├── Diamond Dependency Deduplication
     ├── Cycle Detection (Tarjan's DFS)
     └── Topological Compilation Order Construction
          │
          ▼
   [ Lockfile Generator ] ────► troop.lock (Deterministic Reproducibility)
          │
          ▼
   [ Build Runner ] ──────────► Incremental Invocation of 'tamarin'
```

### 7.1 Manifest (`troop.toml`)
Defines package metadata, compiler requirements, and dependencies:
```toml
[package]
name = "math_service"
version = "0.1.0"
authors = ["Compiler Team"]
edition = "2026"

[dependencies]
matrix_core = { path = "../matrix_core" }
```

### 7.2 Deterministic Resolver
- Resolves dependencies using Semantic Versioning (SemVer 2.0.0).
- Performs depth-first search (DFS) with topological sorting to detect cycles and build a deterministic Directed Acyclic Graph (DAG) compilation schedule.
- Emits `troop.lock` containing exact package hashes and pinned dependency paths.

### 7.3 Automated Native Dynamic Library Deployment
On Windows targets, `troop` inspects `lib-dirs` in `troop.toml` (and platform-specific sections `[target.windows]`) and automatically copies all matching native dynamic link libraries (`.dll`) to the target `bin/` directory upon build completion (`package_manager.cpp`). This eliminates runtime missing DLL errors when launching Raylib or SDL3 binaries.

### 7.4 Target-Aware Project Scaffolding
`troop new` and `troop init` support first-class project templates (`--template=raylib-game`, `--template=sdl3-app`), generating pre-configured manifests, platform linking directives, and entry point boilerplate.

---

## 8. Standard Library Architecture

The Tamarin Standard Library (`lib/std/`) is written directly in Tamarin with minimal compiler primitives:

### 8.1 Swiss Table Hash Map (`lib/std/collections/hash_map.tmn`)
- Implements a flat-array open-addressing hash table inspired by Google Abseil and Rust hashbrown.
- Uses 1-byte control bytes (`ctrl` table): `EMPTY = 128`, `DELETED = 254`, `FULL = 0..127` ($H_2$ 7-bit fingerprint).
- Employs 16-way SIMD/group probing: loads a 16-byte window and matches tags using triangular probe strides.
- Control bytes are mirrored for the first 16 bytes (`ctrl[capacity + i] = ctrl[i]`), guaranteeing bounds-safe group loads without wrapping overhead.
- Supports generic keys and values with $O(1)$ amortized lookups and an 87.5% maximum load factor.

### 8.2 Zero-Allocation JSON DOM (`lib/std/json.tmn`)
- Structural streaming tokenizer avoiding heap allocations for string tokens and object keys by emitting non-owning `JsonStringView` slices.
- Flat tape AST representation (`JsonNode nodes[]` contiguous array) where each node contains integer-indexed links to `first_child` and `next_sibling`, completely eliminating heap recursion and pointer chasing.
- High-performance query APIs: `find_field`, `get_int`, `get_float`, `get_bool`, `array_get`.

### 8.3 Hardware Bit Intrinsics (`lib/std/bits.tmn`)
- Direct lowering to LLVM target intrinsics (`llvm.ctpop`, `llvm.ctlz`, `llvm.cttz`, `llvm.bitreverse`, `llvm.bswap`).
- Emits native x86-64 instructions (`popcnt`, `lzcnt`, `tzcnt`, `bswap`) and ARM64 NEON instructions (`cnt`, `clz`, `rbit`, `rev`).

---

## 9. Automatic C Binding Generator Architecture (`bindgen/`)

`tamarin-bindgen` is an automated C header visitor built directly on `libclang`:

```text
+---------------------------------------------------------------------------------+
|                       tamarin-bindgen Translation Pipeline                      |
+---------------------------------------------------------------------------------+
   C Header (.h)
          │
          ▼
   [ libclang Clang Index & TranslationUnit ]
          │
          ▼
   [ Cursor AST Visitor & Symbol Filter ]
     ├── Struct & Union Records (Calculates Offsets & Sizes)
     ├── Enums (Sequential ADT enum vs Bitflag const int)
     ├── Typedefs & Function Pointers (*void callback prototypes)
     └── Function Declarations (extern "C" signatures)
          │
          ▼
   [ Keyword Sanitizer & Type Mapper ]
     └── Disambiguates type_, trait_, fn_, match_, auto_ collisions
          │
          ▼
   Idiomatic Tamarin Bindings (.tmn)
```

- **Type Lowering**: Automatically resolves recursive typedefs, pointer qualifiers (`*const T`, `*T`), fixed-size arrays (`T[N]`), and preprocessor `#define` integer/float constants.
- **CLI Modes**: Available as standalone `tamarin-bindgen` and compiler subcommand `tamarin bindgen`.

---

## 10. Language Server Protocol Engine (`lsp/`)

`tamarin-lsp` implements the Language Server Protocol (LSP 3.17) over standard I/O:

```text
+---------------------------------------------------------------------------------+
|                            tamarin-lsp Server Pipeline                          |
+---------------------------------------------------------------------------------+
   JSON-RPC Wire Protocol (Content-Length: <bytes>\r\n\r\n{...})
          │
          ▼
   [ Transport & Framer ] (transport.cpp)
          │
          ▼
   [ In-Memory Virtual File System (VFS) ] (vfs.cpp)
     ├── Text Buffer Snapshots (didOpen, didChange, didClose)
     └── UTF-8 Byte Offset <---> UTF-16 Line/Character Translation
          │
          ▼
   [ Document Analyzer ] (analyzer.cpp)
     ├── Lexer -> Fault-Tolerant Parser -> Sema Pipeline
     └── textDocument/publishDiagnostics (Live Error/Warning Squiggles)
          │
          ▼
   [ Language Intelligence Handlers ]
     ├── textDocument/hover (navigation.cpp)
     ├── textDocument/definition (navigation.cpp)
     ├── textDocument/references (navigation.cpp)
     ├── textDocument/documentSymbol (navigation.cpp)
     ├── textDocument/completion (completion.cpp)
     └── textDocument/signatureHelp (completion.cpp)
```

- **Fault-Tolerant Resynchronization**: Parser recovers on missing semicolons and unclosed blocks, maintaining outline symbols and hover documentation during typing.
- **Cache Invalidation**: Analysis results are cached per document URI and invalidated on incremental edits.

---

## 11. Official Editor Packaging (`editors/vscode/`)

- **VS Code Extension**: Clean TypeScript LanguageClient in `editors/vscode/src/extension.ts` that spawns `tamarin-lsp --stdio`.
- **TextMate Grammar**: `editors/vscode/syntaxes/tamarin.tmLanguage.json` scoping keywords, built-in types, `@layout` / `@c_layout` attributes, comments, numbers, and strings.
- **Theme-Adaptive File Icons**: Custom dark/light mode icons for `.tam` and `.tmn` files.

---

## 12. Verification Gauntlet & Quality Engineering

The compiler pipeline is validated via a multi-tiered test matrix orchestrated by `python tests/run_all.py`:
- **Unit & Soundness Test Suite (`tamarin_tests`)**: 603 automated unit tests, including 200 dedicated tests added in v0.18.5:
  - `MemorySafety` (50 tests): Non-lexical lifetimes, mutable borrow exclusivity, pointer provenance, reference escaping.
  - `ConcurrencySoundness` (50 tests): Data race prevention, affine loop injectivity, disjoint slicing, atomic primitives.
  - `IRLLVMSoundness` (50 tests): SSA dominance invariants, bounds check elimination (BCE), dead code elimination (DCE), pointer decay conversions.
  - `CompilerPerformance` (50 tests): Throughput scaling across lines of code, memory overhead, generic monomorphization depth.
- **Cross-Platform Verification Gauntlet**: 856 total tests executed across Windows, Linux, and macOS covering C ABI interoperability, adversarial borrow corner cases, coroutine dispatch, package management, language server protocols, and native graphics pipelines.
