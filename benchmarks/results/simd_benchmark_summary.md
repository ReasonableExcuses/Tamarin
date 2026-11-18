# High-Performance SIMD Vector Benchmark: Tamarin vs C++ AVX2 vs Rust AVX2

**Workload:** 100,000,000 iterations of 8-wide AVX2 vector accumulation and 4-wide vector dot product reductions.

| Implementation | Language | Runtime Median (ms) | Speedup vs Rust | Compile Time (ms) | Binary Size (KB) | Output Checksum |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Tamarin Native SIMD (float4/float8)** | Tamarin | **75.56 ms** | **0.99x** | 307.1 ms | 18.5 KB | `SUM=721420288; DOT=134217728` |
| **C++ Clang AVX2 Intrinsics (__m256)** | C++ | **75.03 ms** | **1.00x** | 380.1 ms | 141.5 KB | `SUM=721420288; DOT=134217728` |
| **Rust AVX2 Intrinsics (_mm256)** | Rust | **74.84 ms** | **1.00x** | 334.9 ms | 133.0 KB | `SUM=721420288; DOT=134217728` |
