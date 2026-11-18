# TAMARIN Language Specification & Overview (v0.14)

## 1. Philosophy and Design Commitments

**TAMARIN** is a C-like, memory-safe, performance-focused systems programming language compiler targeting native Windows x86-64 via LLVM. Tamarin is architected around five core pillars:

1. **C Readability + Direct C99 ABI Interoperability**:
   - All named functions strictly use classic C-style syntax (`ReturnType name(Type arg)`).
   - Functions marked with `@export` or declared in `extern "C"` blocks have zero-cost C99 ABI compatibility.
   - The type syntax `fn(ArgTypes...) -> ReturnType` is strictly reserved for function and closure types; anonymous lambdas use `(Type arg) => expr` or `(Type arg) => { ... }`. Tamarin deliberately rejects Rust-style `fn name(a: i32) -> i32` for named functions to preserve C clarity.
   - Transparently generates C99 header files via `--emit-header`.
2. **Rust-Grade Compile-Time Memory & Thread Safety**:
   - Enforces lexical scope lifetimes, definite initialization (including per-element array tracking), and aliasing-XOR-mutability (`ActiveBorrow`).
   - Destructive move semantics (`move(x)`) with automatic drop glue for resource-owning types and algebraic data types (`Result<T, E>`, `Option<T>`).
   - Complete compile-time data-race rejection in parallel loops and zero dangling stack escapes.
3. **Zero-Overhead Stack-Allocated Closures**:
   - Closures and function types are 16-byte stack-allocated fat pointers (`{ ptr fn_ptr, ptr env_ptr }`) with **zero heap allocation**, zero reference counting, and zero garbage collection.
   - Captured stack variables are borrow-checked; capturing closures are strictly prevented from escaping their captured frames.
4. **Asynchronous Concurrency, Coroutines & Zero-Allocation Fibers (v0.14)**:
   - First-class cooperative coroutines with `async ReturnType name(Params...)` and `await task` expressions.
   - Backed by `Task<T>` future handles with zero-allocation steady state via thread-local pooling (`ThreadTaskPool`) and 128-byte inline context/result buffers.
   - Multi-threaded cooperative work-stealing await prevents worker starvation and guarantees deadlock freedom in recursive async task trees.
   - Strict C ABI boundary safety: `async` functions and `Task<T>` types cannot cross `@export`, `extern "C"`, or `@c_layout` boundaries directly.
5. **Data-Oriented Performance Leadership**:
   - First-class Structure-of-Arrays (`@layout(scan)`) lowering, native SIMD vectors (`float4`, `float8`, `int4`, `int8`, `int16`), and fork-join parallel loops (`parallel for`, `parallel (reduction(...)) for`).
   - Zero-allocation work-stealing thread pool and page-backed lazy-commit memory regions.

---

## 2. Basic Syntax & Type System

### 2.1 Primitive Types
- `void`
- `bool` (`false` / `true`)
- `char`, `byte`, `u8` (8-bit unsigned integer)
- `i8` (8-bit signed integer)
- `short`, `i16` (16-bit signed integer)
- `unsigned short`, `u16` (16-bit unsigned integer)
- `int`, `i32` (32-bit signed integer)
- `unsigned int`, `u32` (32-bit unsigned integer)
- `long`, `i64` (64-bit signed integer)
- `unsigned long`, `u64` (64-bit unsigned integer)
- `float`, `f32` (IEEE-754 32-bit single-precision float)
- `double`, `f64` (IEEE-754 64-bit double-precision float)
- `size_t`, `usize`, `ptrdiff_t`, `isize` (target-width word integers)

### 2.2 Variables, Constants & Definite Assignment
Variables are mutable by default. `const` makes a binding immutable:
```c
int count = 0;
const int max_limit = 1000;
count += 1;
```
Tamarin strictly enforces **definite assignment**: reading an uninitialized variable on any code path is rejected at compile time:
```c
int val;
if (condition) {
    val = 10;
}
// error: variable 'val' is used before being initialized
println(val);
```

