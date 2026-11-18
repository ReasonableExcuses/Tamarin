# Tamarin Language Reference Specification

**Version:** 0.15.0  
**Target:** LLVM 20+ / C99 ABI  
**Classification:** Systems Programming Language Specification  

---

## 1. Syntax and Lexical Structure

Tamarin adopts an explicit C-family syntax designed for readability, predictable parsing without backtracking, and unambiguous AST construction.

### 1.1 Source Encoding and Identifiers
- Source files are encoded in UTF-8.
- Identifiers begin with an ASCII letter or underscore `[a-zA-Z_]`, followed by any number of alphanumeric characters or underscores `[a-zA-Z0-9_]*`.
- Semicolons `;` are required statement terminators.

### 1.2 Comments
```tamarin
// Single-line comment to end of line
/* Multi-line 
   block comment */
```

### 1.3 Visibility Modifiers
Functions and types declared at module scope are internal to the translation unit by default. The `public` keyword exposes symbols to importers and package dependents:
```tamarin
public int add(int a, int b) {
    return a + b;
}
```

---

## 2. Type System

Tamarin features a statically typed, monomorphic core type system with deterministic representation.

### 2.1 Primitive Types
| Type | Representation | Bit Width | Description |
| :--- | :--- | :--- | :--- |
| `i8`, `u8` | Signed / Unsigned Integer | 8 | Byte / 8-bit scalar integer |
| `i16`, `u16` | Signed / Unsigned Integer | 16 | 16-bit halfword integer |
| `int`, `i32`, `u32` | Signed / Unsigned Integer | 32 | Standard 32-bit integer |
| `i64`, `u64` | Signed / Unsigned Integer | 64 | 64-bit wide integer |
| `f32`, `float` | IEEE-754 Single Precision | 32 | Single-precision floating point |
| `f64`, `double` | IEEE-754 Double Precision | 64 | Double-precision floating point |
| `bool` | Boolean | 8 (`i1` in LLVM IR) | Logical `true` or `false` |
| `void` | Unit / Empty Type | 0 | Function return without value |

### 2.2 Aggregate and Composite Types
- **Fixed-Size Arrays**: `[N]T` represents a contiguous inline array of `N` elements of type `T`.
- **Slices**: `[]T` represents a non-owning fat pointer consisting of `(T* ptr, u64 len)`.
- **Pointers and References**:
  - `&T`: Immutable borrowed reference.
  - `&mut T`: Unique mutable borrowed reference.
  - `*T`: Unsafe raw pointer for C FFI interoperability.

### 2.3 SIMD Vector Types
Tamarin provides native SIMD vector primitives lowered directly to LLVM vector registers:
- `float4`: 128-bit vector consisting of 4 packed single-precision floats (`<4 x float>`).
- `float8`: 256-bit AVX2 vector consisting of 8 packed single-precision floats (`<8 x float>`).

SIMD types support component-wise arithmetic operators (`+`, `-`, `*`, `/`) and vector intrinsics:
```tamarin
float4 va = float4(1.0, 2.0, 3.0, 4.0);
float4 vb = float4(5.0, 6.0, 7.0, 8.0);
float4 vc = va * vb + va;
```

---

## 3. Functions and Declarations

Function signatures strictly follow standard C syntax: `ReturnType FunctionName(Parameters)`.

```tamarin
int calculate_distance(int x1, int y1, int x2, int y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    return dx * dx + dy * dy;
}
```

### 3.1 Parameter Passing Conventions
- Scalar primitives (`int`, `float`, `bool`) are passed by value in target registers.
- Structs and fixed arrays are passed by reference or value based on calling convention and ABI rules.
- Explicit borrows (`&T` and `&mut T`) guarantee zero-copy reference passing with compile-time borrow verification.

---

## 4. Data-Oriented Structs and `@layout(scan)`

Tamarin treats data orientation as a first-class language construct to guarantee optimal CPU L1/L2 cache hit rates and automated vectorization.

### 4.1 Standard Structs (Array-of-Structures)
```tamarin
struct Particle {
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
};
```

### 4.2 `@layout(scan)` Structure-of-Arrays
When a struct is marked with `@layout(scan)`, arrays of this struct are transparently transformed by the compiler from Array-of-Structures (AoS) to Structure-of-Arrays (SoA) in memory:
```tamarin
@layout(scan)
struct ParticleSoA {
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
};
```
Under `@layout(scan)`:
1. `ParticleSoA particles[1024];` allocates six contiguous, cache-aligned column buffers: `float x[1024]`, `float y[1024]`, ..., `float vz[1024]`.
2. Accesses such as `particles[i].x += particles[i].vx;` are emitted as contiguous column vector loads and stores.
3. Loops over `@layout(scan)` arrays are automatically vectorized with AVX2/AVX-512 without requiring manual memory transpositions.

---

## 5. Algebraic Data Types and Pattern Matching

Tamarin supports tagged unions (enums with payloads) and exhaustive pattern matching.

### 5.1 Enums with Payloads
```tamarin
enum Result<T, E> {
    Ok(T),
    Err(E)
}

enum Option<T> {
    Some(T),
    None
}
```

### 5.2 Exhaustive `match` Statements
Pattern matching enforces compile-time exhaustiveness:
```tamarin
int handle_result(Result<int, int> res) {
    match (res) {
        Result::Ok(val) => {
            return val;
        }
        Result::Err(err_code) => {
            return -err_code;
        }
    }
}
```

---

## 6. Capturing Closures

Tamarin provides stack-allocated, borrow-checked closures without requiring a garbage collector or heap allocator.

### 6.1 Syntax
Closures use pipe syntax for parameters:
```tamarin
int multiplier = 5;
auto fn = |int x| -> int {
    return x * multiplier;
};
int res = fn(10); // Returns 50
```

### 6.2 Capture Semantics and In-Memory Layout
- **Stack Allocation**: Closures are represented as a 16-byte fat pointer:
  - 8 bytes: Function pointer `void (*fn)(void* ctx, ...)`
  - 8 bytes: Context pointer `void* ctx` referencing the stack activation record of the enclosing scope.
- **Lifetime Safety**: Closures cannot escape the stack frame of their captured variables. Returning a closure capturing local stack references is rejected at compile time by the borrow checker.

---

## 7. Concurrency and Asynchronous Programming

Tamarin integrates structured concurrency with deterministic thread scheduling and zero-allocation coroutines.

### 7.1 `async` Functions and `Task<T>`
An `async` function returns a `Task<T>`:
```tamarin
async int compute_async(int id) {
    int sum = 0;
    for (int i = 0; i < 1000; i++) {
        sum += i * id;
    }
    return sum;
}
```

### 7.2 `.await` Suspension
Within an `async` context, awaiting a task suspends the coroutine until completion without blocking the operating system thread:
```tamarin
async int orchestrate() {
    Task<int> t1 = compute_async(1);
    Task<int> t2 = compute_async(2);
    
    int r1 = t1.await;
    int r2 = t2.await;
    return r1 + r2;
}
```

### 7.3 `parallel for`
Data-parallel loops execute across the runtime work-stealing thread pool with compile-time affine injectivity race analysis:
```tamarin
int data[10000];
parallel for (int i = 0; i < 10000; i++) {
    data[i] = i * i;
}
```
The compiler verifies that loop indexing guarantees non-overlapping memory writes, rejecting potential data races before code generation.
