#!/usr/bin/env python3
"""
Tamarin First-Class Parallel Reduction vs C++ OpenMP Reduction vs Rust Atomic Reduction
Workload: 100 Million Parallel SIMD Vector Operations with Dual Reductions (total_sum, total_dot_all):
  - 100 parallel chunks x 1,000,000 iterations of 8-wide AVX2 vector additions and 4-wide vector dot products
  - First-class in-register thread-local accumulator with zero false sharing
  - Atomic reduction at chunk completion
  - Multi-Threaded Runtime Speed (ms)
  - Compilation Speed (ms)
  - Binary Size (KB)
  - Bit-Identical Numerical Checksum
"""

import os
import sys
import time
import subprocess
import statistics
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = BENCH_DIR.parent.parent
CLANGPP_PATH = r"C:\Program Files\LLVM\bin\clang++.exe"
RUSTC_PATH = r"C:\Users\hp\.cargo\bin\rustc.exe"
TAMARIN_PATH = REPO_ROOT / "build" / "Release" / "tamarin.exe"
RESULTS_FILE = REPO_ROOT / "benchmarks" / "results" / "reduction_benchmark_summary.md"

CONFIGS = [
    {
        "id": "tamarin_reduction",
        "name": "Tamarin Safe Parallel Reduction (`parallel (reduction(...)) for`)",
        "lang": "Tamarin",
        "src": BENCH_DIR / "reduction_tamarin.tmn",
        "exe": BENCH_DIR / "reduction_tamarin.exe",
        "compile_cmd": [str(TAMARIN_PATH), "-O3", str(BENCH_DIR / "reduction_tamarin.tmn"), "-o", str(BENCH_DIR / "reduction_tamarin.exe")],
    },
    {
        "id": "cpp_openmp_static",
        "name": "C++ Clang OpenMP (`schedule(static) reduction(+:...)`)",
        "lang": "C++",
        "src": BENCH_DIR / "reduction_cpp_static.cpp",
        "exe": BENCH_DIR / "reduction_cpp_static.exe",
        "compile_cmd": [CLANGPP_PATH, "-O3", "-mavx2", "-fopenmp", str(BENCH_DIR / "reduction_cpp_static.cpp"), "-o", str(BENCH_DIR / "reduction_cpp_static.exe")],
    },
    {
        "id": "cpp_openmp_dynamic",
        "name": "C++ Clang OpenMP (`schedule(dynamic) reduction(+:...)`)",
        "lang": "C++",
        "src": BENCH_DIR / "reduction_cpp_dynamic.cpp",
        "exe": BENCH_DIR / "reduction_cpp_dynamic.exe",
        "compile_cmd": [CLANGPP_PATH, "-O3", "-mavx2", "-fopenmp", str(BENCH_DIR / "reduction_cpp_dynamic.cpp"), "-o", str(BENCH_DIR / "reduction_cpp_dynamic.exe")],
    },
    {
        "id": "rust_scoped_threads_reduction",
        "name": "Rust Scoped Threads (`thread::scope` + JoinHandle reduction)",
        "lang": "Rust",
        "src": BENCH_DIR / "reduction_rust.rs",
        "exe": BENCH_DIR / "reduction_rust.exe",
        "compile_cmd": [RUSTC_PATH, "-C", "opt-level=3", "-C", "target-cpu=native", str(BENCH_DIR / "reduction_rust.rs"), "-o", str(BENCH_DIR / "reduction_rust.exe")],
    },
]

def measure_compile(cmd):
    t0 = time.perf_counter()
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    t1 = time.perf_counter()
    if res.returncode != 0:
        print(f"Compilation failed: {res.stderr}")
        return -1
    return (t1 - t0) * 1000.0

