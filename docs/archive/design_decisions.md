# TAMARIN Design Decisions & Ambiguity Resolution

This document records architectural and semantic decisions made during the implementation of the TAMARIN compiler (v0.1.0, v0.2.0, v0.2.1, and v0.3.0), referencing the authoritative specification document.

## 1. Integer Semantics
- **Decision**: `int` is strictly 32-bit signed two's complement (`i32`), `long` is 64-bit (`i64`), `short` is 16-bit (`i16`), `char`/`byte` is 8-bit unsigned (`u8`).
- **Rationale**: Specification Section 5.2 explicitly defines fixed-width meanings for familiar C names independent of host platform conventions.
- **Arithmetic**: Wrapping modulo 2^N for `+`, `-`, `*`. Trapping for division by zero and signed `MIN / -1`.
- **Hotfix v0.2.1**: Unsigned division and remainder lower to `udiv` and `urem`. Shift operations explicitly select arithmetic shift (`ashr`) for signed types and logical shift (`lshr`) for unsigned types. 128-bit integers (`i128`/`u128`) are rejected during semantic analysis until 128-bit ABI lowering is implemented.

## 2. Array Views and Fat Pointers
- **Decision**: A function parameter `int values[]` is lowered to a bounded view representation `{ ptr, i64 }` carrying both base address and element length.
- **Rationale**: Specification Sections 8.7, 30.2, and 36.2 forbid raw C pointer decay. Bounded views must preserve length.
- **Hotfix v0.2.1**: `len()` returns `size_t` (`u64`/`usize`). Element accesses use the actual element type for GEP and memory loads/stores rather than hardcoded 32-bit words.

## 3. Array Allocation Safety (Hotfix v0.2.1)
- **Decision**: Non-constant array-size expressions (VLAs) are rejected at compile time. Negative array sizes (both negative integer literals and unary negations) are rejected at compile time. Unsupported sizes are never represented with `-1`.
- **Decision**: Empty arrays `T[0]` allocate `[0 x elemTy]` on the stack, not 1 element. Array declarations allocate exactly the requested logical dimension, and storage is zero-initialized via `@llvm.memset.p0.i64`. Excess initializer elements are strictly rejected at compile time.

## 4. Scope-Safe Variable Bindings (Hotfix v0.2.1)
- **Decision**: Replaced separate variable pointer, type, and length lookup tables with a single scoped `Binding` structure pushed and popped atomically across lexical scopes.
- **Rationale**: Prevents inner variable shadowing from corrupting or mutating outer variable metadata. An outer variable shadowed by an inner variable of a different type or array length has its original binding restored upon exiting the inner scope.

## 5. Definite-Assignment Analysis & Uninitialized Reads (Hotfix v0.2.1)
- **Decision**: Semantic analysis performs definite-assignment tracking for all local scalar variables. Reading a variable before it has been definitely assigned on all incoming execution paths is rejected at compile time.
- **Decision**: As a defense-in-depth safety strategy, local stack allocations in TIR/LLVM lowering are explicitly zero-initialized (`store <zero>`).

## 6. Control-Flow Correctness & Short-Circuiting (Hotfix v0.2.1)
- **Decision**: Logical operators `&&` and `||` lower to branching control-flow graphs ensuring strict short-circuit evaluation. The right-hand operand is not evaluated if the left-hand operand determines the result.
- **Decision**: `break` and `continue` lower to loop exit and increment targets. Using `break` or `continue` outside a loop is rejected during semantic analysis. `switch` statements are intercepted with an informative diagnostic until full lowering is complete.

## 7. LLVM Toolchain Configuration & Verification (Hotfix v0.2.1)
- **Decision**: The TAMARIN compiler invokes the external Clang driver for verification, optimization, and code generation. The path to Clang is configurable via CMake (`TAMARIN_CLANG_PATH`), environment variable (`TAMARIN_CLANG`), or CLI flag (`--clang <path>`).
- **Decision**: `LLVMVerifier::verifyIR` writes temporary files with process IDs and atomic counters to eliminate file-locking races on Windows during concurrent verification calls.

## 8. Struct Memory Layout & Alignment (Milestone v0.3)
- **Decision**: Struct layout strictly adheres to the standard Windows x86-64 ABI:
  - Primitive field alignments: `bool`/`i8`/`u8` = 1, `i16`/`u16` = 2, `i32`/`u32`/`f32` = 4, `i64`/`u64`/`f64`/pointer = 8.
  - Struct alignment is `max(field.alignment)` across all members (minimum 1).
  - Each field's byte offset is naturally aligned to its own alignment constraint. Padding bytes are inserted automatically before unaligned members.
  - Total struct size is rounded up to a multiple of the struct's overall alignment.
- **Decision**: Layout directives `@c_layout` and `@layout(point)` map to standard contiguous Array-of-Structures (AoS) layouts matching MSVC/Clang layout rules. `@layout(scan)` is validated syntactically and semantically for SoA memory optimization.
- **Decision**: Recursive struct definitions containing an instance of the struct itself directly by value are rejected with a compile-time error during 2-pass semantic registration.

## 9. Member Initializers & Compound Literals (Milestone v0.3)
- **Decision**: C-style compound literals `(Type){ ... }`, designated field initializers `{ .x = 1.0f, .y = 2.0f }`, and positional initializers `{ 1.0f, 2.0f }` are fully supported for struct initialization.
- **Decision**: In TIR and LLVM lowering, struct compound initializers allocate an alloca slot, store the evaluated member expressions via `GetFieldPtr` GEP instructions, and return the loaded aggregate value.
- **Decision**: Nested braced initializers within struct initializers (e.g. `Rect r = { { 0, 0 }, { 10, 20 } };`) are disambiguated and converted from inner positional array initializers to typed nested compound initializers during semantic analysis.

## 10. Symbol Table Scope Stability (Milestone v0.3)
- **Decision**: In `SymbolTable`, `scopes_` is implemented using `std::deque<std::unordered_map<std::string, Symbol>>` rather than `std::vector`.
- **Rationale**: Reallocations of an underlying vector container when `pushScope()` is called would move existing maps and invalidate existing raw pointers to `Symbol` instances in MSVC debug builds. `std::deque` guarantees stable references to elements across container insertions and removals.

## 11. Object-Oriented Layout & Subobject Composition (Milestone v0.3.5)
- **Decision**: Single inheritance uses inline subobject composition. Base class fields are prepended in declaration order starting at offset 0 (or offset 8 if a vtable pointer is present).
- **Rationale**: Guarantees zero-cost upcasting to a base reference or pointer (`Derived*` to `Base*`) with identical base byte offsets.
- **Decision**: Derived constructors automatically invoke base constructors before derived field initializers.

## 12. Dynamic Virtual Dispatch & VTable Architecture (Milestone v0.3.5)
- **Decision**: Polymorphic classes (classes declaring or inheriting at least one `virtual` method) prepend an implicit 64-bit vtable pointer `_vptr` at field index 0.
- **Decision**: Global vtables (`@_vtable_<Class>`) are emitted as constant arrays of function pointers `[N x ptr]`. Constructors store the corresponding class vtable address into `_vptr` upon object construction.
- **Decision**: Virtual calls emit `Opcode::CallVirtual`, loading `_vptr`, indexing the predetermined virtual method slot via GEP, loading the function pointer, and performing an indirect call with `this` as the first argument.

## 13. RAII Destructors & Scope Cleanup Architecture (Milestone v0.3.5)
- **Decision**: Classes declaring a destructor `~ClassName()` register instances with `IRBuilder::cleanupScopes_`.
- **Decision**: Destructors execute deterministically in strict reverse order of construction upon block scope exit (`}`), early `return`, or loop control jumps.
- **Rationale**: Provides automatic deterministic resource management (closing files, releasing locks, clearing memory) without garbage collection overhead or manual `free()` calls.

## 14. References & Borrow Semantics (Milestone v0.3.5)
- **Decision**: References `T&` and `const T&` are lowered to pointers (`ptr`) in LLVM IR with indirect load/store semantics on access.
- **Decision**: Non-const references can only bind to lvalues (variables, field accesses, array elements). Binding a temporary rvalue to a non-const reference is rejected during semantic analysis.
- **Decision**: Returning a reference to a local stack-allocated variable is detected and rejected at compile time.

## 15. Structure-of-Arrays (SoA) Storage & Addressing Restrictions (Milestone v0.3.5)
- **Decision**: `@layout(scan)` struct arrays allocate a contiguous bundle `{ [N x F0], [N x F1], ... }` where each field is stored in an independent array of size `N`.
- **Decision**: Field access `p[i].field` emits `Opcode::SoAGetFieldPtr` indexing the specific field array directly.
- **Decision**: Because individual logical struct instances do not exist contiguously in memory, attempting to take the address of an SoA element (`&p[i]`) or bind a reference to an SoA element (`T& ref = p[i];`) is strictly rejected at compile time.

