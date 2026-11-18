# TAMARIN Memory Architecture (v0.13)

## 1. Architectural Philosophy

TAMARIN is a C-like, memory-safe, high-performance systems programming language. A primary design tenet of TAMARIN is:

> **TAMARIN achieves industry-leading memory efficiency and Rust-grade safety while preserving the familiar expressiveness of a C systems language without mandatory lifetime annotations or tracing garbage collection.**

Rather than forcing all objects into an undifferentiated heap or relying on runtime garbage collectors, TAMARIN implements an **Adaptive Memory Engine** consisting of six complementary storage tiers:

1. **Native Hardware Registers & Stack**: Fixed-size scalar variables, loop induction counters, small primitives, and local return frames reside directly in CPU registers or on the standard native stack frame.
2. **Stack-Allocated Closure Environments (v0.13)**: Anonymous environment structs synthesized on the caller stack (`alloca`) containing pointers to captured stack slots, passed as `ptr %env` to 16-byte fat closures (`{ ptr fn_ptr, ptr env_ptr }`) with **zero heap allocation**.
3. **Page-Backed Lazy-Commit Regions**: Scoped arenas backed by virtual memory pages allocated on demand via the target `MemoryPageProvider`. Fast monotonic bump allocation with zero syscall overhead on the fast path, bulk reclamation on region exit, and zero virtual bytes allocated for empty regions.
4. **OOM-Safe Sparse RAII Cleanup Sidecars**: Out-of-band metadata tracking destructors for non-trivial resource-owning types allocated inside regions. Pre-reserves sidecar capacity before construction and activates upon completion. Trivial objects incur zero per-object byte overhead or destructor tracking slots.
5. **Recyclable Generational Slabs (v0.6)**: Non-monotonic storage for dynamic, independently recycled objects. Managed via cache-friendly arrays with inverted free-lists and safe 64-bit generational handles (`Handle<T> = { u32 slot, u32 gen }`).
6. **Durable General-Purpose Heap Fallback**: Explicit, unconstrained allocations that must outlive lexical scopes or survive arbitrary dynamic lifetimes (`std.mem::alloc`, `Vec<T>`).

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           TAMARIN Storage Tiers                                 │
├─────────────────────┬───────────────────────────┬───────────────────────────────┤
│ Storage Tier        │ Use Cases                 │ Reclamation Model             │
├─────────────────────┼───────────────────────────┼───────────────────────────────┤
│ Hardware Registers  │ Scalar ints, floats, ptrs │ Machine registers             │
│ Native Stack        │ Local scalars, small PODs │ Native stack pointer (RSP)    │
│ Closure Environment │ Capturing lambda contexts │ Caller stack frame (alloca)   │
│ Lazy-Commit Region  │ Transient arrays, structs │ Page bulk release / rcache    │
│ Sparse RAII Sidecar │ Resource owners in region │ LIFO sidecar destructor stack │
│ Generational Slab   │ Independent object churn  │ Inverted free list / handles  │
│ Durable Heap        │ Indefinite lifetimes      │ Explicit free / RAII Vec<T>   │
└─────────────────────┴───────────────────────────┴───────────────────────────────┘
```

---

## 2. Rust-Grade Compile-Time Memory Model (v0.12)

### 2.1 Lexical Active Borrow Tracking & Aliasing-XOR-Mutability
Tamarin enforces the fundamental law of memory safety at compile time:
$$\text{Storage Access} \implies (\text{Shared Immutability}) \oplus (\text{Exclusive Mutability})$$
- `ActiveBorrow` tracking in `TypeChecker` registers live references.
- Any storage location may have any number of active `const T&` immutable borrows OR exactly one `T&` mutable borrow, but never both simultaneously.
- Direct mutations to a variable while an active borrow exists are rejected:
  ```c
  int x = 42;
  int& ref = x;
  x = 100; // COMPILE ERROR: cannot mutate 'x' while active pointer 'ref' borrows it
  ```
- Call-site double mutable aliasing (`foo(&x, &x)`) and transitive pointer aliasing through copies are strictly rejected.

### 2.2 Definite Assignment & Per-Element Initialization
- `assignedStack_` and `arrayElemAssignedStack_` track definite initialization across all control-flow paths.
- Reading an uninitialized scalar variable or uninitialized array element (`arr[i]`) is rejected at compile time.
- Variables initialized inside loops that may execute 0 times remain uninitialized in outer scopes.

### 2.3 Destructive Move Semantics & `movedStack_`
- Resource-owning types (`Vec<T>`, `String`, types declaring or inheriting destructors) cannot be copied implicitly.
- Ownership must be transferred using explicit `move(x)`.
- Semantic analysis tracks `movedStack_`, rejecting use-after-move and borrow-after-move:
  ```c
  Vec<int> v1;
  v1.init(4);
  Vec<int> v2 = move(v1);
  v1.push(10); // COMPILE ERROR: use of moved variable 'v1'
  ```

### 2.4 Algebraic Data Type Drop Glue
For enums with resource-owning variant payloads (`Result<Vec<int>, String>`):
- The compiler synthesizes deterministic drop glue.
- When an enum variable is overwritten or falls out of scope, the drop glue inspects the active variant discriminant tag and invokes the destructor of the currently held payload, guaranteeing zero memory leaks.

---

## 3. Stack-Allocated Borrow-Checked Closures (v0.13)

### 3.1 16-Byte Fat-Pointer Layout
All functions and closures in Tamarin are represented as a 16-byte aggregate:
```c
struct TamarinClosure {
    void* fn_ptr;   // Machine code entry point
    void* env_ptr;  // Caller-stack environment pointer (or null)
};
```

### 3.2 Stack Environment Allocation
When a lambda captures outer local variables:
1. `IRBuilder` synthesizes an anonymous struct type on the caller's stack frame via `alloca`.
2. Stack slot addresses of captured variables are stored into the environment struct.
3. The pointer to the environment struct (`env_ptr`) is passed as a hidden first argument to the lowered lambda function.
4. **Zero Heap Allocation**: No dynamic memory allocations occur; closures operate at the speed of native stack calls.

### 3.3 Escape Prevention via Provenance Sets
- The closure value inherits the union of all captured stack slots' `ProvenanceSet` (local stack tier and lexical scope depth).
- If `env_ptr != null`, the closure is strictly prohibited from escaping:
  - Cannot be returned from the defining function.
  - Cannot be assigned to a variable in an outer lexical scope.
  - Cannot be stored in an escaping struct or out-parameter.
- Stateless lambdas (`env_ptr == null`) and named C-style functions coerce into `{ fn_ptr, null }` and may escape freely.

---

## 4. Virtual Memory & Target Page Provider Integration

### 4.1 Address Space Reservation & Page-Granular Commitment
Tamarin connects directly to native OS virtual memory APIs (`VirtualAlloc` on Windows):
- **Reservation (`MEM_RESERVE`)**: Reserves 64 KB address blocks aligned to system allocation granularity (`0x10000`). Crucially, reservations consume **0 bytes of physical RAM** and **0 bytes of commit charge**.
- **Page-Granular Commitment (`MEM_COMMIT`)**: Commits physical memory only in 4 KB page multiples on demand. An empty region block performs **0 virtual allocations, 0 page commits, and 0 syscalls**.
- **Kernel-Guaranteed Zero-Initialization**: Freshly committed pages from the OS kernel are guaranteed zero-filled, eliminating redundant per-object `memset` loops in user space.

### 4.2 Bounded Fast Small-Region Pool (Zero Steady-State Syscalls)
- Each thread maintains an isolated pool of reusable small-region blocks (`MAX_SLOTS = 4`).
- Small regions ($\le 64$ KB) acquire and return blocks via fast O(1) pointer operations without invoking kernel syscalls.
- In steady-state loops, high-churn region allocation executes with **exactly 0 OS virtual memory calls** (`reserve = 0`, `commit = 0`, `decommit = 0`, `release = 0`).
- An aggregate memory cap (`TAMARIN_RCACHE_MEMCAP`, default 128 KB) prevents unbounded physical commit charges.

---

## 5. Recyclable Generational Slabs & Compact Handles (v0.6)

### 5.1 Generational Parity Invariant
For dynamic objects with independent lifetimes:
- Free slots hold **EVEN** generations ($0, 2, 4, ...$).
- Occupied slots hold **ODD** generations ($1, 3, 5, ...$).
- Allocation advances generation to odd; deallocation advances generation to even.
- Handle validation is a single 4-byte check: `cmp [slab.gen + slot*4], handle.gen`. Overhead is within $\le 1.10\times$ of raw pointers (~5.97 ns lookup).

### 5.2 Generational Retirement & ABA Immunity
When a slot generation counter reaches $0xFFFFFFFE$, the slot is permanently retired ($0xFFFFFFFF$) and never reused, mathematically eliminating ABA aliasing.

---

## 6. Sparse RAII Cleanup Sidecars

For non-trivial types with user-defined destructors allocated inside regions:
- Trivial types (POD structs, primitive arrays) incur **0 bytes of sidecar overhead**.
- Non-trivial objects register in an out-of-band LIFO sidecar record stack.
- Two-phase registration pre-reserves sidecar capacity before constructor execution and activates the slot only upon successful completion.
- On region exit, registered destructors execute in strict reverse-construction order before region memory is reclaimed.

---

## 7. Compile-Time Memory Telemetry & Planning

### 7.1 Static Memory Planning Report (`--emit-memory-report`)
```text
=== TAMARIN Static Memory Planning Report ===
Name            Type           Size  Align  Storage   Lifetime        Destructor      Borrows  Payload
--------------------------------------------------------------------------------------------------------------
globalVal       i32               4      4  Global    Static          None            No            4 B
localStack      i32               4      4  Stack     Lexical         None            No            4 B
regionBuf       i32[100]        400      4  Region    Region(r1)      None            No          400 B
regionRes       Resource         24      8  Region    Region(r1)      Sparse Sidecar  No           24 B
--------------------------------------------------------------------------------------------------------------
Total Static Allocations:  4
Total Region Payload:      424 bytes
Total Stack Payload:       4 bytes
Total Global Payload:      4 bytes
=============================================
```

### 7.2 Runtime Memory Telemetry (`--memory-stats`)
Passing `--memory-stats` emits high-precision allocation and paging telemetry to `stderr` on region exit:
```text
[TAMARIN MEMORY STATS] Region destroyed:
  Initial hint:                    65536 bytes
  Virtual reserved:                65536 bytes
  Committed bytes / commit charge:  8192 bytes
  Process working set (resident RAM): 3276800 bytes
  Payload handed out:               5000 bytes
  Allocations:                         2
  Chunks:                              1
  Commit operation count:              2
  Destructors:                         1
```
Telemetry clearly separates virtual reservations from commit charges and actual resident working-set RAM.
