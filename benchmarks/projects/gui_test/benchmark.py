#!/usr/bin/env python3
"""
Comprehensive Benchmark & Comparison: GUI Calculator
Tamarin vs C++ vs Rust
"""

import os
import sys
import time
import ctypes
from ctypes import wintypes
import pathlib
import subprocess
import shutil

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
GUI_TEST_DIR = REPO_ROOT / "benchmarks" / "projects" / "gui_test"
TAMARIN_DIR = GUI_TEST_DIR / "tamarin"
CPP_DIR = GUI_TEST_DIR / "cpp"
RUST_DIR = GUI_TEST_DIR / "rust"
NATIVE_RAYLIB_DIR = REPO_ROOT / "lib" / "raylib" / "native"
TROOP_EXE = REPO_ROOT / "build" / "Release" / "troop.exe"

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

def count_sloc(file_path: pathlib.Path) -> int:
    if not file_path.exists():
        return 0
    lines = file_path.read_text(encoding="utf-8").splitlines()
    count = 0
    for line in lines:
        stripped = line.strip()
        if stripped and not stripped.startswith("//") and not stripped.startswith("/*") and not stripped.startswith("*"):
            count += 1
    return count

def measure_compilation(lang: str) -> tuple[float, pathlib.Path]:
    print(f"\n[Compiling {lang} (Release Mode)]...")
    t0 = time.perf_counter()
    if lang == "Tamarin":
        # Clean previous bin
        bin_dir = TAMARIN_DIR / "bin"
        if bin_dir.exists():
            shutil.rmtree(bin_dir, ignore_errors=True)
        cmd = [str(TROOP_EXE), "build", "--release"]
        res = subprocess.run(cmd, cwd=TAMARIN_DIR, capture_output=True, text=True)
        t1 = time.perf_counter()
        if res.returncode != 0:
            print("Tamarin compilation failed:", res.stderr)
            sys.exit(1)
        exe_path = TAMARIN_DIR / "bin" / "release" / "tamarin.exe"
        return (t1 - t0), exe_path

    elif lang == "C++":
        exe_path = CPP_DIR / "calculator_cpp.exe"
        if exe_path.exists():
            exe_path.unlink()
        cmd = [
            "clang++", "-O3", "-std=c++17", "main.cpp",
            "-I.", f"-L{NATIVE_RAYLIB_DIR}",
            "-lraylib", "-lwinmm", "-lgdi32", "-luser32", "-lshell32", "-lopengl32",
            "-o", "calculator_cpp.exe"
        ]
        res = subprocess.run(cmd, cwd=CPP_DIR, capture_output=True, text=True)
        t1 = time.perf_counter()
        if res.returncode != 0:
            print("C++ compilation failed:", res.stderr)
            sys.exit(1)
        # Ensure raylib.dll is present
        shutil.copy(NATIVE_RAYLIB_DIR / "raylib.dll", CPP_DIR / "raylib.dll")
        return (t1 - t0), exe_path

    elif lang == "Rust":
        target_dir = RUST_DIR / "target"
        # Run clean to test fresh build speed
        subprocess.run(["cargo", "clean"], cwd=RUST_DIR, capture_output=True)
        cmd = ["cargo", "build", "--release"]
        t0 = time.perf_counter()
        res = subprocess.run(cmd, cwd=RUST_DIR, capture_output=True, text=True)
        t1 = time.perf_counter()
        if res.returncode != 0:
            print("Rust compilation failed:", res.stderr)
            sys.exit(1)
        exe_path = RUST_DIR / "target" / "release" / "calculator_rust.exe"
        shutil.copy(NATIVE_RAYLIB_DIR / "raylib.dll", RUST_DIR / "target" / "release" / "raylib.dll")
        return (t1 - t0), exe_path

    raise ValueError(f"Unknown lang {lang}")