## 16. In-Place Array-of-Structures Element GEP Lowering (Milestone v0.3.5)
- **Decision**: Array indexing on AoS aggregates `p[i]` in lvalue or method call contexts emits `Opcode::GetArrayElemPtr`, performing an inbounds GEP directly into the array's backing storage.
- **Rationale**: Prevents copying entire structs to temporary stack slots via `ArrayLoad` + `Alloca`, avoiding stack overflows during large loops and ensuring member mutations `p[i].x += dx;` occur directly in-place.

## 17. Static Module System & Import Syntax (Milestone v0.3.6)
- **Decision**: Imports are strictly compile-time static declarations (`import <path>;` or `import <path> as <alias>;`). No runtime module loader overhead.
- **Decision**: The default namespace alias is the final dotted component of the module path (e.g. `import math.vector;` creates namespace `vector`). Explicit aliases via `as` allow renaming (e.g. `import math.vector as vec;`).
- **Decision**: Imports must appear before any ordinary declarations in a file. Conflicting aliases report a diagnostic; identical duplicate imports are safely deduplicated.

## 18. Module Identity, Search Roots & Path Resolution (Milestone v0.3.6)
- **Decision**: Dotted module paths map deterministically to subdirectories and filenames (`math.vector` -> `math/vector.tmn`).
- **Decision**: Path components cannot contain path traversal (`..`), forward slashes, or backslashes.
- **Decision**: Search roots are resolved with precedence:
  1. Directory containing the importing source file.
  2. CLI include directories (`-I <path>`).
  3. `TAMARIN_PATH` environment variable (semicolon-delimited on Windows).
- **Decision**: Source files are canonicalized and cached in `ModuleGraph` so each file is loaded, lexed, and parsed at most once regardless of how many times it is imported across a dependency graph.

## 19. Dependency Graph, Cycle Detection & Topological Ordering (Milestone v0.3.6)
- **Decision**: A 3-color (White/Gray/Black) DFS traversal detects dependency cycles.
- **Decision**: Circular dependencies are rejected at compile time with a detailed diagnostic chain showing the cycle path (`a.tmn imports b.tmn, b.tmn imports a.tmn`).
- **Decision**: Modules are topologically ordered so dependencies are analyzed, checked, and lowered before their consumers. The entry module is always placed last.

## 20. Cross-Module Access Control, Canonical Types & Name Mangling (Milestone v0.3.6)
- **Decision**: Declarations are private to their module by default. Only declarations marked with `public` can be referenced across module boundaries via `<alias>::<name>`.
- **Decision**: Cross-module types share a single canonical identity in `TypeContext` with deterministic naming (`<module_prefix>_<Type>`), ensuring layout offsets, methods, constructors, destructors, and vtables operate identically across modules.
- **Decision**: Function symbols in imported modules are mangled with their module prefix (`<module_prefix>_<fn>`), while `main` and functions in the root module remain unmangled (`@main`, `@fn`) for single-file and C ABI compatibility.

## 15. Standard I/O, Prelude Builtins & Standard Modules (TAMARIN v0.4)

### Context
Beginner programs and simple scripts in TAMARIN should be straightforward and C-like (`println("Hello, TAMARIN!");`), without forcing the user to write import headers for basic console I/O. At the same time, structured multi-module systems organize their dependencies cleanly (`import std.io;`, `import std.file;`, `import std.math;`).

### Decision
1. **Prelude Builtins**: `println()`, `print()`, `getInput()`, and procedural file operations (`openFile`, `closeFile`, `readFile`, `writeFile`, `appendFile`, `fileExists`, `removeFile`) are registered directly as prelude builtins in semantic analysis and code generation. They work in any source file out of the box without requiring `import std.io;`.
2. **Standard Library Modules**: Shipped under `lib/std/`:
   - `lib/std/io.tmn` (`std.io`)
   - `lib/std/file.tmn` (`std.file`)
   - `lib/std/math.tmn` (`std.math`)
   Calling `io::println()`, `io::print()`, `io::getInput()`, `io::File`, `file::write()`, `math::abs()` works cleanly and routes to the exact same high-performance runtime helpers.
3. **Standard Library Auto-Discovery**: The compiler driver automatically discovers `<project_root>/lib` (and candidates via `TAMARIN_STDLIB_DIR`, `<compiler_dir>/lib`, `<cwd>/lib`) and injects it into module search roots. Users never have to pass `-I lib` manually to access standard modules.
4. **String Literals as Array Views**: Static string literals (`"..."`) are emitted as global constant byte arrays `@.str.N` with proper LLVM character escaping, and evaluate as `{ ptr, i64 }` bounded array views (`char[]`). They seamlessly support length inspection (`len(str)`), indexing (`str[i]`), and bounds checking.
5. **RAII File Handling**: Object-oriented file handling is implemented via `class File`, featuring explicit methods (`open`, `close`, `isOpen`, `read`, `write`, `writeLine`, `flush`) and an automatic deterministic RAII destructor `~File()` that guarantees file descriptors are closed upon scope exit and early return without resource leaks.

---

## 21. String Literal Immutability & Const Array Views (Milestone v0.3.7)
- **Decision**: String literals strictly evaluate as `const u8[]` (`const char[]`). In `sema::Type`, array and array-view types carry an `isConst` attribute.
- **Rationale**: String literal data is placed in read-only data sections (`.rdata`). Mutating string literal elements (e.g. `"hello"[0] = 'x'`) produces access violation crashes at runtime in un-hardened compilers.
- **Rule**: Assignments to index expressions of string literals or `const` array views are rejected at compile time. Mutable array views implicitly widen to `const` views for argument passing, but `const` views cannot decay to mutable views.

## 22. Format-String Safety & Direct Length-Aware Console I/O (Milestone v0.3.7)
- **Decision**: Lowered `print(str)` and `println(str)` to direct Windows CRT `_write(1, ptr, len)` calls using length-aware write semantics rather than `puts()` or `printf()`.
- **Rationale**: Passing user input strings through `printf(ptr)` enables format string exploits (`%s`, `%n`, `%p`). Furthermore, `puts()` stops at the first embedded null byte (`\0`), which violates TAMARIN's bounded-length view semantics `{ ptr, i64 }`. `_write(1, ptr, len)` guarantees exact length output and eliminates format-string vulnerabilities completely.

## 23. Safe Caller-Owned Buffer Console Input (Milestone v0.3.7)
- **Decision**: Introduced the caller-owned buffer input pattern `readLine(char buffer[], int maxBytes)` / `getInput(char buffer[], int maxBytes)`.
- **Rationale**: Returning an internal static or heap buffer from `getInput()` creates hidden lifetime bugs or race conditions. With a caller-owned buffer:
  - The caller controls allocation lifetime and capacity.
  - The runtime helper `@__tamarin_get_input_buf` safely bounds reading to `min(maxBytes - 1, len(buffer) - 1)`.
  - Input lines are automatically stripped of trailing CRLF and null-terminated.
  - On EOF, the helper returns `-1`.

## 24. Non-Copyable Resource Semantics for Classes with Destructors (Milestone v0.3.7)
- **Decision**: Any class declaring or inheriting a destructor (`~ClassName()`, e.g. `std.file::File`) is classified as a unique resource owner.
- **Rule**: Copy initialization (`File b = a;`), assignment (`b = a;`), and passing by value to functions or methods are rejected at compile time during semantic analysis.
- **Rationale**: Without full C++-style move semantics or borrow checking, shallow copying an object with an RAII destructor causes both copies to invoke the destructor on scope exit, leading to double-close and double-free errors. Forcing reference passing (`File& f`) guarantees single ownership and deterministic cleanup.

## 25. Portable Executable-Relative Standard Library Search Roots (Milestone v0.3.7)
- **Decision**: Removed hardcoded development path `"E:/Projects/Tamarin/lib"` from `src/driver/driver.cpp`. Replaced with portable Windows API resolution using `GetModuleFileNameW`.
- **Rationale**: The compiler discovers its standard library portably relative to wherever `tamarin.exe` is installed (`<exeDir>/../lib` and `<exeDir>/lib`), as well as respecting `TAMARIN_STDLIB_DIR` and current working directory paths.

---

## 26. Effective Resource Ownership & Synthesized Destructor Semantics (Milestone v0.3.8)
- **Decision**: Implemented `Type::hasNonTrivialDestructor()` to compute effective resource ownership recursively across:
  1. Directly declared destructors (`Type::destructor != nullptr`).
  2. Inherited base class destructors (`baseClass->hasNonTrivialDestructor()`).
  3. By-value member fields requiring destruction (`field.type->hasNonTrivialDestructor()`).
  4. Nested aggregate members transitively owning resources.
- **Decision**: Synthesize implicit destructors for intermediate classes and structs that contain or inherit resource owners without requiring empty user-defined destructors.
- **Order of Destruction**:
  1. Execute derived user-defined destructor body, if present.
  2. Destroy resource-owning member fields in strict reverse declaration order.
  3. Execute base-class destructor invocation through the complete inheritance chain.
