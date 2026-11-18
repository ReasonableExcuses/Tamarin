#!/usr/bin/env python3
"""
Tamarin Native SIMD vs C++ AVX2 vs Rust AVX2 Benchmark Runner
Evaluates 100 Million Vector Operations:
  - Runtime Speed (ms)
  - Compilation Speed (ms)
  - Binary Size (KB)
  - Numerical Checksum
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
RESULTS_FILE = REPO_ROOT / "benchmarks" / "results" / "simd_benchmark_summary.md"

CONFIGS = [
    {
        "id": "tamarin_simd",
        "name": "Tamarin Native SIMD (float4/float8)",
        "lang": "Tamarin",
        "src": BENCH_DIR / "simd_tamarin.tmn",
        "exe": BENCH_DIR / "simd_tamarin.exe",
        "compile_cmd": [str(TAMARIN_PATH), "-O3", str(BENCH_DIR / "simd_tamarin.tmn"), "-o", str(BENCH_DIR / "simd_tamarin.exe")],
    },
    {
        "id": "cpp_simd",
        "name": "C++ Clang AVX2 Intrinsics (__m256)",
        "lang": "C++",
        "src": BENCH_DIR / "simd_cpp.cpp",
        "exe": BENCH_DIR / "simd_cpp.exe",
        "compile_cmd": [CLANGPP_PATH, "-O3", "-mavx2", str(BENCH_DIR / "simd_cpp.cpp"), "-o", str(BENCH_DIR / "simd_cpp.exe")],
    },
    {
        "id": "rust_simd",
        "name": "Rust AVX2 Intrinsics (_mm256)",
        "lang": "Rust",
        "src": BENCH_DIR / "simd_rust.rs",
        "exe": BENCH_DIR / "simd_rust.exe",
        "compile_cmd": [RUSTC_PATH, "-C", "opt-level=3", "-C", "target-cpu=native", str(BENCH_DIR / "simd_rust.rs"), "-o", str(BENCH_DIR / "simd_rust.exe")],
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
    print("=== TAMARIN NATIVE SIMD BENCHMARK SUITE ===")
    results = []

    for cfg in CONFIGS:
        print(f"Compiling {cfg['name']}...")
        compile_time = measure_compile(cfg["compile_cmd"])
        bin_size = os.path.getsize(cfg["exe"]) / 1024.0 if cfg["exe"].exists() else 0.0

        print(f"Running {cfg['name']} (10 runs)...")
        med_time, out = run_benchmark(cfg["exe"], runs=10)

        # Extract checksum
        checksum = "N/A"
        if "SUM=" in out and "DOT=" in out:
            parts = out.split(";")
            sum_val = [p for p in parts if p.startswith("SUM=")][0]
            dot_val = [p for p in parts if p.startswith("DOT=")][0]
            checksum = f"{sum_val}; {dot_val}"

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
    md.append("# High-Performance SIMD Vector Benchmark: Tamarin vs C++ AVX2 vs Rust AVX2\n")
    md.append("**Workload:** 100,000,000 iterations of 8-wide AVX2 vector accumulation and 4-wide vector dot product reductions.\n")
    md.append("| Implementation | Language | Runtime Median (ms) | Speedup vs Rust | Compile Time (ms) | Binary Size (KB) | Output Checksum |")
    md.append("| :--- | :---: | :---: | :---: | :---: | :---: | :---: |")

    rust_time = next(r["runtime_ms"] for r in results if r["lang"] == "Rust")

    for r in results:
        speedup = rust_time / r["runtime_ms"] if r["runtime_ms"] else 0.0
        md.append(f"| **{r['name']}** | {r['lang']} | **{r['runtime_ms']:.2f} ms** | **{speedup:.2f}x** | {r['compile_ms']:.1f} ms | {r['bin_kb']:.1f} KB | `{r['checksum']}` |")

    md_content = "\n".join(md) + "\n"
    RESULTS_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(RESULTS_FILE, "w", encoding="utf-8") as f:
        f.write(md_content)

    print("\n" + md_content)
    print(f"Saved results to {RESULTS_FILE}")

if __name__ == "__main__":
    main()
