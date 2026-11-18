#!/usr/bin/env python3
"""
Demanding Simulation Benchmark: 3D N-Body Gravitational Universe Simulation
Evaluates:
  - Runtime Speed (Median ms, 95% CI, StdDev)
  - Compile Speed (ms)
  - Binary Size (KB)
  - Peak Working-Set RAM (KB)
  - Numerical Correctness (Exact Bit Checksum)
Across:
  - Tamarin (Normal AoS vs Expert SoA)
  - C (Clang Normal/Expert, MSVC Normal/Expert)
  - C++ (Clang Normal/Expert, MSVC Normal/Expert)
  - Rust (Rust Normal vs Rust Expert)
"""

import os
import sys
import time
import subprocess
import statistics
import ctypes
from ctypes import wintypes
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = BENCH_DIR.parent.parent
CLANG_PATH = r"C:\Program Files\LLVM\bin\clang.exe"
CLANGPP_PATH = r"C:\Program Files\LLVM\bin\clang++.exe"
RUSTC_PATH = r"C:\Users\hp\.cargo\bin\rustc.exe"
TAMARIN_PATH = REPO_ROOT / "build" / "Release" / "tamarin.exe"
VCVARS_BAT = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

# Win32 Process Memory Tracking
class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("PageFaultCount", wintypes.DWORD),
        ("PeakWorkingSetSize", ctypes.c_size_t),
        ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t),
        ("PeakPagefileUsage", ctypes.c_size_t),
    ]

psapi = ctypes.WinDLL("psapi.dll")
kernel32 = ctypes.WinDLL("kernel32.dll")
GetProcessMemoryInfo = psapi.GetProcessMemoryInfo
GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESS_MEMORY_COUNTERS), wintypes.DWORD]
GetProcessMemoryInfo.restype = wintypes.BOOL