- **Bug Fix**: Eliminated duplicate cleanup registration in `IRBuilder::lowerVarDecl` and removed improper cleanup registration for reference types (`T&`). Cleanups are registered strictly once, after initialization.
- **Control Flow**: Recorded loop entry scope depths in `LoopTarget`; loop control statements (`break`, `continue`) emit all intervening cleanups up to the loop scope before jumping.

## 27. Safe CRT Path Interoperability & Bounded View Translation (Milestone v0.3.8)
- **Decision**: Bounded string views `{ ptr, i64 }` cannot be passed directly to CRT functions (`fopen`, `remove`) without guaranteeing a null terminator at `ptr[len]`.
- **Implementation**: Created `@__tamarin_view_to_cstr({ ptr, i64 })`:
  1. Validates base pointer is non-null and length is within valid bounds (`0 < len <= 65536`).
  2. Allocates a temporary heap buffer of `len + 1` bytes via `malloc`.
  3. Copies bytes from source view to temporary buffer while checking for embedded null bytes (`\0`). If an embedded null is found, frees memory and returns `null` immediately to prevent truncation attacks.
  4. Writes null terminator at index `len` and returns pointer.
  5. File helpers (`__tamarin_file_open`, `__tamarin_read_file`, etc.) free all temporary heap buffers on every execution branch before returning.

## 28. Unified Length-Aware Console Output Architecture (Milestone v0.3.8)
- **Decision**: Unified all console output (`print`, `println`) to route exclusively through a low-level write sink `@__tamarin_write_stdout` backed by Windows CRT `_write(1, ...)`.
- **Rationale**: Mixing buffered stdio (`printf`, `puts`, `putchar`) with unbuffered low-level writes (`_write`) causes unpredictable output interleaving and reordering on Windows.
- **Scalar Formatting**: Formatted scalars (integers, floats, booleans) write into stack-allocated buffers using `snprintf` with `%d` for 32-bit signed integers and are output through `@__tamarin_write_stdout`.
- **Chunking & Partial Writes**: Arbitrary 64-bit lengths are processed in chunks of up to 64KB with retry loops tracking partial write progress to prevent truncation or infinite loops.

## 29. Caller-Owned Input with Line Truncation Discard (Milestone v0.3.8)
- **Decision**: Hardened `@__tamarin_get_input_buf` to consume and discard any remaining characters on the line when input exceeds buffer capacity.
- **Rationale**: When a user line exceeds the caller's buffer, leftover bytes previously corrupted subsequent read operations. Discarding the truncated overflow ensures subsequent reads start cleanly at the next logical line.
- **Return Values**:
  - `> 0`: characters read into buffer, excluding newline, null-terminated.
  - `0`: empty line read.
  - `-1`: EOF reached before any input.
  - `-2`: line truncated (buffer filled to capacity - 1, null-terminated, remaining line consumed).
- **Line Endings**: Handles CRLF (`\r\n`), lone CR (`\r`), LF (`\n`), and EOF safely.
- **Legacy Zero-Argument getInput()**: Retained for backward compatibility as returning a temporary view into shared static storage; compiler emits a deprecation warning at compile time.

## 30. Return-by-Value Rejection for Resource Owners (Milestone v0.3.8)
- **Decision**: Returning a resource-owning type by value is strictly rejected during semantic analysis in `TypeChecker::checkStmt(ReturnStmt&)`.
- **Rationale**: TAMARIN's return lowering evaluates the returned expression and then executes cleanups for all local variables in scope before returning. Returning a resource-owning local variable by value would destroy the resource before the caller receives it, causing immediate use-after-free or double-free defects. Until safe move semantics or ownership-transfer syntax is introduced, resource owners must be passed and manipulated by reference (`T&`).

## 31. Two-Mode Benchmark Architecture & Defined Modular Semantics (Milestone v0.4.1)
- **Decision**: Separated performance measurement into Mode A (internal QPC computational timing) and Mode B (external wall-clock startup timing).
- **Rationale**: External process startup on Windows (5–7 ms) completely distorted microbenchmark kernels taking 0.4–2.5 ms. Separating internal QPC timing isolates pure compute throughput while measuring startup in an isolated suite.
- **Arithmetic Safety**: Replaced undefined signed 64-bit integer overflow in C benchmarks with modular unsigned wrapping arithmetic (`uint64_t`), matching Tamarin and Rust wrapping semantics.

## 32. Iterative 3-State Graph Traversal for Destructors (Milestone v0.4.2)
- **Decision**: Removed arbitrary recursion depth limits (`vList.size() > 128`) from `Type::hasNonTrivialDestructor()` and replaced with an iterative depth-first graph traversal using an explicit heap-allocated stack frame vector and 3-state White/Gray/Black coloring.
- **Rationale**: The previous 128-level limit caused false-positive non-trivial destructor classifications on deep trivial types at depths 129+, triggering unnecessary cleanup registrations. The iterative graph traversal handles arbitrary depths (70, 129, 256, 512+) with $O(1)$ shared-subgraph memoization, zero host recursion, and safe cycle detection.

## 33. IEEE-754 Bitcast Intrinsic & Exact Multi-Field Schema Validation (Milestone v0.4.2)
- **Decision**: Introduced `__f64_to_bits(double) -> u64` lowering to LLVM `bitcast double %d to i64` (`movq %xmm0, %rax`), and overhauled `benchmarks/runner.py` with exact field schemas.
- **Rationale**: 4-decimal float approximations concealed low-order mantissa differences. Converting double-precision values to exact IEEE-754 64-bit integers verified 100% bit-for-bit equivalence across Clang, MSVC, Rust, and Tamarin. Requiring full field schemas for every workload prevents false-positive aggregate matches.

## 34. Timer Alignment, Pre-Fetched Frequency, and Boundary Isolation (Milestone v0.4.2)
- **Decision**: Aligned timer execution order across all languages to call `QueryPerformanceFrequency` first, validate `freq > 0`, and query `QueryPerformanceCounter` immediately before and after the timed kernel.
- **Rationale**: Querying frequency inside or between counter reads polluted start timestamps with OS query latency. Hoisting array storage allocation and zero-initialization memsets (`is_prime[20000]`) outside the timer boundary ensured zero setup pollution in the measured computational interval.

## 35. Lexical Memory Regions and Monotonic Arena Architecture (Milestone v0.5.0)
- **Decision**: Introduced scoped `region [name] [(capacity)] { ... }` blocks with default 64 KB capacity or compile-time positive integer constant expressions.
- **Rationale**: Bulk memory reclamation avoids individual `free()` overhead and eliminates heap fragmentation for frame-based, simulation, and batch workflows.

## 36. Explicit Semantic Provenance Model & Separation from Type Identity (Milestone v0.5.1)
- **Decision**: Replaced single integer `regionId` tracking with a multi-origin semantic provenance model (`ProvenanceKind`, `ProvenanceOrigin`, `ProvenanceSet`).
- **Rationale**: Type identity must remain separate from value provenance. Two variables can have identical `int[]` types while referencing storage with different lifetimes. Encoding ownership in source types would introduce complicated lifetime syntax, whereas tracking provenance in semantic metadata preserves clean C-like syntax.

## 37. Function Parameter Escape Analysis & Borrow Summaries (Milestone v0.5.1)
- **Decision**: Reference, view, and aggregate parameters receive symbolic `ParameterBorrow` origins during function definition analysis.
- **Rationale**: Storing borrowed parameters into globals or outer lifetimes is rejected at compile time regardless of eventual caller. Centralized `FunctionBorrowSummary` tracks whether parameters escape, enabling safe temporary borrowing (`sum(int values[])`) without rejecting non-escaping helper calls.

## 38. Method Receiver & Receiver-Field Provenance (Milestone v0.5.1)
- **Decision**: Implicit and explicit `this` is tagged with `ThisBorrow`. Methods returning references or views derived from `this` propagate receiver provenance to caller expressions.
- **Rationale**: Prevents method calls from discarding receiver origins. Assigning a receiver or receiver-derived field to a global or outer lifetime is rejected at compile time.

## 39. Rejection of Structure-of-Arrays in Region Storage (Option B) (Milestone v0.5.1)
- **Decision**: Declaring `@layout(scan)` SoA arrays inside regions is strictly rejected during semantic analysis with an informative compile-time diagnostic.
- **Rationale**: Option B was selected over silently allocating SoA arrays on the stack or implementing multi-array region allocation. Region allocation promises bulk reclamation of its contents; silently falling back to stack allocation would deceive users about memory location and limits. Requiring contiguous AoS (`@layout(point)`) or explicit stack allocation enforces semantic honesty.

## 40. Monotonic Allocator Runtime Hardening & Overflow Defenses (Milestone v0.5.1)
- **Decision**: Hardened `@__tamarin_region_init` and `@__tamarin_region_alloc` in `llvm_codegen.cpp` with power-of-two alignment checks (1, 2, 4, 8, 16), capacity range validation, integer overflow checks on alignment rounding and chunk doubling, metadata freeing on allocation failure, and corrected OOM message write length (45 bytes).
- **Rationale**: Guarantees deterministic, defensive behavior under adversarial capacities or memory exhaustion.

