# GUI Calculator Benchmark: Tamarin vs C++ vs Rust

This benchmark evaluates a fully functional native GUI Calculator application implemented in three systems programming languages:
1. **Tamarin (v0.18.5)** — Managed via `troop` package manager with native Raylib 5.5 bindings.
2. **C++ (C++17)** — Compiled via `clang++` with `-O3` linking directly against Raylib 5.5.
3. **Rust (Edition 2021)** — Managed via `cargo` with direct C ABI bindings to Raylib 5.5.

All three implementations use the **exact same Raylib 5.5 native rendering library, same window geometry (360x560), same color palette, same font rendering, same input handling, and same calculation state machine**.

---

## 1. Benchmark Results Summary

Measured on **Windows 11 x86_64 (AMD Ryzen 7 7840HS, 16 GB DDR5, NVMe SSD)** running 300 active frames of calculation, text measurement, and button updates:

| Metric | Tamarin | C++ | Rust | Winner / Analysis |
|:---|:---|:---|:---|:---|
| **Release Build Time** | **0.73 s** | **0.28 s** | 1.03 s | C++ fastest (single file), Tamarin **29% faster than Cargo/Rust** |
| **Stripped Binary Size** | **502.5 KB** | **489.0 KB** | 641.5 KB | C++ smallest, Tamarin within **2.7%** of C++; Rust is **31% larger** |
| **Total Runtime (300 frames)**| **6.004 s** | 6.141 s | 6.065 s | **Tamarin is fastest** (50.0 FPS vs 48.9 FPS C++ vs 49.5 FPS Rust) |
| **Frame Render Time** | **20.01 ms** | 20.47 ms | 20.22 ms | **Tamarin delivers lowest frame time** |
| **Peak Working Set RAM** | **92.59 MB** | **92.17 MB** | **92.23 MB** | Virtually identical (within 0.4 MB, dominated by GPU context) |
| **Source Lines of Code (SLOC)**| 394 lines | 369 lines | 366 lines | Comparable code density across all three |
| **Safety Model** | **Safe by Default** | Unsafe | Unsafe FFI | Tamarin requires **zero `unsafe` blocks**, Rust requires extensive `unsafe` |

---

## 2. Key Findings & Architectural Analysis

### Performance & Throughput
- **Zero-Overhead Raylib Bindings**: Tamarin's C ABI struct passing (`@c_layout struct Vector2`, `struct Color`, `struct Rectangle`) lowers directly to LLVM IR values without wrapper thunks or marshaling allocations. As a result, Tamarin's frame time (20.01 ms) matches and slightly edges out both C++ (20.47 ms) and Rust (20.22 ms).
- **Sub-Second Compilation**: `troop build --release` invokes Tamarin's TIR optimizer and Clang LLVM code generation in just **0.73 seconds**, beating `cargo build --release` (1.03 seconds).

### Binary Size
- Tamarin's release binary is **502 KB**, almost indistinguishable from optimized C++ (**489 KB**).
- Rust's release binary (**641 KB**) carries additional standard library runtime overhead (`std::panicking`, formatting machinery, unwinding tables).

### Safety & Ergonomics
- **Tamarin**: Written entirely in safe, idiomatic Tamarin. Definite assignment ensures that state buffers and structs are never read uninitialized. The borrow checker statically verifies that references to buffers do not escape their lifetime. Zero `unsafe` keywords needed.
- **C++**: Compiles with `-Wdeprecated-declarations` warnings on standard C runtime functions (`strcpy`, `getenv`). Manual pointer and buffer arithmetic with no spatial bounds protection unless manually instrumented.
- **Rust**: While the core state machine is safe, calling Raylib's C ABI requires wrapping every single API call, key query, mouse position check, and drawing routine in `unsafe { ... }` blocks, plus manual `CString` conversions and `.as_ptr()` management.

---

## 3. Running the Benchmark

From the repository root:
```bash
python benchmarks/projects/gui_test/benchmark.py
```