def run_benchmark(exe_path, runs=10):
    times = []
    output_last = ""
    for _ in range(runs):
        t0 = time.perf_counter()
        res = subprocess.run([str(exe_path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        t1 = time.perf_counter()
        if res.returncode != 0:
            print(f"Run failed: {res.stderr}")
            return None, ""
        times.append((t1 - t0) * 1000.0)
        output_last = res.stdout.strip()
    return statistics.median(times), output_last

def main():
    print("=== TAMARIN PARALLEL REDUCTION BENCHMARK SUITE ===")
    results = []

    for cfg in CONFIGS:
        print(f"Compiling {cfg['name']}...")
        compile_time = measure_compile(cfg["compile_cmd"])
        bin_size = os.path.getsize(cfg["exe"]) / 1024.0 if cfg["exe"].exists() else 0.0

        print(f"Running {cfg['name']} (10 runs)...")
        med_time, out = run_benchmark(cfg["exe"], runs=10)

        checksum = "N/A"
        if "SUM=" in out and "DOT=" in out:
            parts = out.split(";")
            sum_val = [p for p in parts if p.startswith("SUM=")][0]
            dot_val = [p for p in parts if p.startswith("DOT=")][0]
            checksum = f"{sum_val}; {dot_val}"

        print(f"  -> Runtime: {med_time:.2f} ms, Checksum: {checksum}, Bin: {bin_size:.1f} KB, Compile: {compile_time:.1f} ms")

        results.append({
            "name": cfg["name"],
            "lang": cfg["lang"],
            "runtime_ms": med_time,
            "compile_ms": compile_time,
            "bin_kb": bin_size,
            "checksum": checksum,
        })

    # Generate Markdown Summary Table
    md = []
    md.append("# Parallel Reduction Benchmark: Tamarin v0.11 vs C++ OpenMP vs Rust\n")
    md.append("**Workload:** 100,000,000 parallel operations (100 parallel work chunks x 1,000,000 iterations of 8-wide AVX2 vector additions and 4-wide vector dot products with dual reductions).\n")
    md.append("| Implementation | Language | Runtime Median (ms) | Speedup vs Rust | Compile Time (ms) | Binary Size (KB) | Output Checksum |")
    md.append("| :--- | :---: | :---: | :---: | :---: | :---: | :---: |")

    rust_time = next(r["runtime_ms"] for r in results if r["lang"] == "Rust")

    for r in results:
        speedup = (rust_time / r["runtime_ms"]) if r["runtime_ms"] else 0.0
        md.append(f"| **{r['name']}** | {r['lang']} | **{r['runtime_ms']:.2f} ms** | **{speedup:.2f}x** | {r['compile_ms']:.1f} ms | {r['bin_kb']:.1f} KB | `{r['checksum']}` |")

    md.append("\n### Architectural Notes:")
    md.append("- **Tamarin Reduction Lowering:** Thread-local accumulator initialized on worker frame; registers promoted via LLVM `mem2reg` for zero memory traffic inside loop body; atomic accumulation via LLVM `atomicrmw add seq_cst` exactly once per chunk.")
    md.append("- **C++ OpenMP:** Tested under both `schedule(static)` and `schedule(dynamic)` with `#pragma omp parallel for reduction(+:...)` via LLVM Clang 23.1.5.")
    md.append("- **Rust:** Evaluated with idiomatic `std::thread::scope` where each scoped thread returns `(local_sum, local_dot)` by value from its JoinHandle, summed upon joining on the main thread (zero atomics, zero contention) under `rustc -C opt-level=3 -C target-cpu=native`.")
    md.append("- **Binary Size:** Tamarin links dynamically against Windows system runtime (`ucrtbase.dll`, `kernel32.dll`), whereas Clang (`libomp.dll` + static CRT) and Rust bundle unwind and panic runtimes.")

    summary_text = "\n".join(md)
    RESULTS_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(RESULTS_FILE, "w", encoding="utf-8") as f:
        f.write(summary_text)

    print("\nBenchmark Summary Written to:", RESULTS_FILE)
    print("\n" + summary_text)

if __name__ == "__main__":
    main()