## 41. Cross-Language Workload 10 Integrity & Pointer Stability (Milestone v0.5.1)
- **Decision**: Hardened C benchmark baseline (`10_region_alloc.c`) to use linked chunks instead of `realloc`, preserving pointer stability matching Tamarin's monotonic arena. Hardened Rust baseline (`10_region_alloc.rs`) with sound memory encapsulation and per-allocation zeroing.
- **Rationale**: Ensures all three benchmark implementations perform identical algorithmic work, allocate with equivalent semantics, and produce identical bit-for-bit checksums without undefined behavior or unsoundness.

## 42. Scope-Safe Provenance Metadata & Unique Binding Identifiers (Milestone v0.5.2)
- **Decision**: Replaced flat identifier-keyed variable provenance storage with `BindingId`-keyed maps (`bindingProvenance_` and `bindingFieldProvenance_`). `SymbolTable::declare()` assigns a monotonically increasing 64-bit integer ID to each symbol instance, and `TypeChecker` keys all provenance tables by this binding ID.
- **Rationale**: Lexical variable shadowing in nested blocks previously overwrote provenance for identical variable names. When the inner scope popped, the outer variable was left with corrupted or missing provenance metadata. Binding IDs isolate each declaration's semantic lifetime completely.

## 43. Strict Interprocedural Borrow Forwarding to Unverified Functions (Milestone v0.5.2)
- **Decision**: Reject passing any argument with `hasNonStaticBorrowOrRegion()` provenance (`ParameterBorrow`, `ThisBorrow`, `LocalRegion`, `Unknown`) to external functions or callee stubs lacking verified no-escape contracts.
- **Rationale**: Checking only `LocalRegion` permitted borrowed parameters and method receivers to be forwarded to unknown external C libraries, which could retain the pointers past caller region destruction.

## 44. Method Receiver Escape Analysis & Receiver Return Provenance (Milestone v0.5.2)
- **Decision**: Extended escape analysis to check method receivers in `checkMethodCall()`. Calling an external or unverified method on an aggregate containing borrowed storage is rejected if the receiver might escape. Methods returning references or views derived from `this` propagate receiver provenance to caller expressions.
- **Rationale**: Method invocations have implicit receiver arguments (`this`). Treating the receiver as un-checked permitted storage leaks through method mutation or return value disconnection.

## 45. Iterative Gray/Black Cycle Detection for Borrow-Containing Types (Milestone v0.5.2)
- **Decision**: Rewrote `Type::canContainBorrow()` using an iterative depth-first search with Gray (visiting) and Black (visited) state coloring, explicitly skipping internal `_vptr` fields.
- **Rationale**: Recursive aggregate types caused recursive stack overflow in the compiler. Cycle detection guarantees termination on arbitrary recursive type graphs, while ignoring `_vptr` avoids falsely classifying polymorphic classes without borrowable fields as borrow-containing.

## 46. Evaluation of Constant-Expression Array Dimensions for Region Allocation (Milestone v0.5.2)
- **Decision**: Expanded `evalIntConstExpr` to evaluate bitwise operators (`<<`, `>>`, `&`, `|`, `^`) and added `evaluatedArraySize` to `VarDeclStmt` and `GlobalVarDecl`. Plumbed `evaluatedArraySize` into `IRBuilder::lowerVarDecl()`.
- **Rationale**: `IRBuilder::lowerVarDecl()` previously only recognized `IntLiteralExpr` for array bounds, falling back to 0 bytes for constant expressions like `4 * 1024` or `1 << 12`. Region allocations allocated 0-byte arrays, causing immediate runtime OOM when data was written.

## 47. Allocator Initial Capacity Range Verification & Direct OOM Branching (Milestone v0.5.2)
- **Decision**: Validated that requested initial region capacity in `@__tamarin_region_init` is strictly positive (`capacity > 0`) and does not exceed maximum allowable allocation size (`capacity <= MAX_INT64 - CHUNK_HEADER_SIZE`). Invalid capacities branch directly to the runtime `%oom` error block.
- **Rationale**: A silent fallback to 64 KB masked programmer errors (e.g. allocating `region r(-100)`), while unchecked capacity values could trigger undefined behavior in memory allocation syscalls.

## 48. Checked 64-Bit Cumulative Counter Addition in Region Statistics (Milestone v0.5.2)
- **Decision**: Implemented checked 64-bit addition with overflow branch detection when updating region cumulative statistics (`new_r_used`, `new_r_res`, `new_r_u2`).
- **Rationale**: High-volume, long-lived regional workloads could theoretically overflow 64-bit cumulative byte counters, leading to negative usage metrics or diagnostic crashes.

## 49. Exact Standard Error Write Length for Region Diagnostics (Milestone v0.5.2)
- **Decision**: Corrected the UTF-8 error string length passed to the low-level write syscall in `@__tamarin_region_oom_error` to exactly 44 bytes.
- **Rationale**: The previous 45-byte write emitted a trailing null byte (`\0`) after the newline to stderr, corrupting downstream log parsing and automated test capture pipes.

## 50. Parity and Fairness in Cross-Language Region Benchmark Allocation (Milestone v0.5.2)
- **Decision**: Updated C and Rust benchmark implementations to faithfully replicate Tamarin's chunked monotonic arena behavior: C includes defensive NULL checks and capacity bounds; Rust implements a custom chunked arena with dynamic linked chunk growth and per-allocation zero-initialization.
- **Rationale**: Comparing Tamarin's dynamically expanding linked-chunk arena against a single fixed-size 64 KB Rust buffer was structurally unfair. Structuring all three implementations to solve the identical problem under identical memory models maintains benchmark integrity.

## 51. Conservative Defaults for Unknown External Function Summaries (Milestone v0.5.2)
- **Decision**: External functions without explicit contract definitions default to `BorrowEffect::Unknown` for all parameters and receivers.
- **Rationale**: In the absence of an interprocedural escape summary, the compiler must assume that any pointer or view passed to an external callee may be retained across the boundary.

## 52. Page-Backed Lazy-Commit Regions vs. Fixed Heap Arenas (Milestone v0.5.3)
- **Decision**: Transitioned the region storage runtime from fixed heap blocks (`malloc`) to virtual memory page-backed allocation using OS address space reservation (`MEM_RESERVE`) and page-granular on-demand commit (`MEM_COMMIT`). Empty regions perform zero virtual allocations, zero physical page commits, and zero system calls.
- **Rationale**: Pre-allocating 64 KB heap chunks on every region entry caused severe physical commit charge bloat, especially for empty regions or short-lived small buffers. Page-granular commitment ensures TAMARIN consumes physical memory strictly proportional to handed-out payloads.

## 53. Sparse RAII Sidecar Cleanup vs. Fat Object Headers (Milestone v0.5.3)
- **Decision**: Implemented an out-of-band Sparse RAII Sidecar to manage destructors of non-trivial objects inside regions. Destructor records are registered in a LIFO execution stack in external sidecar chunks, while trivial objects (POD structs, integer arrays) incur 0 bytes of header or registry overhead. Fixed-size arrays register a single 40-byte batch record.
- **Rationale**: Forcing all objects in a region to carry object headers or destructor slots imposes a permanent memory and cache tax on high-performance data arrays. A sparse sidecar delivers deterministic LIFO destructor cleanup for resource-owning types with zero penalty for trivial objects.

## 54. Kernel Page Zero-Initialization vs. Explicit Userland Memset (Milestone v0.5.3)
- **Decision**: Leveraged the OS kernel guarantee that newly committed pages from virtual memory address reservations are zero-initialized, eliminating redundant userland `memset` calls on fresh pages.
- **Rationale**: Executing redundant zeroing loops on freshly mapped virtual memory wastes CPU cycles and thrashes CPU cache lines, reducing throughput in allocation-heavy regional workloads.

## 55. Declarative Static Memory Reporting & High-Precision Telemetry (Milestone v0.5.3)
- **Decision**: Added compiler-level static memory planning reporting (`--emit-memory-report`) and runtime telemetry emission (`--memory-stats`) with exact byte-level tracking for hints, virtual reservations, physical commits, payload hand-outs, allocation counts, chunk links, page transitions, and destructor registrations.
- **Rationale**: Systems programmers require deterministic visibility into storage placement (stack vs. region vs. global) and physical page commitment dynamics to optimize high-performance, memory-constrained applications.

## 56. Construction-Before-Destruction Invariant & Order Alignment (Milestone v0.5.4)
- **Decision**: Established a strict language-level invariant: a destructor obligation is never registered or executed for an object unless that object's construction successfully completed first. For arrays of class objects (`Token tokens[N]`), elements are default-constructed sequentially in declaration order (`0..N-1`) and destroyed in reverse order (`N-1..0`). Explicit array initializers evaluate in-place without copies. Composite aggregates construct members in declaration order before running enclosing constructor bodies and clean them up in reverse order.
- **Rationale**: Invoking a destructor on uninitialized or partially-initialized memory leads to undefined behavior, corrupted resource handles, and invalid pointer dereferences.