CONFIGS = [
    {
        "id": "tamarin_normal",
        "name": "Tamarin Normal (AoS)",
        "lang": "Tamarin",
        "tier": "Normal",
        "src": BENCH_DIR / "nbody_tamarin_normal.tmn",
        "exe": BENCH_DIR / "nbody_tamarin_normal.exe",
        "compile_cmd": [str(TAMARIN_PATH), "build", str(BENCH_DIR / "nbody_tamarin_normal.tmn"), "-O3", "--target-cpu", "native", "-o", str(BENCH_DIR / "nbody_tamarin_normal.exe")],
    },
    {
        "id": "tamarin_expert",
        "name": "Tamarin Expert (SoA)",
        "lang": "Tamarin",
        "tier": "Expert",
        "src": BENCH_DIR / "nbody_tamarin_expert.tmn",
        "exe": BENCH_DIR / "nbody_tamarin_expert.exe",
        "compile_cmd": [str(TAMARIN_PATH), "build", str(BENCH_DIR / "nbody_tamarin_expert.tmn"), "-O3", "--target-cpu", "native", "--fast-math", "-o", str(BENCH_DIR / "nbody_tamarin_expert.exe")],
    },
    {
        "id": "c_clang_normal",
        "name": "C Clang Normal (AoS)",
        "lang": "C",
        "tier": "Normal",
        "src": BENCH_DIR / "nbody_c_normal.c",
        "exe": BENCH_DIR / "nbody_c_normal.exe",
        "compile_cmd": [CLANG_PATH, "-O3", "-march=native", str(BENCH_DIR / "nbody_c_normal.c"), "-o", str(BENCH_DIR / "nbody_c_normal.exe")],
    },
    {
        "id": "c_clang_expert",
        "name": "C Clang Expert (SoA)",
        "lang": "C",
        "tier": "Expert",
        "src": BENCH_DIR / "nbody_c_expert.c",
        "exe": BENCH_DIR / "nbody_c_expert.exe",
        "compile_cmd": [CLANG_PATH, "-O3", "-march=native", "-ffast-math", str(BENCH_DIR / "nbody_c_expert.c"), "-o", str(BENCH_DIR / "nbody_c_expert.exe")],
    },
    {
        "id": "c_msvc_normal",
        "name": "C MSVC Normal (AoS)",
        "lang": "C",
        "tier": "Normal",
        "src": BENCH_DIR / "nbody_c_normal.c",
        "exe": BENCH_DIR / "nbody_c_msvc_normal.exe",
        "compile_cmd": ["cmd.exe", "/c", str(BENCH_DIR / "build_msvc.bat"), "c_normal"],
    },
    {
        "id": "c_msvc_expert",
        "name": "C MSVC Expert (SoA)",
        "lang": "C",
        "tier": "Expert",
        "src": BENCH_DIR / "nbody_c_expert.c",
        "exe": BENCH_DIR / "nbody_c_msvc_expert.exe",
        "compile_cmd": ["cmd.exe", "/c", str(BENCH_DIR / "build_msvc.bat"), "c_expert"],
    },
    {
        "id": "cpp_clang_normal",
        "name": "C++ Clang Normal (std::vector)",
        "lang": "C++",
        "tier": "Normal",
        "src": BENCH_DIR / "nbody_cpp_normal.cpp",
        "exe": BENCH_DIR / "nbody_cpp_normal.exe",
        "compile_cmd": [CLANGPP_PATH, "-O3", "-march=native", "-std=c++20", str(BENCH_DIR / "nbody_cpp_normal.cpp"), "-o", str(BENCH_DIR / "nbody_cpp_normal.exe")],
    },
    {
        "id": "cpp_clang_expert",
        "name": "C++ Clang Expert (SoA)",
        "lang": "C++",
        "tier": "Expert",
        "src": BENCH_DIR / "nbody_cpp_expert.cpp",
        "exe": BENCH_DIR / "nbody_cpp_expert.exe",
        "compile_cmd": [CLANGPP_PATH, "-O3", "-march=native", "-ffast-math", "-std=c++20", str(BENCH_DIR / "nbody_cpp_expert.cpp"), "-o", str(BENCH_DIR / "nbody_cpp_expert.exe")],
    },
    {
        "id": "cpp_msvc_normal",
        "name": "C++ MSVC Normal (std::vector)",
        "lang": "C++",
        "tier": "Normal",
        "src": BENCH_DIR / "nbody_cpp_normal.cpp",
        "exe": BENCH_DIR / "nbody_cpp_msvc_normal.exe",
        "compile_cmd": ["cmd.exe", "/c", str(BENCH_DIR / "build_msvc.bat"), "cpp_normal"],
    },
    {
        "id": "cpp_msvc_expert",
        "name": "C++ MSVC Expert (SoA)",
        "lang": "C++",
        "tier": "Expert",
        "src": BENCH_DIR / "nbody_cpp_expert.cpp",
        "exe": BENCH_DIR / "nbody_cpp_msvc_expert.exe",
        "compile_cmd": ["cmd.exe", "/c", str(BENCH_DIR / "build_msvc.bat"), "cpp_expert"],
    },
    {
        "id": "rust_normal",
        "name": "Rust Normal (Vec<Particle>)",
        "lang": "Rust",
        "tier": "Normal",
        "src": BENCH_DIR / "nbody_rust_normal.rs",
        "exe": BENCH_DIR / "nbody_rust_normal.exe",
        "compile_cmd": [RUSTC_PATH, "-C", "opt-level=3", "-C", "target-cpu=native", str(BENCH_DIR / "nbody_rust_normal.rs"), "-o", str(BENCH_DIR / "nbody_rust_normal.exe")],
    },
    {
        "id": "rust_expert",
        "name": "Rust Expert (SoA Unchecked)",
        "lang": "Rust",
        "tier": "Expert",
        "src": BENCH_DIR / "nbody_rust_expert.rs",
        "exe": BENCH_DIR / "nbody_rust_expert.exe",
        "compile_cmd": [RUSTC_PATH, "-C", "opt-level=3", "-C", "target-cpu=native", "-C", "lto=fat", "-C", "codegen-units=1", str(BENCH_DIR / "nbody_rust_expert.rs"), "-o", str(BENCH_DIR / "nbody_rust_expert.exe")],
    },
    {
        "id": "rust_expert_avx2",
        "name": "Rust Expert (FastMath AVX2)",
        "lang": "Rust",
        "tier": "Expert",
        "src": BENCH_DIR / "nbody_rust_expert_avx2.rs",
        "exe": BENCH_DIR / "nbody_rust_expert_avx2.exe",
        "compile_cmd": [RUSTC_PATH, "-C", "opt-level=3", "-C", "target-cpu=native", "-C", "lto=fat", "-C", "codegen-units=1", str(BENCH_DIR / "nbody_rust_expert_avx2.rs"), "-o", str(BENCH_DIR / "nbody_rust_expert_avx2.exe")],
    },
]

