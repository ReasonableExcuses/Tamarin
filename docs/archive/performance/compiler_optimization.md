# TAMARIN v0.5.0 — LLVM Optimization Pipeline Audit & Analysis

## 1. Introduction

The TAMARIN compiler compiles source `.tmn` code into a structured Abstract Syntax Tree (AST), performs rigorous Semantic Analysis (`sema`), lowers the AST to an instruction-based Tamarin Intermediate Representation (TIR), and translates TIR into LLVM Intermediate Representation (LLVM IR).

Finally, TAMARIN invokes LLVM's industrial optimization pipeline (`clang -x ir -O[0-3]`) and native x86-64 code generator targeting the Windows MSVC ABI (`x86_64-pc-windows-msvc`).

This document presents a technical audit of how TAMARIN's IR generation interacts with LLVM's optimization passes, evaluates the transformation pipeline across optimization levels, and audits code generation quality.

---

## 2. TAMARIN IR Lowering Architecture

### 2.1 The TIR to LLVM IR Mapping
TAMARIN's intermediate representation (TIR) uses a three-address code structure with explicit typed virtual registers (`v0, v1, v2...`).

During LLVM IR generation (`src/backend/llvm_codegen.cpp`), each TIR instruction is lowered to equivalent LLVM IR primitives:
- `Opcode::Alloca` $\rightarrow$ `%vN = alloca <type>`
- `Opcode::Load` $\rightarrow$ `%vN = load <type>, ptr %vM`
- `Opcode::Store` $\rightarrow$ `store <type> %vM, ptr %vN`
- `Opcode::BoundsCheck` $\rightarrow$ `%chk = icmp uge i64 %idx, %limit; br i1 %chk, label %err, label %ok`
- `Opcode::CallVirtual` $\rightarrow$ Load vtable pointer $\rightarrow$ GEP offset $\rightarrow$ Load method pointer $\rightarrow$ Indirect call
- `Opcode::RegionInit` $\rightarrow$ `call void @__tamarin_region_init(ptr %region, i64 %capacity)`
- `Opcode::RegionAlloc` $\rightarrow$ `call ptr @__tamarin_region_alloc(ptr %region, i64 %size, i64 %align)`
- `Opcode::RegionDestroy` $\rightarrow$ `call void @__tamarin_region_destroy(ptr %region)`
- Function Attribute `@noinline` $\rightarrow$ LLVM IR `noinline` function attribute
- Intrinsic `__f64_to_bits(d)` $\rightarrow$ `@__tamarin_f64_to_bits(double)` lowering to `bitcast double %d to i64`

### 2.2 Entry Block Alloca Hoisting
TAMARIN performs a function-level pre-pass that collects all `Alloca`, `ArrayAlloc`, and `SoAArrayAlloc` instructions and emits them exclusively in the **entry basic block** of the function:
```llvm
entry:
  %v15 = alloca i32    ; Allocated ONCE at function entry
  br label %for_body

for_body:
  store i32 0, ptr %v15 ; Only value reset; zero dynamic stack growth
```

#### Significance of Entry Alloca Hoisting:
1. **Guaranteed Bounded Stack at -O0**: Eliminates all risk of stack overflow in deep or long-running loops without optimization.
2. **Unlocks LLVM Mem2Reg**: LLVM's canonical `Mem2Reg` pass explicitly requires alloca instructions to reside in the function's entry block. Non-entry allocas cannot be promoted directly to SSA virtual registers. Hoisting allows `Mem2Reg` to immediately turn stack slots into SSA registers `%1, %2`, enabling vectorization and constant propagation.

### 2.3 Memory Regions Lowering & Arena Optimization
In v0.5.0, TAMARIN implements lexical memory regions. The region descriptor `%__tamarin_region` (containing current chunk pointer and head chunk pointer) is allocated on the stack via entry hoisting.
- `RegionAlloc` lowers to a call to `@__tamarin_region_alloc(ptr %region, i64 %size, i64 %align)`. In LLVM IR, the returned pointer is typed as `ptr` (opaque pointer), seamlessly integrating with all downstream GEP, load, and store optimizations.
- The fast path of `__tamarin_region_alloc` performs an inlined offset alignment and bump comparison; LLVM's inliner at `-O2` and `-O3` optimizes calls and recognizes invariant pointer arithmetic, avoiding system call overhead.
- `RegionDestroy` guarantees complete bulk freeing on all scope exit paths, early returns, breaks, and continues.

### 2.4 `@noinline` Attribute Control
TAMARIN supports the `@noinline` attribute on function declarations. This instructs the LLVM code generator to attach the `noinline` attribute to the LLVM function definition, preventing compilers from inlining calls or devirtualizing polymorphic calls when dynamic dispatch overhead is being measured.

### 2.5 IEEE-754 Bitcast Intrinsic (`__f64_to_bits`)
In v0.4.2, TAMARIN introduced `__f64_to_bits(double val) -> u64`, lowering directly to:
```llvm
define i64 @__tamarin_f64_to_bits(double %d) {
entry:
  %0 = bitcast double %d to i64
  ret i64 %0
}
```
This is a zero-cost bit reinterpretation that compiles directly to `movq %xmm0, %rax` on x86-64 without memory store/load round-trips or precision loss.