## 57. OOM-Safe Two-Phase Destructor Sidecar Registration (Milestone v0.5.4)
- **Decision**: Separated sidecar destructor management into two distinct phases: `RegionReserveDestructor` (which secures sidecar slot capacity before constructor execution begins) and `RegionActivateDestructor` (which activates the slot with valid object pointers, counts, and strides only after construction succeeds).
- **Rationale**: In v0.5.3, registering destructors after construction created an OOM window: if sidecar chunk allocation failed, the process terminated while holding acquired resources without registered cleanup metadata. Pre-reserving the slot ensures that any OOM condition aborts safely before object construction runs.

## 58. Native Memory Page Provider Runtime Integration (Milestone v0.5.4)
- **Decision**: Connected the LLVM IR code generator directly to the native `MemoryPageProvider` runtime interface (`@__tamarin_page_reserve`, `@__tamarin_page_commit`, `@__tamarin_page_decommit`, `@__tamarin_page_release`) with 4 KB page commitment and 64 KB reservation granularity. Fallback provider semantics are documented as eager-commit on reservation.
- **Rationale**: Replaces ad-hoc hardcoded virtual allocation logic with an extensible target abstraction while ensuring generated code aligns strictly with operating system page and allocation granularities.

## 59. Honest Memory Telemetry: Distinguishing Commit Charge from Resident RAM (Milestone v0.5.4)
- **Decision**: Renamed misleading telemetry metrics: `"Physical committed"` was changed to `"Committed bytes / commit charge"` and `"Page commits"` to `"Commit operation count"`. Added telemetry reporting for alignment padding, unused committed bytes, and virtual reservation counts. Static memory reporting clearly separates storage tier provenance from reference/view types.
- **Rationale**: In virtual memory operating systems, commit charge represents pagefile-backed allocation commitments, not resident physical RAM (working set). Accurate terminology prevents false equivalence in memory reports.

## 60. Bounded Region Virtual Reservation Caching (Milestone v0.5.4)
- **Decision**: Implemented a bounded cache of up to 2 region virtual address reservations. Upon region destruction, active chunks are decommitted to 0 bytes physical commit charge via `@__tamarin_page_decommit` before being retained in cache slots.
- **Rationale**: High-churn loops creating tens of thousands of short-lived regions incurred heavy Windows NT kernel syscall overhead from repeated `VirtualAlloc(MEM_RESERVE)` and `VirtualFree(MEM_RELEASE)` calls. Caching reservations eliminates kernel address-space manipulation overhead while maintaining zero physical commit charge between region scopes.

## 61. Adaptive Sidecar Sizing and Capacity Growth (Milestone v0.5.4)
- **Decision**: Replaced the fixed 32-record (1,304-byte) sidecar chunk with an adaptive geometric capacity starting at 4 records (184 bytes), expanding geometrically ($4 \to 8 \to 16 \to 32 \to 64 \to 128$) up to 128 records per chunk under sustained pressure.
- **Rationale**: Typical region scopes contain only 1 to 4 resource-owning objects. Allocating 1,304 bytes on the first object wasted up to 96.9% of sidecar memory. The adaptive model reduces sidecar overhead by 85.9% for small regions while reducing system allocation count for large regions.

## 62. Sound Internal Mutability in Rust Allocator Reference (Milestone v0.5.4)
- **Decision**: Replaced the unsound Rust reference benchmark implementation (which cast shared `&self` to `&mut Self` to mutate chunk vectors) with a sound abstraction using `std::cell::UnsafeCell<ArenaState>`.
- **Rationale**: Unchecked mutation of shared references in Rust violates Rust's aliasing contract and undefined behavior rules. Using `UnsafeCell` provides a defensible, sound benchmark reference with verified interior mutability.

## 63. Thread-Local Isolation of Reservation Caches (Milestone v0.5.5)
- **Decision**: Replaced process-global reservation cache slots with genuinely isolated `thread_local` cache structures (`thread_local ThreadReservationCache`). Each thread manages up to 2 isolated reservation slots without locks or cross-thread interference. The cache struct incorporates an RAII destructor that automatically releases all retained reservations back to the OS upon thread termination.
- **Rationale**: In multithreaded programs, process-global cache slots without synchronization caused severe data races and double-allocations where two threads acquired the same reservation base pointer. Thread-local caches eliminate concurrency contention and guarantee absolute isolation.

## 64. Real Page Provider Dynamic Binding & ABI Integration (Milestone v0.5.5)
- **Decision**: Connected generated code directly to the static runtime library (`tamarin_runtime.lib`) and the C++ `MemoryPageProvider` interface via C ABI functions (`__tamarin_runtime_page_*` and `__tamarin_runtime_rcache_*`). Removed hardcoded 65,536-byte reservation and 4,096-byte page size constants from generated LLVM IR, querying allocation granularity and page size dynamically from the provider.
- **Rationale**: Hardcoded constants in LLVM IR broke portability and rendered custom page providers (such as test mocks or large-page providers) ineffective. Linking against the real provider ensures runtime agility and dynamic configurability.

## 65. Implicit Base-Class Construction Soundness (Milestone v0.5.5)
- **Decision**: In `IRBuilder::lowerConstructor()`, base class subobjects are now constructed even when the base class declares no explicit constructor, provided the base contains non-trivial resource-owning members. Base members are initialized before derived members and before the derived constructor body executes.
- **Rationale**: In v0.5.4, a derived constructor only invoked a base constructor if the base had an explicit zero-argument constructor declaration. When a base class had no constructor but contained a resource-owning member, the base member was left unconstructed, yet its destructor was executed on scope exit. Implicit base construction ensures construction-before-destruction symmetry.

## 66. Sound Rust Benchmark Allocator with Zeroable Bounds & Checked Arithmetic (Milestone v0.5.5)
- **Decision**: Genuinely hardened the Rust reference arena allocator in `benchmarks/rust/`: restricted safe zeroed allocations (`alloc` and `alloc_zeroed`) to types implementing the unsafe marker trait `Zeroable`, added `alloc_uninit::<T>` returning `&mut [MaybeUninit<T>]`, enforced power-of-two alignment with `align >= align_of::<T>()`, aligned newly allocated chunks to `max(align, 16)`, and protected all size and offset calculations with checked arithmetic.
- **Rationale**: Safe Rust code must never produce all-zero instances of arbitrary `Copy` types (e.g. types containing non-zero integers, function pointers, or reference wrappers), as this produces immediate undefined behavior. The `Zeroable` trait enforces a sound initialization contract.

## 67. High-Churn Performance Syscall Mitigation & Configurable Retention Policy (Milestone v0.5.5)
- **Decision**: Investigated the dominant overhead of high-churn regions (`10a_high_churn`), determining that repeated `VirtualAlloc(MEM_COMMIT)` and `VirtualFree(MEM_DECOMMIT)` system calls consume ~0.88 µs per pair (44 ms across 50,000 iterations). Introduced configurable caching policies: Policy 0 (default: decommit on reuse, 0 committed bytes held idle, 101.4 ms latency) and Policy 1 (retain-committed up to configurable memory cap `TAMARIN_RCACHE_MEMCAP`, 26.66 ms latency via userland `memset` zeroing).
- **Rationale**: Systems programmers require explicit control over the trade-off between absolute minimal commit charge and maximum allocation throughput. Policy 1 provides a 3.8x speedup on high-churn workloads while strictly bounding memory retention.

## 68. Reservation Cache Metadata Return & Redundant Commit Elimination (Milestone v0.5.6)
- **Decision**: Updated `__tamarin_runtime_rcache_acquire` to return both reservation capacity and previously committed bytes via output pointer arguments (`out_reserved`, `out_committed`). In generated LLVM IR (`lazy_first_alloc`), the runtime compares previously committed bytes against required commit size (`already_com >= needed_commit`). When sufficient pages are already committed, the OS kernel commit syscall (`VirtualAlloc(MEM_COMMIT)`) is completely skipped; when additional pages are required, only the uncommitted range `[already_com, needed_commit)` is committed.
- **Rationale**: In v0.5.5, `rcache_acquire` discarded `committed_size`, causing generated code to unconditionally call `VirtualAlloc(MEM_COMMIT)` on every region scope even when the block was already committed. Eliminating redundant syscalls reduces small-region entry overhead to pure arithmetic.

## 69. Bounded Thread-Local Small-Region Pool & Zero Steady-State Syscalls (Milestone v0.5.6)
- **Decision**: Implemented a bounded, thread-local pool of reusable, already-committed small-region blocks (`MAX_SLOTS = 4`). Small regions ($\le$ allocation granularity, 64 KB) acquire and return blocks via fast O(1) pointer operations without invoking `VirtualAlloc` or `VirtualFree`. Large regions and chunk-growth blocks strictly bypass the pool and release directly to the OS.
- **Rationale**: Transitioning to the Windows NT kernel to allocate/free small regions creates massive latency penalties compared to userland allocators. In steady state, small regions perform zero OS calls, dropping 50,000-iteration `10a_high_churn` latency from 103.21 ms to **0.7564 ms** (**137x speedup**), outperforming Clang C (2.23 ms) by 2.95x and Rust (3.21 ms) by 4.24x.

