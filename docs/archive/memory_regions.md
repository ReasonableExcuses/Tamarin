# TAMARIN Memory Regions & Monotonic Arena Architecture

> **Context**: This technical report specifies the lexical region syntax, monotonic arena engine, and sidecar destruction lifecycle within the broader [TAMARIN Memory Architecture (v0.13)](memory_architecture.md).

## Overview

TAMARIN introduces **lexical memory regions** backed by an **Adaptive Memory Engine** with page-granular lazy commitment and sparse RAII destructor tracking.

In high-performance systems programming—such as game-engine per-frame calculations, physics simulation loops, particle systems, compiler intermediate representations, batch pipelines, and short-lived scratch data—traditional general-purpose heap allocators (`malloc`/`free`) incur substantial overhead:
- Heap synchronization locks across threads.
- Per-allocation metadata overhead (chunk headers, free-list pointers).
- Heap fragmentation from varied allocation sizes and lifetimes.
- High destruction latency when thousands of temporary objects must be individually freed.

TAMARIN memory regions solve this with an explicit lexical construct that eliminates individual per-object deallocation while preserving compile-time memory safety without requiring Rust-style lifetime syntax annotations.

In v0.5.4, regions feature:
1. **Target Page Provider Integration**: Dynamic OS page querying, 64 KB reservation blocks, and 4 KB on-demand physical page commitments.
2. **Deterministic Object Construction & Destruction Order**: Array elements and aggregate members construct in declaration order (`0..N-1`) and destroy in reverse order (`N-1..0`).
3. **OOM-Safe Two-Phase Sidecar Registration**: Pre-reserves sidecar destructor slots before constructor execution, activating only upon successful completion.
4. **Adaptive Sidecar Sizing**: Initial sidecar chunk holds 4 records (184 bytes), expanding geometrically up to 128 records.
5. **Bounded Reservation Reuse**: Reuses up to 2 reservations with 0 physical commit charge between region scopes, reducing kernel syscall overhead on high-churn regions.

---

## 1. Syntax & Semantics

### Lexical Region Construct

A region is introduced using the `region` keyword:

```c
int main() {
    region scratch(65536) {
        int values[1000];

        for (int i = 0; i < 1000; i++) {
            values[i] = i * i;
        }

        println(values[500]);
    } // Backing memory reclaimed in bulk here

    return 0;
}
```

### Syntax Variations

1. **Named Region with Explicit Capacity**:
   ```c
   region frame(131072) {
       // 128 KB initial capacity
   }
   ```
2. **Named Region with Default Capacity**:
   ```c
   region scratch {
       // Default 65,536 bytes (64 KB) capacity
   }
   ```
3. **Anonymous Region with Default Capacity**:
   ```c
   region {
       // Anonymous region with 64 KB capacity
   }
   ```

---

## 2. Object Lifecycle & Destructor Sidecars

### 2.1 Construction-Before-Destruction Invariant

TAMARIN v0.5.4 enforces an absolute lifecycle guarantee:

> **No destructor obligation is registered or executed against an object unless that object's constructor has successfully finished execution.**

- **Class Arrays**:
  ```c
  region r {
      Token tokens[4]; // Construct tokens[0], [1], [2], [3] in order
  } // Destroy tokens[3], [2], [1], [0] in reverse order
  ```
  The compiler generates sequential constructor calls for each element index, followed by activating the pre-reserved batch sidecar record with `count = 4` and `stride = sizeof(Token)`.
- **In-Place Array Initialization**:
  ```c
  region r {
      Token tokens[3] = { Token(1), Token(2), Token(3) };
  }
  ```
  Each constructor call evaluates directly into the allocated element storage, ensuring non-copyable resource-owning objects are initialized without intermediate copies or heap indirection.
- **Aggregates with Non-Trivial Members**:
  For composite types with non-trivial fields, member default constructors are invoked in declaration order before executing enclosing constructor logic, and member destructors run in reverse declaration order.

### 2.2 OOM-Safe Two-Phase Sidecar Registration

To eliminate the OOM window where an object is constructed but sidecar allocation fails, TAMARIN implements a two-phase protocol:

1. **`RegionReserveDestructor`**: Allocates and reserves an inactive slot in the sidecar chunk *before* constructor execution begins.
2. **Constructor Execution**: Runs element or object constructors.
3. **`RegionActivateDestructor`**: Activates the slot with valid object pointers, counts, and strides *after* construction succeeds.

If sidecar allocation fails in phase 1, execution aborts to `%oom` before resources are acquired, ensuring no unmanaged resource handles leak.

---

## 3. Allocator Runtime Hardening

The monotonic bump allocator runtime in the LLVM backend (`llvm_codegen.cpp`) includes defensive validation against invalid parameters and arithmetic overflows:

1. **Alignment Verification**:
   - `align` must be a power of two (1, 2, 4, 8, or 16). Invalid alignments trigger an immediate OOM abort.
2. **Capacity Range Validation**:
   - Region capacities are verified in `(0, INT64_MAX - 1048576]`.