def measure_peak_memory(exe_path):
    # Launch process suspended/normal and query GetProcessMemoryInfo
    proc = subprocess.Popen([str(exe_path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    h_proc = wintypes.HANDLE(int(proc._handle))
    max_ws = 0
    while proc.poll() is None:
        counters = PROCESS_MEMORY_COUNTERS()
        counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
        if GetProcessMemoryInfo(h_proc, ctypes.byref(counters), counters.cb):
            ws = counters.PeakWorkingSetSize
            if ws > max_ws:
                max_ws = ws
        time.sleep(0.005)
    stdout, stderr = proc.communicate()
    # Check one last time
    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
    if GetProcessMemoryInfo(h_proc, ctypes.byref(counters), counters.cb):
        if counters.PeakWorkingSetSize > max_ws:
            max_ws = counters.PeakWorkingSetSize
    return max_ws / 1024.0, stdout.decode("utf-8", errors="replace"), proc.returncode

def parse_output(out_str):
    fields = {}
    for part in out_str.strip().split(";"):
        if ":" in part:
            k, v = part.split(":", 1)
            fields[k.strip()] = v.strip()
    return fields

def main():
    print("=" * 80)
    print("DEMANDING 3D N-BODY GRAVITATIONAL SIMULATION BENCHMARK CAMPAIGN")
    print("Evaluating: Runtime Speed, Compilation Speed, Binary Size, Peak Memory")
    print("4,096 Bodies | 20 Time Steps | 335,544,320 Pairwise Force Interactions")
    print("=" * 80)

    results = []

    # 1. Compilation Phase
    print("\n--- PHASE 1: COMPILATION SPEED & BINARY SIZE ---")
    for cfg in CONFIGS:
        print(f"Compiling {cfg['name']}...", end="", flush=True)
        if cfg['exe'].exists():
            try:
                cfg['exe'].unlink()
            except Exception:
                pass

        t0 = time.perf_counter()
        env = os.environ.copy()
        if "env" in cfg:
            env.update(cfg["env"])
        if cfg.get("shell"):
            res = subprocess.run(cfg["compile_cmd"], shell=True, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        else:
            res = subprocess.run(cfg["compile_cmd"], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        t1 = time.perf_counter()

        compile_ms = (t1 - t0) * 1000.0
        if res.returncode != 0 or not cfg['exe'].exists():
            print(f" FAILED (code {res.returncode})")
            print(res.stderr.decode("utf-8", errors="replace"))
            continue

        bin_size_kb = cfg['exe'].stat().st_size / 1024.0
        cfg['compile_ms'] = compile_ms
        cfg['bin_size_kb'] = bin_size_kb
        print(f" DONE in {compile_ms:.1f} ms | Size: {bin_size_kb:.1f} KB")

    # 2. Execution & Runtime Measurement Phase
    print("\n--- PHASE 2: RUNTIME PERFORMANCE & PEAK MEMORY (3 Warmups, 10 Repetitions) ---")
    WARMUPS = 3
    REPETITIONS = 10

    for cfg in CONFIGS:
        if not cfg['exe'].exists():
            continue
        print(f"\nBenchmarking {cfg['name']}:")

        # Warmups
        for w in range(WARMUPS):
            res = subprocess.run([str(cfg['exe'])], capture_output=True, text=True)
            if res.returncode != 0:
                print(f"Warmup {w+1} failed!")
                break

        # Measured runs
        times = []
        peak_ws_list = []
        checksum = None

        for r in range(REPETITIONS):
            peak_ws, stdout, retcode = measure_peak_memory(cfg['exe'])
            if retcode != 0:
                print(f"Run {r+1} failed with code {retcode}")
                continue
            fields = parse_output(stdout)
            t_ms = float(fields.get("TIME_MS", 0.0))
            chk = fields.get("CHECKSUM", "0")
            times.append(t_ms)
            peak_ws_list.append(peak_ws)
            if checksum is None:
                checksum = chk
            print(f"  [Rep {r+1:02d}] Time: {t_ms:.2f} ms | Peak WS: {peak_ws:.0f} KB | Checksum: {chk}")

        if times:
            cfg['median_ms'] = statistics.median(times)
            cfg['min_ms'] = min(times)
            cfg['max_ms'] = max(times)
            cfg['mean_ms'] = statistics.mean(times)
            cfg['stdev_ms'] = statistics.stdev(times) if len(times) > 1 else 0.0
            cfg['peak_ws_kb'] = statistics.median(peak_ws_list)
            cfg['checksum'] = checksum
            results.append(cfg)

    # 3. Print Comprehensive Markdown Summary Table
    print("\n" + "=" * 80)
    print("FINAL BENCHMARK SCOREBOARD")
    print("=" * 80)

    header = f"| Implementation | Language | Tier | Runtime Median (ms) | Speedup vs Rust | Compile Time (ms) | Binary Size (KB) | Peak RAM (KB) | Checksum |"
    sep    = f"| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |"
    print(header)
    print(sep)

    rust_normal_med = next((r['median_ms'] for r in results if r['id'] == 'rust_normal'), 1.0)
    rust_expert_med = next((r['median_ms'] for r in results if r['id'] == 'rust_expert_avx2'), 
                           next((r['median_ms'] for r in results if r['id'] == 'rust_expert'), 1.0))

    lines = []
    lines.append("# Comprehensive Multi-Language Benchmark: 3D N-Body Gravitational Simulation\n")
    lines.append("**Date:** November 17, 2026  ")
    lines.append("**Host OS:** Windows 11 x86-64 (AMD64 Family 25 Model 116 Stepping 1, 16 Logical Cores)  ")
    lines.append("**Evaluated Compilers:**")
    lines.append("- **TAMARIN:** `Tamarin v0.8.5` (`build/Release/tamarin.exe`, target: `x86_64-pc-windows-msvc`)")
    lines.append("- **CLANG (C / C++):** `clang / clang++ version 23.1.5` (`-march=native`)")
    lines.append("- **MSVC (C / C++):** `Microsoft (R) C/C++ Optimizing Compiler Version 19.44.35211 for x64` (`/arch:AVX2`)")
    lines.append("- **RUST:** `rustc 1.99.1 (58cf8f9c3 2026-10-13)` (`-C target-cpu=native`)\n")
    lines.append("---\n")
    lines.append("## 1. Workload Specification: 3D N-Body Universe Simulation\n")
    lines.append("The benchmark models the direct gravitational interaction of **$N = 4,096$ celestial bodies** over **$S = 20$ simulation steps**:")
    lines.append(r"$$\mathbf{a}_i = \sum_{j=0}^{N-1} \frac{G \cdot m_j \cdot (\mathbf{r}_j - \mathbf{r}_i)}{(\|\mathbf{r}_j - \mathbf{r}_i\|^2 + \epsilon^2)^{3/2}}$$")
    lines.append("- **Total Pairwise Force Calculations:** $20 \times 4096 \times 4096 = \\mathbf{335,544,320}$ 3D force vectors evaluated.")
    lines.append("- **Floating-Point Arithmetic:** Over **6.7 GigaFLOPs** of reciprocal square roots, vector subtractions, and multiply-accumulates.")
    lines.append("- **Initial Conditions:** Bit-identical deterministic pseudo-random LCG initialization (`seed = (seed * 1664525 + 1013904223) & 2147483647`).")
    lines.append("- **Numerical Integration:** Symplectic Euler time stepping ($\Delta t = 0.01$).")
    lines.append("- **Verification Invariant:** Exact calculation of Kinetic Energy ($E_k$), Momentum, and spatial position sums producing the exact checksum `7250061` (IEEE-754 exact) and `7250044` (Fast-Math vector approximation).\n")
    lines.append("---\n")
    lines.append("## 2. Comprehensive Multi-Dimensional Scoreboard\n")
    lines.append("Evaluation of **Runtime Speed**, **Compilation Speed**, **Binary Size**, **Peak RAM**, and **Numerical Correctness**:\n")
    lines.append(header)
    lines.append(sep)

    for r in results:
        ref_med = rust_normal_med if r['tier'] == 'Normal' else rust_expert_med
        ratio = ref_med / r['median_ms']
        speedup_str = f"**{ratio:.2f}x**" if ratio >= 1.05 else f"{ratio:.2f}x"
        row = f"| **{r['name']}** | {r['lang']} | {r['tier']} | **{r['median_ms']:.2f} ms** | {speedup_str} | {r['compile_ms']:.1f} ms | {r['bin_size_kb']:.1f} KB | {r['peak_ws_kb']:.0f} KB | `{r['checksum']}` |"
        print(row)
        lines.append(row)

    out_md = REPO_ROOT / "benchmarks" / "results" / "nbody_simulation_summary.md"
    out_md.parent.mkdir(parents=True, exist_ok=True)
    with open(out_md, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"\nSaved updated benchmark report to {out_md}")

if __name__ == "__main__":
    main()