## 70. Strict Aggregate Committed-Memory Cap Across Entire Thread-Local Pool (Milestone v0.5.6)
- **Decision**: Applied the aggregate committed-memory cap (`memory_cap`, default 128 KB, configurable via `TAMARIN_RCACHE_MEMCAP`) across the entire thread-local pool, rather than per slot. If returning a block would cause aggregate committed pool memory to exceed `memory_cap`, the block's physical pages are decommitted to 0 before retention.
- **Rationale**: Per-slot caps could allow a multi-slot pool to accumulate unbounded physical commit charges under diverse workload patterns. Enforcing an aggregate cap guarantees that thread-local caches never exceed user-configured physical memory limits.

## 71. Airtight Zero-Initialization Guarantee on Region Scope Reclaim (Milestone v0.5.6)
- **Decision**: When a region scope exits and returns a block to the thread-local pool via `__tamarin_runtime_rcache_put`, the runtime zeroes all previously used payload bytes using userland `memset(base, 0, min(used_size, committed_size))`.
- **Rationale**: Memory handed out by regions must be zero-initialized by default. When memory is reused from a pool without kernel zeroing, bytes previously written by earlier regions would leak into new variables. Zeroing used bytes upon reclaim guarantees that all idle blocks in the pool are 100% clean, zeroed memory at all times.

## 72. Canonicalized Bounds Checking Lowering & SCEV Inner-Loop Elimination (Milestone v0.5.7)
- **Decision**: In `src/backend/llvm_codegen.cpp`, array bounds checks were canonicalized from compound signed negative checks (`icmp slt 0`, `sext to i64`, `icmp uge len`, `or`) to a single unsigned check (`icmp uge i32/i64 %idx, len`). In fixed-size arrays indexed by 32-bit integers, the check lowers to `icmp uge i32 %idx, fixedLen`.
- **Rationale**: The previous combination of signed comparison, sign extension, and logical `or` prevented LLVM's `IndVarSimplify` pass from analyzing loop bounds using Scalar Evolution (SCEV). Emitting canonical unsigned comparisons enables LLVM to prove bounds checks unreachable across loops with known upper bounds, completely eliminating bounds checks from inner loops and delivering a **5.3x speedup** on `01_integer` (from 20.31 ms to 3.81 ms).

## 73. Target Architecture Parity & `-march=native` Host CPU Autovectorization (Milestone v0.5.7)
- **Decision**: Enabled host CPU auto-detection in `llvm_codegen.cpp` using `-march=native` on `-O2` and `-O3` (with manual override via `TAMARIN_ARCH`).
- **Rationale**: Clang and Rust benchmarks default to or allow host target CPU feature enablement (`-march=native`, `-C target-cpu=native`), granting access to AVX, AVX2, and FMA instructions. Enabling native target CPU features ensures compiler parity and allows LLVM to autovectorize normal-form loops (e.g. Structure-of-Arrays particle simulations, float transforms) using 256-bit wide SIMD registers.

## 74. Region Fast-Path Telemetry Elimination for Pure Pointer Bump Allocation (Milestone v0.5.7)
- **Decision**: Guarded diagnostic counter updates (`p_rf_pad`, `p_rf_usd`, `p_rf_ac`) in `__tamarin_region_alloc`'s bump-pointer fast path with `@__tamarin_flag_memory_stats`.
- **Rationale**: When `--memory-stats` is disabled (the standard production and benchmarking mode), updating telemetry counters caused 3 extra loads and 3 extra stores on every allocation. Guarding the telemetry block allows LLVM to dead-code eliminate all diagnostic stores, reducing bump allocation in the fast path to two instructions (a single store of the new used offset and address offset GEP).

## 75. Dual Competitor Benchmark Baselines (Idiomatic vs Expert) & Reusable Arena Pooling (Milestone v0.5.7)
- **Decision**: For all 17 workloads, provided both idiomatic and expert competitor implementations in C and Rust. For memory allocation workloads, implemented genuine reusable thread-local arena pools (`benchmarks/c/reusable_arena.h` and `benchmarks/rust/src/lib.rs` `PooledArena`) performing zero steady-state heap allocations, while preserving idiomatic `malloc`/`free` and `Box::new`/`drop` as separate baselines.
- **Rationale**: A defensible compiler benchmark cannot compare an optimized, cached memory engine solely against naive, repeatedly-allocating baselines, nor can it compare against unfair competitor implementations. Providing both idiomatic and expert variants establishes an honest, rigorous spectrum of performance.

## 76. Canonical Induction Loops over Manual Non-Unit Stride Unrolling (Milestone v0.5.9)
- **Decision**: Replaced manual source-level unrolling (`i += 4`, `arr[i+1]...`) with canonical unit-stride loops (`for (int i = 0; i < N; i++)`) across expert benchmarks.
- **Rationale**: Manual unrolling with non-unit strides breaks LLVM Scalar Evolution (SCEV) induction variable proofs, preventing bounds-check elimination and forcing up to 4x redundant runtime bounds checks per iteration. Furthermore, it fragments basic blocks, causing the Loop Vectorizer to reject 256/512-bit vectorization. In contrast, canonical unit-stride loops allow LLVM to prove safety mathematically, eliminate 100% of bounds checks, and emit optimal AVX2/AVX-512 SIMD native code (`vaddps`, `vmulps`).

## 77. High-Dimensional SoA Memory Pass Separation for L1 D-Cache Fit (Milestone v0.5.9)
- **Decision**: In `04_structures_soa_expert`, separated the multi-field simulation loop into distinct dimensional passes ($X$ update, $Y$ update, $Z$ update) rather than interleaving 6 distinct array updates inside a single loop.
- **Rationale**: Interleaving 6 arrays of 5,000 floats accesses 120 KB of memory in lockstep, exceeding the 32 KB L1 data cache of x86-64 cores and causing hardware prefetcher thrashing. Separating the passes keeps working memory per pass at 40 KB, fitting neatly into cache and enabling peak SIMD streaming bandwidth.

## 78. Generational Slabs with Compact Handles as the Primary Recyclable Storage Architecture (Milestone v0.6)
- **Decision**: For non-monotonic dynamic object allocation with independent element lifetimes, adopted a generational slab architecture (`Slab<T>` / `slab`) returning compact 64-bit generational handles (`{ u32 slot, u32 gen }`). Free slots are managed via an inverted embedded free list.
- **Rationale**: Monotonic arenas (`region`) cannot reclaim individual deleted items. General-purpose heaps introduce fragmentation, locking, and use-after-free vulnerabilities. Generational slabs provide $O(1)$ allocation, $O(1)$ removal, zero fragmentation, and safe handle validation ($O(1)$ generation check), completely preventing use-after-free, double-free, and ABA bugs without garbage collection or Rust lifetime annotations.

## 79. Staged Architectural Roadmap Sequence: Recyclable Memory -> ADTs -> Generics -> TIR Passes -> Parallelism
- **Decision**: Formally sequenced the upcoming TAMARIN milestones: Milestone v0.6 (Slabs & Generational Handles) -> Milestone v0.7 (Algebraic Data Types & Pattern Matching) -> Milestone v0.8 (Generics & Parameterized Types) -> Milestone v0.9 (Domain-Aware TIR Passes & SIMD) -> Milestone v0.10 (Structured Concurrency & Parallel Execution) -> Milestone v1.0 (Production Ecosystem).
- **Rationale**: Memory safety and efficiency are TAMARIN's core value propositions. Completing non-monotonic recyclable storage (v0.6) establishes an airtight storage tier hierarchy before extending general-purpose language syntax (ADTs and Generics) and parallel execution primitives.

## 80. Generational Parity Invariant & Zero-Cost Handle Validation (Milestone v0.6.0)
- **Decision**: Implemented strict parity-based generational lifecycle invariants in recyclable slab storage:
  - Free slots hold **EVEN** generations ($0, 2, 4, ...$), starting at 0.
  - Active/occupied slots hold **ODD** generations ($1, 3, 5, ...$).
  - Allocation increments gen (even -> odd), issuing the handle with the odd generation.
  - Removal increments gen (odd -> even), invalidating the issued handle.
- **Rationale**: This guarantees that no free slot can ever match any issued handle. Validation is a single 4-byte comparison: `cmp [slab.generations + slot*4], handle.generation`. If equal, the slot is guaranteed active and issued to this exact handle. If mismatched, it is stale or freed. Dereference overhead is virtually indistinguishable from raw pointer dereferencing ($\le 1.10\times$), achieving ~5.97 ns validation and lookup time in benchmarks.

