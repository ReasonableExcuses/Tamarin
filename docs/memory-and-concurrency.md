# Tamarin Memory Safety and Concurrency Model

**Version:** 0.18.5  
**Classification:** Formal Semantic Safety Specification  

---

## 1. Overview and Design Invariants

Tamarin achieves memory safety and race-freedom entirely at compile time without a garbage collector, tracing engine, or runtime reference-counting overhead. The model is built on three orthogonal mechanisms:
1. **Lexical and Non-Lexical Provenance Analysis** (`ProvenanceSet`, `ActiveBorrow`).
2. **Affine Destructive Moves and Drop Flags** (`move(x)` linear consumption).
3. **Affine Injectivity Loop Analysis** (compile-time data-race prevention on parallel regions).

---

## 2. Borrow Verification and Provenance Tracking

### 2.1 Pointer Provenance (`ProvenanceSet`)
Every reference (`&T` or `&mut T`) is statically assigned a provenance set $P \subseteq \mathcal{R}$, where $\mathcal{R}$ denotes the set of abstract memory roots:
$$\mathcal{R} = \text{StackSlots} \cup \text{HeapAllocations} \cup \text{RegionRoots} \cup \text{ParameterBases}$$

When a reference is derived via address-of `&var` or pointer arithmetic, the compiler records its provenance root:
- References derived from the same root share provenance and are treated as potentially aliasing.
- References derived from disjoint roots are proven non-aliasing ($P_1 \cap P_2 = \emptyset$).

### 2.2 The Active Borrow Invariant
At every point in the Control Flow Graph (CFG), the compiler tracks active borrows $\mathcal{B} = \{(r, \text{kind}, \text{root})\}$:
- **Multiple Readers**: Any number of immutable borrows (`&T` or `const T&`) may coexist for a given root:
  $$\forall b \in \mathcal{B}, \quad \text{root}(b) = r \implies \text{kind}(b) = \text{Shared}$$
- **Exclusive Writer**: A mutable borrow `&mut T` requires total exclusivity. No other borrow of root $r$ may exist:
  $$\exists b \in \mathcal{B} \text{ s.t. } \text{root}(b) = r \land \text{kind}(b) = \text{Exclusive} \implies |\mathcal{B}_r| = 1$$

Violations trigger compile-time diagnostic errors (e.g., `E0201: cannot borrow 'x' as mutable because it is also borrowed as immutable`).

### 2.3 Non-Escaping Lifetime Rules
References cannot outlive the lexical scope of their provenance root:
1. **Structural Local Reference Escape Prevention**: Returning a reference to a local stack slot is prohibited at compile time. This check applies structurally through member access expressions (`b.data`) and array indexing (`arr[0]`), ensuring compound wrappers and containers cannot be used as an indirection channel to leak stack slot addresses.
2. **Struct Lifetime Containment**: Storing a reference into a struct with longer lifetime is rejected unless lifetime bounds are strictly satisfied.
3. **Non-Lexical Lifetimes (NLL)**: Lifetimes end at the point of last use, unlocking the base variable immediately once all borrows are dead.

---

## 3. Move Semantics and Destructive Consumption

Tamarin treats non-trivially-copyable resources (such as dynamic vectors, file handles, synchronization primitives, and heap-allocated objects) as affine linear types.

### 3.1 Destructive Moves (`move(x)`)
Invoking `move(x)` transfers ownership of the resource to the target destination and invalidates variable `x` in the current lexical environment:
```tamarin
Vector<int> v1 = Vector<int>::create();
v1.push(42);

Vector<int> v2 = move(v1); // v1 is now consumed

// Compile error: Use of moved variable 'v1'
// int val = v1.get(0); 
```

### 3.2 CFG Definite Assignment and Drop Flags
When a variable is moved inside a conditional branch (e.g., `if (cond) { process(move(v)); }`), its liveness state at the merge point is conditionally uninitialized:
1. The compiler synthesizes a hidden 1-byte boolean drop flag on the stack frame (`i1 %v.drop_flag`).
2. At the branch site where the move occurs, `%v.drop_flag` is set to `0`.
3. At the exit of the enclosing scope, the destructor check emits:
   ```llvm
   %flag = load i1, ptr %v.drop_flag
   br i1 %flag, label %do_drop, label %skip_drop
   ```
4. If static dataflow analysis proves the variable is unconditionally moved along all paths, the drop check and flag are eliminated entirely ($0$ runtime overhead).

### 3.3 Nested Scopes and Loop Affine Invariants
1. **Lexical Scope Isolation**: Variables moved within nested inner blocks remain marked moved upon exiting those blocks. The compiler tracks a lexical stack (`movedStack_`), preventing variable resurrection or use-after-move across scope boundaries.
2. **Loop Iteration Soundness**: When an outer variable is moved inside a `for` or `while` loop, subsequent iterations of the loop would execute against an invalidated, consumed state. Tamarin strictly rejects moving loop-external variables inside loop bodies unless the variable is re-assigned an initialized value within that same iteration before any subsequent read.

---

## 4. Affine Injectivity and Data-Race Freedom

Data races in concurrent code occur when two execution threads access the same memory location simultaneously and at least one access is a write.

### 4.1 Parallel Loops (`parallel for`)
Tamarin eliminates data races in `parallel for` constructs via static affine index injectivity:
```tamarin
parallel for (int i = 0; i < N; i++) {
    output[i] = input[i] * 2; // Proven disjoint
}
```

### 4.2 Mathematical Injectivity Invariant
For an array write `A[f(i)]` inside a `parallel for` over iteration domain $I$:
$$\forall i_1, i_2 \in I, \quad i_1 \ne i_2 \implies f(i_1) \ne f(i_2)$$

1. **Affine Linear Indexing**: Index expressions of the form $f(i) = c_1 \cdot i + c_2$ with constant $c_1 \ne 0$ are proven strictly injective. The compiler guarantees disjoint memory partitions for all concurrently running worker threads.
2. **Rejection of Ambiguous / Non-Injective Accesses**:
   - Indirect indexing `A[indices[i]] = ...` cannot be proven injective at compile time and is rejected inside `parallel for` unless guarded by atomic primitives or thread-local accumulation.
   - Shared scalar mutations inside the loop body without synchronization are rejected:
     ```tamarin
     int counter = 0;
     parallel for (int i = 0; i < N; i++) {
         counter += 1; // Compile error: Mutable cross-thread alias of 'counter'
     }
     ```

---

## 5. Region Arenas (`region`)

For high-throughput allocations (such as AST passes, frame-based rendering, or packet parsing), Tamarin provides scoped page-backed bump arenas.

### 5.1 Syntax and Semantics
```tamarin
region FrameArena {
    Node* n1 = FrameArena.alloc(sizeof(Node));
    Node* n2 = FrameArena.alloc(sizeof(Node));
    // Bulk operations...
} // FrameArena completely resets or unmaps all pages at scope exit
```

### 5.2 Zero Individual Free Overhead
1. Within a `region` block, allocations are bump-pointer operations (`ptr += aligned_size`), requiring approximately 3-4 CPU instructions.
2. At block termination, all backing pages are returned to the `PageProvider` cache in a single pointer reset operation.
3. The borrow checker enforces that no pointer allocated within `FrameArena` can escape past the region's closing brace.
