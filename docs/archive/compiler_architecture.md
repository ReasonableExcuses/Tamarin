# TAMARIN Compiler Architecture (v0.13)

## 1. Multi-Stage Pipeline Overview

```
Source (.tmn)
     │
     ▼
 1. [Driver & CLI]           ─── Options (-O0..-O3, --static, --dynamic, --emit-header, --clang, --emit-tir)
     │
     ▼
 2. [SourceManager]          ─── File buffering, source spans, diagnostic caret formatting
     │
     ▼
 3. [Lexer]                  ─── Tokenizes source text; keywords (parallel, reduction, task_scope, etc.)
     │
     ▼
 4. [Parser]                 ─── Recursive descent; C-style functions, inline lambdas ((x) => ...),
     │                           parallel loops, reductions, structured tasks, traits, match, enums
     ▼
 5. [AST & ModuleGraph]      ─── Typed AST; multi-module dependency resolution with 3-color DFS cycle check
     │
     ▼
 6. [Sema: Declarations]     ─── Type registration, C ABI boundary validation, struct layout, vtables
     │
     ▼
 7. [Borrow & Lifetime]      ─── Active borrow tracking (aliasing-XOR-mutability), definite initialization,
     │                           destructive move analysis (movedStack_), capture analysis & escape prevention
     ▼
 8. [TypeChecker & Generics] ─── Static monomorphization, static trait dispatch, enum drop glue synthesis
     │
     ▼
 9. [TIR IRBuilder]          ─── Lowers AST to Typed Tamarin IR (TIR); synthesizes stack closure environments;
     │                           outlines parallel loops & reductions into worker functions;
     │                           short-circuit CFG; in-place array/struct indexing; custom [] lowering
     ▼
10. [TIR Opt Pass Pipeline]  ─── Domain-Aware TIR PassManager (-O1..-O3):
     │                           - RangeAnalysis (Interval algebra, canonical induction proof)
     │                           - BoundsCheckElimination (replaces safe checks with Nop)
     │                           - AliasAnalysis (memory disjointness, noalias proofs)
     ▼
11. [LLVMCodeGen]            ─── Lowers optimized TIR to clean LLVM IR:
     │                           - 16-byte fat closure lowering ({ ptr fn, ptr env })
     │                           - In-register reduction accumulators with atomicrmw merge
     │                           - Native SIMD vector types (<4 x float>, <8 x float>) & intrinsics
     │                           - Hardware atomic instructions (AtomicLoad/Store/RMW/CmpXchg)
     │                           - Cold bounds error branch weighting (!prof !1)
     │                           - Linkonce_odr linkage for COMDAT deduplication
     ▼
12. [C Header Generator]     ─── Validates C ABI conformity and emits C99 headers (--emit-header)
     │
     ▼
13. [LLVM Verification]      ─── Verifies textual LLVM IR via backend LLVMVerifier
     │
     ▼
14. [Clang & Linker]         ─── Clang code generation with -O0..-O3 pass pipelines;
                                 links against tamarin_runtime.lib to Windows x86-64 executable:
                                 - Dynamic CRT (--dynamic, ucrtbase.dll) -> 28.5 KB binary
                                 - Static CRT (--static, libcmt.lib) -> 181.0 KB self-contained binary
```

---

## 2. Key Architectural Components

### 2.1 Scope-Safe Variable Bindings & Provenance
- Unified scoped binding structure:
  ```cpp
  struct Binding {
      ValueId ptr = InvalidValue;
      const sema::Type* type = nullptr;
      std::optional<ValueId> arrayLenValueId;
      int64_t constantArrayLen = 0;
      bool isConst = false;
      bool isParameter = false;
  };
  ```
- Managed via `std::vector<std::unordered_map<std::string, Binding>> scopeStack_`.
- When entering a block, `pushScope()` creates a new lexical frame. When exiting, `popScope()` removes all bindings in that frame, restoring outer shadowed variables with their original storage pointer, type, and array length.

### 2.2 Active Borrow Tracking & Aliasing-XOR-Mutability (v0.12)
- In `src/sema/type_checker.cpp`, lexical active borrows are tracked via `activeBorrows_`:
  ```cpp
  struct ActiveBorrow {
      std::string variableName;
      bool isMutable;
      SourceLocation loc;
      int scopeDepth;
  };
  ```
- Enforces aliasing-XOR-mutability: a storage location can have either any number of active `const T&` immutable borrows OR exactly one `T&` mutable borrow, but never both simultaneously.
- Detects and rejects call-site double mutable aliasing (`foo(&x, &x)`), mutating while borrowed, and transitive aliasing through pointer copies.

### 2.3 Definite Assignment & Fine-Grained Array Initialization
- `TypeChecker` maintains a scope stack of variable initialization states: `std::vector<std::unordered_map<std::string, bool>> assignedStack_` and fine-grained per-element array maps `arrayElemAssignedStack_`.
- Detects and rejects reads of uninitialized scalar variables or array elements (`arr[i]`) during semantic analysis.
- Assignments inside loops that may execute 0 times are isolated to prevent leakage to enclosing scopes.

