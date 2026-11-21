# Tamarin Language Reference Specification

**Version:** 0.18.0  
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
- **Fixed-Size Arrays**: `T[N]` represents a contiguous inline array of `N` elements of type `T` (also standard C syntax `T name[N]`).
- **Slices**: `T[]` represents a non-owning fat pointer consisting of `(T* ptr, u64 len)`.
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
Closures use arrow syntax with optional parameter types:
```tamarin
int multiplier = 5;
auto fn = (int x) -> int => {
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

### 7.2 `await` Suspension
Within an `async` context, awaiting a task suspends the coroutine until completion without blocking the operating system thread:
```tamarin
async int orchestrate() {
    Task<int> t1 = compute_async(1);
    Task<int> t2 = compute_async(2);
    
    int r1 = await t1;
    int r2 = await t2;
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

---

## 8. Conditional Compilation Directives (`@cfg(...)`)

Tamarin provides compile-time configuration predicates evaluated before semantic analysis, pruning inactive declarations and imports:

```tamarin
@cfg(target_os = "windows")
import std.os.windows;

@cfg(target_os = "linux")
import std.os.linux;

@cfg(any(target_arch = "x86_64", target_arch = "aarch64"))
public void fast_compute() {
    // 64-bit architecture optimizations
}
```

Predicates support `target_os` (`"windows"`, `"linux"`, `"macos"`, `"darwin"`), `target_arch` (`"x86_64"`, `"aarch64"`), and boolean combinators `all(...)`, `any(...)`, and `not(...)`.

---

## 9. Standard Library Ecosystem

Tamarin provides a high-performance standard library designed for zero-allocation and cache-friendly systems engineering:

### 9.1 Swiss Table Hash Map (`std.collections.hash_map`)
Google Abseil-style Swiss Table with 1-byte control metadata and 16-way SIMD/group probing:
```tamarin
import std.collections.hash_map as hm;

hm::HashMap<int, int> map;
map.init();
map.insert(42, 1000);
if (map.contains(42)) {
    int val = map.get(42);
}
map.release();
```

### 9.2 Zero-Allocation Streaming JSON (`std.json`)
Non-owning string view slices and flat contiguous tape AST representation (`JsonNode nodes[]`):
```tamarin
import std.json as json;

char jsonText[] = "{\"server\": \"127.0.0.1\", \"port\": 8080}";
json::JsonDocument doc;
doc.init();
if (doc.parse(jsonText)) {
    int root = doc.root();
    int port = doc.get_int(root, "port");
}
doc.release();
```

### 9.3 Hardware Bit-Twiddling (`std.bits`)
Exposes direct LLVM hardware intrinsics:
```tamarin
import std.bits as bits;

int ones = bits::popcnt(0x00FF00FF);  // 16
int zeros = bits::clz(0x00010000);    // 15
int trailing = bits::ctz(8);          // 3
int rev = bits::bitrev(1);            // 0x80000000
int swapped = bits::bswap(0x12345678);// 0x78563412
bool isPow2 = bits::is_power_of_two(64); // true
```

### 9.4 Dynamic Vectors (`std.collections.vec`)
Contiguous growable dynamic array:
```tamarin
import std.collections.vec as col;

col::Vec<int> v;
v.init();
v.push(10);
v.push(20);
int x = v[0];
v.release();
```

---

## 10. Compiler Builtin Intrinsics

- `__builtin_alloc<T>(count)`: Heap allocation returning a slice `T[]`.
- `__builtin_realloc<T>(slice, count)`: Reallocates dynamic heap slice.
- `__builtin_free(slice)`: Releases heap slice.
- `__builtin_memcpy<T>(dst, dstOff, src, srcOff, count)`: Copies elements between slices.
- `__builtin_memset<T>(dst, dstOff, val, count)`: Sets byte values.
- `__builtin_popcnt(x)`, `__builtin_clz(x)`, `__builtin_ctz(x)`: Hardware bit intrinsics.
- `__builtin_bitrev(x)`, `__builtin_bswap(x)`: Hardware bit and byte reversal.
- `__builtin_hash(x)`: Universal high-speed hash function for scalar, float, slice, and struct keys.
- `len(slice)`: Returns element count of fixed array, slice, or string.