## 81. Generational Slot Retirement & Dynamic Page-Provider Chunk Growth (Milestone v0.6.0)
- **Decision**: Generational counters increment monotonically across slot lifecycles. When a slot generation reaches $0xFFFFFFFE$, the slot is permanently retired ($0xFFFFFFFF$) and never returned to the free list. Slabs dynamic growth allocates secondary chunks (`TamarinSlabChunk`) using the thread-local chunk cache (`ThreadChunkCache`) and OS virtual memory provider (`MemoryPageProvider`).
- **Rationale**: Generational retirement eliminates the mathematical possibility of ABA aliasing across 4 billion reuses of a slot. Dynamic chunk growth allows slabs to start with small reservations and grow on demand while maintaining valid handles across chunk boundaries. Non-trivial destructors are tracked and executed in reverse LIFO order upon explicit removal (`slab.remove()`) and full slab cleanup (`slab.clear()` / scope exit).

## 82. Tagged Unions & Algebraic Data Type Representation (Milestone v0.7.0)
- **Decision**: Represented enums with typed variant payloads as packed tagged unions: `%enum.Name = type { i32, [MaxPayload x i8] }`. Variant constructors store the variant index (tag) and write the variant arguments into the payload array.
- **Rationale**: Provides compact representation with deterministic alignment. Small variants incur minimal memory overhead while larger variants share the overlapping payload buffer.

## 83. Exhaustive Pattern Matching Lowering (Milestone v0.7.0)
- **Decision**: `match` expressions are validated for complete exhaustiveness during semantic analysis. Lowering in `IRBuilder` emits an LLVM multi-way `switch` on the discriminant tag, branching directly to individual arm basic blocks with SSA extraction of payload variables.
- **Rationale**: Guarantees zero missed unhandled cases at compile time. Lowers to dense hardware jump tables at runtime, avoiding sequential if-else branching chains.

## 84. Zero-Cost Discriminant Niche Optimization for Handles and Pointers (Milestone v0.7.0)
- **Decision**: For `Option<Handle<T>>` and `Option<ptr>`, eliminated the separate 4-byte discriminant tag by utilizing invalid states (generation 0 or null pointer) as the `None` sentinel.
- **Rationale**: Packs `Option<Handle<T>>` into exactly 8 bytes (matching raw 64-bit integer / pointer semantics) with zero storage or runtime memory overhead.

## 85. Early-Exit Error Propagation via the `?` Operator (Milestone v0.7.0)
- **Decision**: The `?` operator evaluates expressions of type `Result<T, E>` or `Option<T>`. On `Ok`/`Some`, it extracts the inner payload; on `Err`/`None`, it emits an immediate return of the error variant from the enclosing function.
- **Rationale**: Provides clean, expressive, non-exception error handling without hidden allocations or stack unwinding personality routines.

## 86. Zero-Cost Parametric Polymorphism via Static Monomorphization (Milestone v0.8.0)
- **Decision**: Generic functions (`fn func<T>(T val)`), generic structs (`struct Vec<T>`), and generic enums (`enum Option<T>`) are instantiated concretely at compile time based on observed call-site and declaration type arguments.
- **Rationale**: Static monomorphization generates fully specialized native code for each concrete type, unlocking complete LLVM inlining, loop unrolling, and vectorization without the pointer indirection, boxing, or virtual method overhead of runtime generics.

## 87. Static Trait Dispatch without Virtual Function Tables (Milestone v0.8.0)
- **Decision**: `trait` definitions establish compile-time contracts, and `impl Trait for Type` blocks provide concrete implementations. Functions with trait bounds (`where T: Trait`) resolve method invocations statically at compile time during monomorphization.
- **Rationale**: Completely bypasses vtable generation (`_vptr`) and indirect calls for known concrete types, converting trait calls into direct calls or inlined instructions.

## 88. Cross-Module Generic Deduplication via COMDAT Folding (Milestone v0.8.0)
- **Decision**: Specialized generic function and struct method instances are emitted with LLVM `linkonce_odr` linkage.
- **Rationale**: When multiple modules specialize the same generic template with identical type parameters, LLVM and the Windows PE/COFF linker automatically fold duplicate definitions into a single shared machine code instance, preventing binary bloat.

## 89. Core Standard Library Separation into Modular Primitives (Milestone v0.8.5)
- **Decision**: Shipped production standard library modules under `lib/std/`: `std.mem` (memory operations), `std.collections.vec` (`Vec<T>`), `std.string` (UTF-8 string builder), and `std.algorithms` (sorting and searching).
- **Rationale**: Isolates low-level dynamic memory management and unchecked operations into well-tested, reusable standard library modules, keeping the compiler core lean while providing high-performance building blocks for user code.

## 90. Custom Struct Indexing Lowering (`[]` Operator) (Milestone v0.8.5)
- **Decision**: Transparently lowered custom struct indexing: `container[i]` lowers to `container.get(i)` and `container[i] = val` lowers to `container.set(i, val)` during semantic analysis and IR construction.
- **Rationale**: Delivers clean array-style indexing syntax for custom data structures like `Vec<T>` while guaranteeing that at `-O2`/`-O3` the method calls are completely inlined to direct pointer arithmetic.

## 91. Cold Bounds Branch Profiling Metadata (Milestone v0.8.5)
- **Decision**: Attached LLVM branch weights metadata `!prof !1` (`!1 = !{!"branch_weights", i32 1, i32 1048576}`) to all runtime bounds checks, and marked `@__tamarin_bounds_error()` as `cold noreturn nounwind`.
- **Rationale**: Informs LLVM's branch predictor and code layout passes that bounds-check failure paths are virtually never taken, moving cold error traps entirely out of instruction cache lines for inner loops and enabling aggressive autovectorization.

## 92. Unchecked Memory Builtins for High-Throughput Algorithmic Kernels (Milestone v0.8.5)
- **Decision**: Provided `__builtin_get_unchecked<T>` and `__builtin_set_unchecked<T>` intrinsics that emit raw `ArrayLoad` and `ArrayStore` instructions without bounds checks.
- **Rationale**: Enables expert algorithmic implementations, such as the 8-bit LSD Signed Radix Sort in `std.algorithms`, to achieve peak hardware throughput matching or beating C and Rust.

## 93. Dedicated TIR Optimization Pass Subsystem (Milestone v0.9.0)
- **Decision**: Established a modular TIR pass architecture with `TIRFunctionPass`, `TIRModulePass`, and `PassManager` in `src/ir/opt/`, integrated into `-O1`, `-O2`, and `-O3` pipelines.
- **Rationale**: High-level semantic invariants (such as bounded array views, region lifetimes, and loop ranges) are easily analyzed at the TIR level before being lowered into flat LLVM IR where high-level type semantics are obscured.

## 94. Symbolic Range Analysis & Induction Variable Proving (Milestone v0.9.0)
- **Decision**: Implemented an `Interval` algebra engine in `RangeAnalysis` that tracks variable values through loop induction steps, integer additions, subtractions, multiplications, intersections, and unions.
- **Rationale**: Automatically proves that loop counters in canonical loops `for (int i = 0; i < len(arr); i++)` are strictly bounded within `[0, len(arr) - 1]`.

## 95. Domain-Aware Bounds-Check Elimination (BCE) (Milestone v0.9.0)
- **Decision**: Implemented the `BoundsCheckElimination` pass, which queries `RangeAnalysis` to prove index safety and replaces provably redundant `Opcode::BoundsCheck` instructions with `Opcode::Nop`.
- **Rationale**: Eliminates all runtime branching overhead in hot numerical and collection loops while preserving 100% memory safety.

## 96. Memory Disjointness & Alias Analysis (Milestone v0.9.0)
- **Decision**: Implemented `AliasAnalysis` in `src/ir/opt/alias_analysis.cpp` proving disjointness between distinct stack frames, separate region buffers, and independent reference parameters.
- **Rationale**: Establishes mathematical proofs of non-aliasing, allowing LLVM to emit `noalias` metadata that unlocks aggressive store forwarding and auto-vectorization.

## 97. First-Class Native SIMD Vector Types & Swizzling (Milestone v0.9.0)
- **Decision**: Introduced native SIMD vector types `float4`, `float8`, `int4`, `int8`, `int16`, component constructors, swizzling (`v.xyzw`), vector arithmetic, and reduction builtins (`reduce_add`, `dot_product`).
- **Rationale**: Lowers directly to LLVM native vector types `<4 x float>`, `<8 x float>`, etc., mapping cleanly to SSE/AVX/AVX2/AVX-512 vector registers without inline assembly or compiler intrinsics.

## 98. Zero-Allocation Work-Stealing Runtime Thread Pool (Milestone v0.10.0)
- **Decision**: Implemented a lock-free work-partitioning thread pool in `src/runtime/concurrency.cpp` integrated directly into `tamarin_runtime.lib`. Worker threads sleep on atomic condition variables when idle.
- **Rationale**: Eliminates OS thread creation and destruction overhead on parallel dispatch, delivering microsecond-level scheduling latency.