### 2.3 Arrays & Bounded Array Views
- Fixed-size stack arrays: `int numbers[4] = {1, 2, 3, 4};`
- Inferred-size arrays: `int primes[] = {2, 3, 5, 7, 11};`
- **Bounded Array Views**: Function parameters taking `T name[]` or `const T name[]` lower to a 16-byte bounded fat pointer `{ ptr data, i64 len }`:
```c
int sum(int values[]) {
    int total = 0;
    for (int i = 0; i < len(values); i++) {
        total += values[i]; // Automatic bounds checking
    }
    return total;
}
```
All array indexing is bounds-checked. Under `-O3`, Tamarin's **Bounds-Check Elimination (BCE)** pass proves loop invariants and eliminates redundant checks, while retaining safety traps on invalid indices.

---

## 3. Stack-Allocated Borrow-Checked Closures (v0.13)

### 3.1 Closure Types & Syntax
- **Closure / Function Type**: `fn(ArgTypes...) -> ReturnType`
- **Expression Lambda**: `(int x) => x * 2`
- **Block Lambda**: `(int a, int b) => { return a + b; }`
- **No-Argument Lambda**: `() => { println("hello"); }`

### 3.2 16-Byte Fat-Pointer Layout
Tamarin represents all function values and lambdas as a 16-byte aggregate:
```text
struct TamarinClosure {
    void* fn_ptr;   // Machine code pointer
    void* env_ptr;  // Caller-stack environment pointer (or null)
};
```
When a lambda captures variables from its enclosing function, the compiler synthesizes an anonymous environment struct on the caller's stack frame (`alloca`) containing pointers to the captured stack slots. `env_ptr` is passed as a hidden first argument to the lowered function.

### 3.3 Capture Analysis & Escape Prevention
1. **Zero Heap Allocation**: All closures are stack-allocated. No heap allocators or garbage collectors are invoked.
2. **Borrow Checking**: While a capturing closure is live, active borrow records prevent conflicting mutations of captured variables.
3. **Escape Prevention**: A capturing closure (`env_ptr != null`) carries the `ProvenanceSet` of all captured stack slots. It **cannot escape** its defining scope:
   ```c
   fn(int) -> int make_adder(int factor) {
       // COMPILE ERROR: cannot return closure capturing local variable 'factor'
       return (int x) => x + factor;
   }
   ```
4. **Stateless Closures & Function Coercion**: Lambdas that do not capture any local variables have `env_ptr == null` and may escape freely. Named C-style functions coerce into `{ fn_ptr, null }` with zero runtime overhead:
   ```c
   int square(int x) { return x * x; }

   void apply(fn(int) -> int f) {
       println(f(5));
   }

   int main() {
       apply(square);             // Coerces named function
       apply((int x) => x * 3);   // Stateless lambda
       int mult = 4;
       apply((int x) => x * mult);// Capturing closure (stack-allocated)
       return 0;
   }
   ```

### 3.4 C ABI Boundary Enforcement
Standard C99 function pointers are 8 bytes (`Ret (*)(Args...)`), whereas Tamarin closures are 16 bytes. To prevent silent memory corruption:
- `fn(...) -> Ret` is strictly rejected in `@export` functions, `extern "C"` declarations, and `@c_layout` struct fields.
- Casting closures to or from `void*` is rejected at compile time.

---

## 4. Rust-Grade Compile-Time Memory Safety (v0.12)

### 4.1 Aliasing-XOR-Mutability & Active Borrows
Tamarin enforces the fundamental law of memory safety:
$$\text{Storage Access} \implies (\text{Shared Immutability}) \oplus (\text{Exclusive Mutability})$$
- You may have any number of active `const T&` references to a variable.
- You may have at most one active `T&` mutable reference to a variable.
- You cannot mutate a variable while an active reference borrows it:
```c
int x = 10;
int& r = x;
x = 20; // COMPILE ERROR: cannot mutate 'x' while active pointer 'r' borrows it
println(r);
```

### 4.2 Destructive Move Semantics (`move(x)`)
Resource-owning types (types with destructors, `Vec<T>`, `String`, `File`) cannot be silently copied. Ownership must be transferred using explicit `move(x)`:
```c
Vec<int> v1;
v1.init(4);
v1.push(100);

Vec<int> v2 = move(v1); // Ownership transferred
// v1.push(200);        // COMPILE ERROR: use of moved variable 'v1'
```