### 2.4 Destructive Move Semantics & ADT Drop Glue (v0.12)
- Enforces explicit `move(x)` for ownership transfer of resource-owning types (`Vec<T>`, `String`, types with destructors).
- Maintains `movedStack_` across lexical scopes and control flow branches, rejecting use-after-move and borrow-after-move.
- In `IRBuilder`, synthesizes deterministic drop glue for tagged union algebraic data types (`Option<T>`, `Result<T, E>`), automatically examining the active discriminant tag and calling the appropriate destructor when reassigned or exiting scope.

### 2.5 Stack-Allocated Borrow-Checked Capturing Closures (v0.13)
- **16-Byte Fat-Pointer Layout**: Function types `fn(ArgTypes...) -> ReturnType` and lambdas `(Type arg) => expr` are represented as `{ ptr fn_ptr, ptr env_ptr }`.
- **Environment Synthesis**: For capturing lambdas, `IRBuilder` synthesizes an anonymous stack struct in the caller frame (`alloca`) containing pointers to captured stack slots, passing `ptr %env` as a hidden first argument to the lowered lambda function.
- **Escape Prevention**: The closure value inherits the union of all captured stack slots' `ProvenanceSet`. Returning a capturing closure (`env_ptr != null`), storing it in an escaping struct, or assigning it to an outer scope is rejected at compile time. Stateless closures (`env_ptr == null`) may escape freely.
- **Uniform Coercion**: Stateless lambdas and named C-style functions coerce into `{ fn_ptr, null }` with zero runtime branching.

### 2.6 Strict C ABI Boundary Enforcement (v0.13)
- `checkCAbiType` in `TypeChecker` inspects all signatures of `@export` functions, `extern "C"` declarations, and `@c_layout` struct fields.
- Rejects `TypeKind::Function` at these boundaries (`"closure type '...' has a 16-byte fat-pointer layout and cannot cross C ABI boundaries directly"`), preventing 16-byte closures from crossing into 8-byte C function pointer interfaces.
- Generates clean C99 headers via `CHeaderGenerator` (--emit-header).

### 2.7 Domain-Aware TIR Pass Subsystem (v0.9)
- Modular pass pipeline in `src/ir/opt/` with `PassManager`, `TIRFunctionPass`, and `TIRModulePass`.
- **RangeAnalysis**: Symbolic interval algebra calculating induction bounds across canonical loops (`for (int i = 0; i < len(arr); i++)`).
- **BoundsCheckElimination (BCE)**: Converts provably safe runtime bounds checks to no-ops at compile time.
- **AliasAnalysis**: Proves disjointness across distinct stack frames, regions, and references, emitting LLVM `noalias` metadata.

### 2.8 Native SIMD Vectorization Pipeline (v0.9)
- Native vector types (`float4`, `float8`, `int4`, `int8`, `int16`) lower to LLVM native vector types `<4 x float>`, `<8 x float>`, etc.
- Emits `@llvm.vector.reduce.fadd` and `@llvm.vector.reduce.add` intrinsics, mapping directly to native SSE, AVX, AVX2, and AVX-512 vector instructions.

### 2.9 Structured Concurrency & Parallel Reductions (v0.10–v0.11)
- **Thread Pool**: Zero-allocation work-partitioning thread pool in `src/runtime/concurrency.cpp`.
- **Worker Outlining**: Outlines `parallel for` loop bodies into separate LLVM worker functions with stack-allocated context packing (`alloca [K x ptr]`).
- **In-Register Accumulators**: Reduction variables allocate thread-local accumulators initialized to algebraic identities, promoted to SSA registers via `mem2reg` with zero false sharing, followed by atomic reduction at chunk exit (`atomicrmw`).

### 2.10 Adaptive Memory Engine: Regions & Generational Slabs (v0.5–v0.8)
- **Lazy-Commit Regions**: Monotonic arenas backed by virtual memory pages (`VirtualAlloc(MEM_RESERVE)` and on-demand `MEM_COMMIT`). Bounded thread-local cache pools deliver zero steady-state syscalls.
- **Generational Slabs**: Recyclable object storage returning 64-bit compact generational handles (`{ u32 slot, u32 gen }`). Odd/even parity invariants and permanent ABA retirement provide memory safety without garbage collection.
- **Sparse RAII Sidecars**: Tracks non-trivial destructors in regions, executing cleanups in reverse LIFO order upon region scope exit.

### 2.11 Monomorphic Generics & Trait Specialization (v0.8)
- Fully specialized at compile time with zero runtime boxing or pointer indirection.
- Emitted with LLVM `linkonce_odr` linkage for COMDAT deduplication across compilation units.

### 2.12 Cold Bounds Branch Profiling & Inlining Strategy
- Marks `@__tamarin_bounds_error()` as `cold noreturn nounwind` with branch weights `!prof !1` (1 : 1,048,576).
- Keeps cold error paths out of hot loop instruction cache lines, unlocking aggressive LLVM loop unrolling and auto-vectorization.

### 2.13 Dual Binary Linkage Architecture (v0.12)
- `--dynamic` (default): Links against native Windows Universal C Runtime (`ucrtbase.dll`), yielding compact **28.5 KB** binaries.
- `--static`: Links against `libcmt.lib` with zero external DLL dependencies, producing self-contained **181.0 KB** binaries.