def run_performance_test(exe_path: pathlib.Path, num_frames: int = 300) -> dict:
    env = os.environ.copy()
    env["TAMARIN_CALC_BENCH"] = str(num_frames)

    # Launch process
    t0 = time.perf_counter()
    p = subprocess.Popen(
        [str(exe_path)],
        cwd=exe_path.parent,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        text=True
    )

    h_proc = None
    if sys.platform == "win32":
        PROCESS_QUERY_INFORMATION = 0x0400
        PROCESS_VM_READ = 0x0010
        h_proc = ctypes.windll.kernel32.OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, p.pid
        )

    peak_working_set = 0.0

    while p.poll() is None:
        if h_proc:
            pmc = PROCESS_MEMORY_COUNTERS()
            pmc.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
            if ctypes.windll.psapi.GetProcessMemoryInfo(h_proc, ctypes.byref(pmc), pmc.cb):
                mb = pmc.WorkingSetSize / (1024 * 1024)
                if mb > peak_working_set:
                    peak_working_set = mb
        time.sleep(0.005)

    stdout, stderr = p.communicate()
    t1 = time.perf_counter()

    if h_proc:
        pmc = PROCESS_MEMORY_COUNTERS()
        pmc.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
        if ctypes.windll.psapi.GetProcessMemoryInfo(h_proc, ctypes.byref(pmc), pmc.cb):
            mb = pmc.PeakWorkingSetSize / (1024 * 1024)
            if mb > peak_working_set:
                peak_working_set = mb
        ctypes.windll.kernel32.CloseHandle(h_proc)

    elapsed_s = t1 - t0
    fps = num_frames / elapsed_s if elapsed_s > 0 else 0
    ms_per_frame = (elapsed_s * 1000.0) / num_frames if num_frames > 0 else 0

    return {
        "exit_code": p.returncode,
        "elapsed_s": elapsed_s,
        "fps": fps,
        "ms_per_frame": ms_per_frame,
        "peak_ram_mb": peak_working_set,
        "binary_size_kb": exe_path.stat().st_size / 1024.0,
    }

def main():
    print("=" * 72)
    print(" TAMARIN vs C++ vs RUST: GUI CALCULATOR ECOSYSTEM BENCHMARK")
    print(" Framework: Raylib 5.5 Native Rendering Engine")
    print(" Platform: Windows 11 x86_64 (AMD Ryzen 7 7840HS)")
    print("=" * 72)

    sloc = {
        "Tamarin": count_sloc(TAMARIN_DIR / "src" / "main.tmn"),
        "C++": count_sloc(CPP_DIR / "main.cpp"),
        "Rust": count_sloc(RUST_DIR / "src" / "main.rs"),
    }

    results = {}

    for lang in ["Tamarin", "C++", "Rust"]:
        compile_time, exe = measure_compilation(lang)
        print(f" -> {lang} compiled in {compile_time:.2f}s (Binary size: {exe.stat().st_size / 1024.0:.1f} KB)")
        print(f" -> Running 300-frame benchmark for {lang}...")
        perf = run_performance_test(exe, num_frames=300)
        perf["compile_time_s"] = compile_time
        perf["sloc"] = sloc[lang]
        results[lang] = perf
        print(f"    Elapsed: {perf['elapsed_s']:.3f}s | FPS: {perf['fps']:.1f} | Frame: {perf['ms_per_frame']:.2f}ms | RAM: {perf['peak_ram_mb']:.2f} MB")

    print("\n" + "=" * 72)
    print(" BENCHMARK RESULTS & METRICS SUMMARY")
    print("=" * 72)

    header = f"{'Metric':<30} | {'Tamarin':<12} | {'C++':<12} | {'Rust':<12}"
    print(header)
    print("-" * len(header))

    def row(label, key, fmt="{:.2f}", unit=""):
        t_val = fmt.format(results["Tamarin"][key]) + unit
        c_val = fmt.format(results["C++"][key]) + unit
        r_val = fmt.format(results["Rust"][key]) + unit
        print(f"{label:<30} | {t_val:<12} | {c_val:<12} | {r_val:<12}")

    row("Release Build Time", "compile_time_s", "{:.2f}", " s")
    row("Binary Size", "binary_size_kb", "{:.1f}", " KB")
    row("Total Runtime (300 frames)", "elapsed_s", "{:.3f}", " s")
    row("Render Time / Frame", "ms_per_frame", "{:.2f}", " ms")
    row("Effective Framerate", "fps", "{:.1f}", " FPS")
    row("Peak Working Set RAM", "peak_ram_mb", "{:.2f}", " MB")
    row("Source Code (SLOC)", "sloc", "{:d}", " lines")

    print("=" * 72)

if __name__ == "__main__":
    main()