### 4.3 Enum Drop Glue & Algebraic Data Types
When an enum containing resource-owning variants (`Result<Vec<int>, String>`) is reassigned or falls out of scope, Tamarin's compiler-synthesized drop glue examines the active variant discriminant and invokes the appropriate destructor automatically.

---

## 5. Algebraic Data Types & Pattern Matching

### 5.1 Enums with Typed Payloads
```c
enum Message {
    Quit,
    Move(int, int),
    Write(String),
    ChangeColor(int, int, int)
};
```
Enums lower to packed tagged unions: `%enum.Message = type { i32, [MaxPayload x i8] }`.

### 5.2 Exhaustive Pattern Matching (`match`)
Pattern matching requires exhaustive coverage of all variants. Arms can bind and destructure variant payloads:
```c
void process(Message msg) {
    match (msg) {
        case Message::Quit: {
            println("Quitting");
        }
        case Message::Move(x, y): {
            println(x);
            println(y);
        }
        case Message::Write(str): {
            println(str.as_view());
        }
        case Message::ChangeColor(r, g, b): {
            println(r);
        }
    }
}
```

### 5.3 `Option<T>`, `Result<T, E>`, and the `?` Operator
Tamarin provides built-in `Option<T>` (`Some(T)` / `None`) and `Result<T, E>` (`Ok(T)` / `Err(E)`). The `?` operator unwraps success values or returns early on errors:
```c
Result<int, String> compute() {
    int val = step1()?; // Returns Err if step1 fails
    return Result<int, String>::Ok(val * 2);
}
```

---

## 6. Object-Oriented Programming & Structs

### 6.1 Structs, Classes & Memory Layouts
- `struct`: Public members by default, layout adheres to Windows x86-64 ABI.
- `class`: Private members by default, supports constructors, methods, and RAII destructors.
- `@c_layout`: Standard C ABI layout matching Clang/MSVC.
- `@layout(point)`: Contiguous Array-of-Structures (AoS).
- `@layout(scan)`: Structure-of-Arrays (SoA) layout. Fixed-size arrays of SoA structs allocate independent parallel arrays per field, enabling high-throughput SIMD auto-vectorization.

### 6.2 Constructors & Deterministic RAII Destructors
```c
class Buffer {
private:
    int* ptr;
    int size;

public:
    Buffer(int cap) {
        this.ptr = alloc<int>(cap);
        this.size = cap;
    }

    ~Buffer() {
        if (this.ptr != null) {
            free(this.ptr);
        }
    }
};
```
Destructors execute in strict reverse-order of construction upon block scope exit, early `return`, or loop `break`/`continue`.

---

## 7. First-Class SIMD Vectorization (v0.9)

Tamarin provides native SIMD vector types mapping directly to hardware SSE, AVX, AVX2, and AVX-512 vector registers:
- `float4` (128-bit vector of 4 `f32`s)
- `float8` (256-bit vector of 8 `f32`s)
- `int4`, `int8`, `int16` (128/256/512-bit integer vectors)

### SIMD Syntax, Swizzling & Builtins
```c
float4 a = float4(1.0f, 2.0f, 3.0f, 4.0f);
float4 b = float4(2.0f); // Splat constructor: [2.0, 2.0, 2.0, 2.0]
float4 c = a + b;        // Hardware vector addition

// Component swizzling and indexing
float x = c.x;
float4 swapped = c.wzyx;

// Hardware reduction builtins
float sum = reduce_add(c);
float dot = dot_product(a, b);
```

---

## 8. Safe Structured Concurrency & Parallel Reductions (v0.10–v0.11)

### 8.1 Work-Stealing Parallel Loops (`parallel for`)
```c
parallel for (int i = 0; i < N; i++) {
    results[i] = compute(inputs[i]);
}
```
Worker chunks are dispatched across an allocation-free thread pool. Outer scalar mutations are rejected at compile time to guarantee data-race freedom.