3. **Arithmetic Overflow Defenses**:
   - Alignment rounding: checks `aligned_used < cur_used` for integer overflow.
   - Required size: checks `needed < aligned_used` for integer overflow.
   - Chunk geometric doubling: checks `(cur_cap << 1) < cur_cap` before doubling chunk capacity.
4. **Metadata Cleanup**:
   - If backing memory allocation fails in the slow path, allocated chunk metadata is freed before exiting via `@__tamarin_region_oom_error`.
5. **Exact Diagnostic Write Length**:
   - `@__tamarin_str_region_oom` string length write is exactly 44 bytes, eliminating diagnostic truncation.
6. **Scope-Safe Provenance Metadata (Binding IDs)**:
   - Each variable declaration receives a unique monotonic `BindingId` from `SymbolTable`.
   - `bindingProvenance_` and `bindingFieldProvenance_` tables in `TypeChecker` use `BindingId` as keys, ensuring that variable shadowing in nested scopes never overwrites outer scope provenance.
7. **Strict Interprocedural Borrow Forwarding**:
   - Passing arguments to unknown external functions checks `hasNonStaticBorrowOrRegion()`, safely rejecting `ParameterBorrow`, `ThisBorrow`, and `Unknown` alongside `LocalRegion`.
8. **Constant-Expression Array Sizing**:
   - Arrays sized with compile-time constant expressions are evaluated by `evalIntConstExpr()` and stored in `evaluatedArraySize`.

---

## 4. Control Flow & Cleanup Guarantees

Regions integrate directly with TAMARIN's intermediate representation (TIR) scoped cleanup stack:

- **Early Return**: Returning from inside a region automatically emits `Opcode::RegionDestroy` before returning, running active sidecar destructors in reverse order and reclaiming all backing chunks.
- **Break & Continue**: Exiting a loop enclosing a region, or executing `break` / `continue` from within an inner region, correctly executes sidecar destructors and reclaims the region's arena.
- **Nested Regions**: An inner region allocates from its own arena and is reclaimed when the inner scope exits. The outer region remains valid and unaffected.
- **Loop Semantics**:
  - A region *inside* a loop (`for (...) { region r { ... } }`) initializes and destroys a fresh arena on every iteration, providing perfect bounded memory usage.
  - A region *surrounding* a loop (`region r { for (...) { ... } }`) monotonically accumulates allocations across iterations and bulk-reclaims after the loop concludes.

---

## 5. Benchmark Performance (Workload 10: `10_region_alloc`)

The TAMARIN benchmark suite includes `10_region_alloc`, which simulates 5,000 iterations of high-frequency temporary object allocation, nested regions, and chunk expansion:
- 5,000 outer regions (64 KB capacity) allocating particle arrays (`Particle[100]`), intermediate scratch buffers (`int[200]`), and sustained chunk-growth arrays (`int[18000]` = 72 KB > 64 KB).
- 5,000 inner nested regions (16 KB capacity) allocating scratch calculation buffers (`int[50]`).
- 10,000 total regions created, expanded, and bulk-reclaimed.

### Benchmark Results Comparison

| Configuration | Compiler / Runtime | Median Time (ms) | Status | Exact Checksum |
| :--- | :--- | :--- | :--- | :--- |
| `tamarin_o0` | Tamarin v0.5.4 (Debug, -O0) | 51.03 ms | **VERIFIED** | 135693757500 |
| `tamarin_o1` | Tamarin v0.5.4 (Optimized, -O1) | 44.60 ms | **VERIFIED** | 135693757500 |
| `tamarin_o2` | Tamarin v0.5.4 (Release, -O2) | 42.90 ms | **VERIFIED** | 135693757500 |
| `tamarin_o3` | Tamarin v0.5.4 (Max Opt, -O3) | 43.15 ms | **VERIFIED** | 135693757500 |
| `c_clang_o2` | Clang 23.1.5 (-O2) | 4.62 ms | **VERIFIED** | 135693757500 |
| `c_clang_o3` | Clang 23.1.5 (-O3) | 4.56 ms | **VERIFIED** | 135693757500 |
| `c_msvc_o2`  | MSVC 19.44 (/O2) | 22.34 ms | **VERIFIED** | 135693757500 |
| `rust`       | Rustc 1.99.1 (Release) | 26.37 ms | **VERIFIED** | 135693757500 |

### Key Architectural Takeaways

1. **Kernel Syscall Realism**: Unlike C and Rust which rely entirely on user-mode heap caching (`malloc`/`free`), TAMARIN regions interface directly with native virtual memory pages. Bounded reservation reuse caches up to 2 reservations (with 0 bytes physical commit charge), reducing high-churn latency from 182 ms to 42.9 ms.
2. **Chunk Expansion Handled Safely**: The sustained growth payload forces multiple chunks to be reserved, committed, linked, and decommitted cleanly across all iterations.
3. **Exact Result Equivalence**: All toolchains produce 100% bit-for-bit identical results:
   `PARTICLES: 124999750000; SCRATCH: 6816132500; NESTED: 3877875000; CHECKSUM: 135693757500`.