## 99. Structured Concurrency via `task_scope` and `spawn` (Milestone v0.10.0)
- **Decision**: Introduced `task_scope { ... }` blocks and `spawn { ... }` statements. All tasks spawned within a `task_scope` are guaranteed to complete before the scope exits. Orphaned `spawn` statements outside a `task_scope` are rejected during semantic analysis.
- **Rationale**: Guarantees structured lifetimes for concurrent tasks, completely preventing thread leaks, orphan background tasks, and use-after-free on stack variables passed to child threads.

## 100. Compile-Time Data-Race Freedom in `parallel for` (Milestone v0.10.0)
- **Decision**: In `TypeChecker`, tracked `parallelLoopScopeDepth_` and rejected any mutation or compound assignment of outer scalar variables inside a `parallel for` loop body.
- **Rationale**: Prevents data races at compile time without requiring runtime synchronization or locks.

## 101. Parallel Loop Outlining & Zero-Heap Context Packing (Milestone v0.10.0)
- **Decision**: In `IRBuilder`, parallel loop bodies are outlined into dedicated worker functions (`@__tamarin_pfor_worker_N`). Captured variables are packed into a stack-allocated pointer array (`alloca [K x ptr]`) in the parent frame and passed directly to the runtime dispatch function.
- **Rationale**: Achieves zero heap allocation for parallel dispatch, preserving cache locality and enabling full LLVM optimization of the worker functions.

## 102. First-Class Parallel Reduction Syntax (Milestone v0.11.0)
- **Decision**: Introduced syntax for declaring reductions directly on parallel loops: `parallel (reduction(op:var), ...) for (...) { ... }`, supporting operators `+`, `*`, `min`, `max`, `&`, `|`, `^`.
- **Rationale**: Makes parallel reductions expressive, intuitive, and concise, matching mathematical intent without requiring manual synchronization or thread coordination code.

## 103. Invariant Reduction Relaxation in Semantic Analysis (Milestone v0.11.0)
- **Decision**: Enhanced `TypeChecker` with active reduction tracking (`activeReductionVars_`), permitting in-place mutation and compound assignment (`+=`, `-=`, etc.) on declared reduction variables while maintaining strict race rejection on all other outer variables.
- **Rationale**: Safely relaxes data-race rules specifically for mathematically verified reduction accumulators.

## 104. In-Register Thread-Local Accumulator Outlining with Zero False Sharing (Milestone v0.11.0)
- **Decision**: In `IRBuilder`, lowered parallel reductions by allocating local accumulators in the worker function frame initialized to the algebraic identity (`0` for `+`/`|`/`^`, `1` for `*`, limits for `min`/`max`). These are promoted to SSA registers via LLVM `mem2reg`. At chunk completion, an atomic merge updates the parent variable.
- **Rationale**: Eliminates inter-core cache line bouncing and false sharing during loop execution, achieving 100% in-register computational throughput.

## 105. Sequentially Consistent Hardware Atomic Primitives (Milestone v0.11.0)
- **Decision**: Added TIR opcodes `AtomicLoad`, `AtomicStore`, `AtomicRMW`, and `AtomicCmpXchg`, lowering directly to LLVM `seq_cst` instructions. Exposed `__builtin_atomic_*` builtins and the `std.sync` library (`Atomic<T>`, `SpinLock`, `Channel<T>`).
- **Rationale**: Provides lock-free concurrency primitives matching hardware capabilities with sequential consistency guarantees.

## 106. Standard Concurrency Module `std.sync` (Milestone v0.11.0)
- **Decision**: Shipped `lib/std/sync.tmn` featuring `Atomic<T>`, `SpinLock`, and `Channel<T>` circular queues.
- **Rationale**: Provides high-level thread-safe abstractions built entirely on Tamarin's atomic primitives without external dependencies.

## 107. Lexical Active Borrow Tracking & Aliasing-XOR-Mutability (Milestone v0.12.0)
- **Decision**: Implemented `ActiveBorrow` tracking in `TypeChecker`. Enforces that any variable may have any number of active `const T&` immutable borrows OR exactly one `T&` mutable borrow, but never both simultaneously. Rejects call-site double mutable aliasing (`foo(&x, &x)`), mutating while borrowed, and transitive aliasing through pointer copies.
- **Rationale**: Delivers Rust-grade compile-time memory safety without complex lifetime syntax annotations in everyday code.

## 108. Definite Assignment & Fine-Grained Array Element Initialization (Milestone v0.12.0)
- **Decision**: Expanded definite-assignment analysis (`assignedStack_`) to track array element initialization per index (`arr[i]`). Variables assigned inside zero-trip loops are isolated from outer scopes.
- **Rationale**: Completely prevents reading uninitialized data across all branching, loop, and array access paths.

## 109. Destructive Move Semantics & `movedStack_` (Milestone v0.12.0)
- **Decision**: Introduced the explicit `move(x)` expression for ownership transfer of resource-owning types (`Vec<T>`, `String`, classes with destructors). Maintained `movedStack_` across lexical scopes and control-flow branches, rejecting use-after-move and borrow-after-move at compile time.
- **Rationale**: Eliminates shallow-copy double-free defects while providing explicit, zero-cost ownership transfer semantics.

## 110. Drop Glue Synthesis for Algebraic Data Types (Milestone v0.12.0)
- **Decision**: In `IRBuilder`, synthesized deterministic drop glue for enums containing resource-owning variant payloads (`Result<Vec<int>, String>`), automatically inspecting the active discriminant tag and calling the variant's destructor on reassignment or scope exit.
- **Rationale**: Guarantees zero resource leaks for complex algebraic data structures without manual cleanup.

## 111. Dual Binary Linkage: Dynamic CRT vs Self-Contained Static CRT (Milestone v0.12.0)
- **Decision**: Added `--dynamic` (default) and `--static` CLI compilation flags. `--dynamic` links against Windows Universal CRT (`ucrtbase.dll`), producing tiny **28.5 KB** executables. `--static` links against `libcmt.lib` with zero runtime DLL dependencies (**181.0 KB**).
- **Rationale**: Offers developers the flexibility of minimal deployment footprints for everyday utilities and self-contained portable executables for standalone deployment.

## 112. 16-Byte Fat-Pointer Layout for Closures & Function Types (Milestone v0.13.0)
- **Decision**: Function types `fn(ArgTypes...) -> ReturnType` and lambdas `(Type arg) => expr` are represented uniformly as a 16-byte aggregate: `{ ptr fn_ptr, ptr env_ptr }`.
- **Rationale**: Unifies bare function pointers, stateless lambdas, and capturing closures under a single consistent ABI representation without requiring dynamic heap allocation or multiple incompatible function pointer types.

## 113. Caller-Stack Environment Allocation for Zero-Overhead Closures (Milestone v0.13.0)
- **Decision**: For capturing closures, synthesized an anonymous stack struct in the caller's frame (`alloca`) containing pointers to captured stack slots, passing `ptr %env` as a hidden first argument to the lowered lambda.
- **Rationale**: Guarantees zero heap allocation, zero reference-counting overhead, and zero garbage collection pressure for local closures.

## 114. Provenance-Based Escape Prevention for Capturing Closures (Milestone v0.13.0)
- **Decision**: Propagated the union of all captured stack slots' `ProvenanceSet` (local stack tier and lexical scope depth) to the closure value. Rejected returning capturing closures (`env_ptr != null`), storing them into escaping structs, or assigning them to outer scopes. Stateless closures (`env_ptr == null`) may escape freely.
- **Rationale**: Solves the upward funarg problem (dangling stack environment pointers) with zero runtime cost by using compile-time semantic provenance analysis.

## 115. Temporary Active Borrows for Inline Lambda Arguments (Milestone v0.13.0)
- **Decision**: In `TypeChecker::checkCall`, registered temporary active borrow records for captured variables during inline lambda argument checking, releasing them once call arguments are checked.
- **Rationale**: Rejects conflicting aliasing within a single call expression, such as `foo(&x, () => { x = 1; })`.

## 116. Uniform Coercion of Stateless Lambdas and Named Functions (Milestone v0.13.0)
- **Decision**: Stateless lambdas and named C-style functions coerce into `{ fn_ptr, null }` with zero runtime branching at call sites.
- **Rationale**: Allows higher-order functions taking `fn(Args) -> Ret` to accept named functions, stateless lambdas, and capturing closures uniformly without requiring multiple function overloads.

## 117. Strict C ABI Boundary Rejections for Fat Closures (Milestone v0.13.0)
- **Decision**: Strictly rejected `TypeKind::Function` in `@export` functions, `extern "C"` declarations, and `@c_layout` struct fields with a clear diagnostic: `"closure type '...' has a 16-byte fat-pointer layout and cannot cross C ABI boundaries directly"`. Rejected closure-to-pointer casting (`(void*)closure`).
- **Rationale**: C99 foreign function interfaces expect 8-byte function pointers. Disallowing 16-byte fat closures at foreign boundaries completely prevents ABI mismatch crashes, stack corruption, and undefined behavior.