### 8.2 First-Class Parallel Reductions
Parallel reductions declare reduction variables directly in syntax:
```c
int total_sum = 0;
int min_val = 1000000;

parallel (reduction(+:total_sum), reduction(min:min_val)) for (int i = 0; i < N; i++) {
    total_sum += data[i];
    if (data[i] < min_val) {
        min_val = data[i];
    }
}
```
- **Thread-Local Accumulators**: Outlines worker loops with stack-allocated local accumulators initialized to algebraic identities (`0`, `1`, limits), promoted to SSA registers via LLVM `mem2reg` with **zero memory traffic and zero false sharing**.
- **Hardware Atomic Merges**: At chunk completion, local accumulators are atomically merged via hardware `atomicrmw` instructions.

### 8.3 Structured Concurrency (`task_scope` & `spawn`)
```c
task_scope {
    spawn {
        task_a();
    };
    spawn {
        task_b();
    };
} // Guaranteed join: all spawned tasks complete before exiting the scope
```

---

## 9. Adaptive Memory Engine: Regions & Generational Slabs

### 9.1 Page-Backed Lazy-Commit Regions (`region`)
Monotonic arenas backed by virtual memory pages for high-throughput transient allocations:
```c
region scratch (1024 * 64) {
    int temp[1000];
    // Allocates from region bump pointer
} // Entire region memory is bulk-reclaimed instantly with zero individual free() calls
```
Empty regions allocate 0 virtual bytes and make 0 syscalls. Reclaimed blocks are cached in thread-local pools for zero-syscall steady-state operation.

### 9.2 Generational Slabs & Compact Handles (`Slab<T>`)
For non-monotonic dynamic object allocation with independent element lifetimes:
```c
Slab<Entity> pool;
Handle<Entity> h = pool.insert(Entity(1, 2));

if (pool.is_valid(h)) {
    Entity e = pool.get(h);
}

pool.remove(h); // Increments generation; handle h is permanently retired
```
Handles are compact 64-bit `{ u32 slot, u32 gen }` tokens passed by value in CPU registers with zero pointer vulnerability.

---

## 10. Parametric Polymorphism & Traits (v0.8)

### 10.1 Generic Functions & Structs
```c
void swap<T>(T& a, T& b) {
    T tmp = move(a);
    a = move(b);
    b = move(tmp);
}

struct Pair<A, B> {
    A first;
    B second;
};
```
Monomorphized at compile time with zero runtime dispatch cost.

### 10.2 Traits & Static Dispatch
```c
trait Hashable {
    u64 hash(const self&);
}

impl Hashable for int {
    u64 hash(const int& val) {
        return (u64)val * 2654435761u;
    }
}

// Static dispatch resolved at compile time
void record<T>(const T& item) where T: Hashable {
    u64 h = item.hash();
}
```

---

## 11. Standard Library Modules (`lib/std/`)

| Module | Canonical Import | Description |
| :--- | :--- | :--- |
| **`std.io`** | `import std.io;` | Console output (`print`, `println`), input reading (`getInput`, `readLine`). |
| **`std.file`** | `import std.file;` | Procedural file operations (`openFile`, `readFile`) and RAII `File` class. |
| **`std.math`** | `import std.math;` | Mathematical utilities (`abs`, `min`, `max`, `clamp`, `ipow`, `sqrt`). |
| **`std.mem`** | `import std.mem;` | Dynamic heap primitives (`alloc<T>`, `realloc<T>`, `free<T>`, `copy`, `set`, unchecked indexing). |
| **`std.collections.vec`** | `import std.collections.vec;` | Dynamic vector `Vec<T>` with geometric growth, bounds checking, and custom `[]`. |
| **`std.string`** | `import std.string;` | Production UTF-8 dynamic string with formatting, concatenation, and substring utilities. |
| **`std.algorithms`** | `import std.algorithms;` | Dual-pivot Introsort, $O(N)$ 8-bit LSD Signed Radix Sort, binary search, partition. |
| **`std.sync`** | `import std.sync;` | Atomic primitives (`Atomic<T>`), low-latency `SpinLock`, and bounded `Channel<T>`. |

---

## 12. C ABI Interoperability & Toolchain CLI