### 2.5 Monotonic Timer Boundary Alignment & Frequency Pre-Fetch
In `@__tamarin_benchmark_start` and `@__tamarin_benchmark_elapsed_ms`, the compiler emits runtime helpers that call `QueryPerformanceFrequency` first, validate `freq > 0` (exiting with code 1 if invalid), and query `QueryPerformanceCounter` immediately before and after the timed kernel. Frequency lookup is excluded from the timed interval.

### 2.6 Iterative 3-State Cycle-Safe Graph Traversal
In v0.4.2, `Type::hasNonTrivialDestructor()` was rewritten to use an iterative depth-first graph traversal with an explicit heap-allocated stack frame vector and 3-state White/Gray/Black coloring. This eliminated all arbitrary depth caps (`vList.size() > 128`), preventing false-positive destructor classification on deeply nested trivial types (depths 70, 129, 256, 512+) while maintaining cycle safety and zero host stack recursion.

---

## 3. Analysis of LLVM Optimization Passes Across Levels

### 3.1 Unoptimized Mode (`-O0`)
- **Purpose**: Fast compilation, maximum debuggability.
- **Pass Pipeline**: Minimal. Only mandatory ABI and target layout transformations are applied.
- **Characteristics**:
  - Every local variable resides in a stack slot.
  - Every read performs a memory `load`; every assignment performs a `store`.
  - Bounds checks are evaluated dynamically on every array access.
  - Generates full frame pointers and unhoisted loops.

### 3.2 Basic Optimization (`-O1`)
- **Key Passes Active**:
  - `Mem2Reg`: Promotes all entry stack allocas to SSA virtual registers.
  - `SimplifyCFG`: Cleans up redundant basic blocks and jump chains.
  - `EarlyCSE`: Eliminates trivial common subexpressions.
- **Performance Impact**: Yields the largest single-step speedup in the compiler (up to 7.8x speedup over `-O0`), converting memory-bound stack traffic into pure CPU register operations.

### 3.3 Standard Optimization (`-O2`)
- **Key Passes Active**:
  - `GVN` (Global Value Numbering): Redundant load elimination and value forwarding across basic blocks.
  - `IndVarSimplify` & `LoopStrengthReduce`: Canonicalizes loop induction variables and replaces expensive indexing with pointer increments.
  - `LoopVectorize` & `SLPVectorizer`: Emits 128-bit/256-bit SIMD vector instructions where memory safety and alignment are proven.
  - `Inliner`: Inlines small leaf functions and flattens trivial abstractions.
  - `CorrelatedValuePropagation`: Eliminates provably redundant bounds checks when loop indices stay within known array ranges.

### 3.4 Aggressive Optimization (`-O3`)
- **Key Passes Active**:
  - Aggressive loop unrolling and interleaving.
  - Deep SIMD vectorization with higher vectorization cost thresholds.
  - More aggressive function inlining.

---

## 4. Empirical Optimization Scaling in TAMARIN v0.4.2

Progression of TAMARIN execution latency from unoptimized (`-O0`) to aggressive (`-O3`):

| Workload | -O0 (ms) | -O1 (ms) | -O2 (ms) | -O3 (ms) | Speedup (-O3 vs -O0) | Primary LLVM Optimization Mechanism |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`01_integer`** | 13.47 ms | 4.48 ms | 4.89 ms | 4.84 ms | **2.78x** | `Mem2Reg` + constant propagation in Collatz & hash |
| **`02_float`** | 23.42 ms | 7.26 ms | 7.00 ms | 7.02 ms | **3.34x** | AVX2 vectorization of matrix multiplication |
| **`03_arrays`** | 17.26 ms | 3.62 ms | 2.45 ms | 2.47 ms | **6.99x** | Bounds check elimination + loop unrolling |
| **`04_structures_aos`** | 10.62 ms | 1.37 ms | 0.95 ms | 0.95 ms | **11.12x** | Vectorization of coordinate updates |
| **`04_structures_soa`** | 8.75 ms | 1.38 ms | 0.38 ms | 0.39 ms | **22.64x** | Contiguous SIMD auto-vectorization (`vmovups`, `vaddps`) |
| **`05_function_calls`** | 30.66 ms | 15.08 ms | 15.97 ms | 15.96 ms | **1.92x** | Tail recursion optimization & stack frame elision |
| **`06_branching`** | 15.20 ms | 10.57 ms | 10.64 ms | 10.64 ms | **1.43x** | Branch conversion to conditional moves (`cmov`) |
| **`07_virtual_dispatch`** | 17.49 ms | 7.70 ms | 8.16 ms | 8.14 ms | **2.15x** | Direct call inlining + vtable pointer caching |
| **`08_memory_ops`** | 17.63 ms | 2.09 ms | 1.90 ms | 1.91 ms | **9.24x** | 64-bit integer word operations & vector memcmp |