### 12.1 Foreign Function Interface & `@export`
```c
// Import foreign C functions
extern "C" {
    int puts(const char* s);
    void* malloc(size_t size);
    void free(void* ptr);
}

// Export native Tamarin functions with standard C ABI
@export
int calculate_metrics(int width, int height) {
    return width * height;
}
```

### 12.2 Compiler Command-Line Interface
- `tamarin build <file.tmn>`: Compiles to native Windows x86-64 executable.
- `tamarin build <file.tmn> --static`: Compiles self-contained executable with static CRT.
- `tamarin build <file.tmn> --emit-header <file.h>`: Generates C99 interoperability header.
- `tamarin build <file.tmn> --emit-memory-report`: Emits static memory planning and storage tiers.
- `tamarin build <file.tmn> -O0..-O3`: Configures optimization pipelines.
- `tamarin run <file.tmn>`: Compiles and executes immediately.

---

## 13. Asynchronous Concurrency, Coroutines & Zero-Allocation Fibers (v0.14)

### 13.1 Async Function Syntax & Return Types
Tamarin enforces classic C readability for async functions:
```c
async int fetch_data(int id) {
    // Computations run cooperatively on work-stealing thread pool
    return id * 42;
}

async void log_event(int code) {
    println(code);
}
```
- Calling an `async ReturnType name(Params...)` function returns a `Task<ReturnType>` handle.
- If the return type is `void`, it returns `Task<void>`.
- The worker body is automatically outlined into an LLVM worker `@__tamarin_async_worker_<name>(ptr %ctx, ptr %result)`.
- Arguments are packed into an inline caller-allocated context tuple, requiring zero heap allocation for parameter passing.

### 13.2 The `await` Expression
Values inside `Task<T>` handles are extracted via `await`:
```c
int main() {
    // 1. Spawning concurrent background tasks
    Task<int> t1 = fetch_data(10);
    Task<int> t2 = fetch_data(20);

    // 2. Awaiting results
    int r1 = await t1;
    int r2 = await t2;
    int sum = r1 + r2;

    // 3. Direct inline await in expressions
    int combined = (await fetch_data(1)) + (await fetch_data(2));
    return 0;
}
```
- `await task_expr` suspends or cooperatively awaits the completion of `task_expr`.
- Type checking enforces that `await` can only be applied to `Task<T>` handles; attempting to await non-task types (`int x; await x;`) produces a compile-time diagnostic: `error: cannot await non-task type 'int'`.
- After completion, `tamarin_async_task_await` extracts the result value and releases the task handle.

### 13.3 Recursive Work-Stealing & Deadlock Freedom
Tamarin's runtime thread pool supports recursive async calls through cooperative work-stealing:
```c
async int fib(int n) {
    if (n <= 1) return n;
    Task<int> a = fib(n - 1);
    Task<int> b = fib(n - 2);
    return (await a) + (await b);
}
```
- When a thread awaits a task that has not started yet, it claims and executes the task directly inline.
- If the task is already running on another thread, the awaiting thread steals and executes other pending tasks from the queue instead of sleeping.
- This guarantees that recursive task graphs (such as parallel divide-and-conquer, parallel mergesort, or tree evaluations) never exhaust thread pool worker threads or deadlock.

### 13.4 Zero-Allocation Steady State (`ThreadTaskPool`)
- Each `TamarinAsyncTask` contains an inline 128-byte parameter context buffer and an inline 128-byte result buffer.
- Tasks with parameters and return values $\le 128$ bytes require **zero dynamic heap allocation** (`malloc`/`free`).
- Thread-local task pools (`ThreadTaskPool`) cache up to 512 reusable task descriptors per OS thread, achieving sub-microsecond task creation and recycling.

### 13.5 C ABI Boundary Safety
Because `Task<T>` is a managed, thread-synchronized runtime handle and async functions require runtime thread pool coordination:
- Async functions cannot be exported across C boundaries (`@export async int foo()` is rejected at compile time).
- `Task<T>` types cannot cross C ABI boundaries (cannot be parameter types or return types in `@export` or `extern "C"` functions, or fields in `@c_layout` structs).

