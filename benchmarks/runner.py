#!/usr/bin/env python3
"""
TAMARIN v0.5.9 — Expert Performance Optimization

Defensible, reproducible, strictly-validated performance comparison between:
- TAMARIN (-O0, -O1, -O2, -O3)
- TAMARIN Expert (-O3 --target-cpu native with compiler intrinsics, @inline, and @unroll)
- Clang C Idiomatic (-O2, -O3) and Expert (-O3 -march=native)
- MSVC C Idiomatic (/O2) and Expert (/O2 /arch:AVX2)
- Safe Rust Idiomatic (Release, Release-LTO) and Expert (Release-LTO + target-cpu=native)

Separates Mode A (internal high-precision QPC computational timing) from
Mode B (external parent-process startup measurement).
Enforces exact field-by-field verification, fail-fast contract validation,
bootstrap 95% confidence intervals, peak working-set memory measurement,
and geometric-mean ratio reporting across workloads.
"""

import argparse
import ctypes
from ctypes import wintypes
import json
import math
import os
import platform
import random
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

# ── Paths ────────────────────────────────────────────────────────────────────
ROOT_DIR = Path(__file__).resolve().parent.parent
BENCHMARKS_DIR = ROOT_DIR / "benchmarks"
BUILD_BIN_DIR = BENCHMARKS_DIR / "build" / "bin"
BUILD_OBJ_DIR = BENCHMARKS_DIR / "build" / "obj"
ASSEMBLY_DIR = BENCHMARKS_DIR / "assembly"
RESULTS_DIR = BENCHMARKS_DIR / "results"

COMPUTE_WORKLOADS = [
    "01_integer",
    "02_float",
    "03_arrays",
    "04_structures_aos",
    "04_structures_soa",
    "05_function_calls",
    "06_branching",
    "07_virtual_dispatch",
    "08_memory_ops",
    "10_region_alloc",
    "10a_high_churn",
    "10b_large_single",
    "10c_chunk_growth",
    "10d_empty_region",
    "10e_sparse_dtors",
    "10f_reuse",
    "12_adt_matching",
]

STARTUP_WORKLOAD = "09_startup"
ALL_WORKLOADS = COMPUTE_WORKLOADS + [STARTUP_WORKLOAD]

STANDARD_FULL_CONFIGS = [
    "tamarin_o0",
    "tamarin_o1",
    "tamarin_o2",
    "tamarin_o3",
    "tamarin_expert",
    "c_clang_o2",
    "c_clang_o3",
    "c_msvc_o2",
    "rust",
    "rust_lto",
    "c_clang_expert",
    "c_msvc_expert",
    "rust_expert",
]

# ── Windows Process Memory Instrumentation ───────────────────────────────────
class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    _fields_ = [
        ('cb', wintypes.DWORD),
        ('PageFaultCount', wintypes.DWORD),
        ('PeakWorkingSetSize', ctypes.c_size_t),
        ('WorkingSetSize', ctypes.c_size_t),
        ('QuotaPeakPagedPoolUsage', ctypes.c_size_t),
        ('QuotaPagedPoolUsage', ctypes.c_size_t),
        ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t),
        ('QuotaNonPagedPoolUsage', ctypes.c_size_t),
        ('PagefileUsage', ctypes.c_size_t),
        ('PeakPagefileUsage', ctypes.c_size_t),
    ]

def get_process_peak_ws_kb(proc) -> float:
    try:
        pmc = PROCESS_MEMORY_COUNTERS()
        pmc.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
        if ctypes.windll.psapi.GetProcessMemoryInfo(int(proc._handle), ctypes.byref(pmc), pmc.cb):
            return round(pmc.PeakWorkingSetSize / 1024.0, 1)
    except Exception:
        pass
    return 0.0

def compute_geomean(ratios) -> float:
    valid = [r for r in ratios if isinstance(r, (int, float)) and r > 0 and math.isfinite(r)]
    if not valid:
        return 0.0
    log_sum = sum(math.log(r) for r in valid)
    return round(math.exp(log_sum / len(valid)), 3)

# ── Exact Result Schemas and Reference Fixtures ──────────────────────────────
EXPECTED_SCHEMAS = {
    "01_integer": {
        "fields": ["PRIMES", "COLLATZ", "H", "CHECKSUM"],
        "fixtures": {
            "PRIMES": "2262",
            "COLLATZ": "1834634",
            "H": "1370357862",
            "CHECKSUM": "1372194758",
        },
    },
    "02_float": {
        "fields": ["MANDEL", "MATMUL", "CHECKSUM"],
        "fixtures": {
            "MANDEL": "4703990612284669952",
            "MATMUL": "4693648769122566158",
            "CHECKSUM": "4705341001542139908",
        },
    },
    "03_arrays": {
        "fields": ["SCAN", "TRANSFORM", "FINAL", "CHECKSUM"],
        "fixtures": {
            "SCAN": "636540000",
            "TRANSFORM": "256601600",
            "FINAL": "128766936",
            "CHECKSUM": "1021908536",
        },
    },
    "04_structures_aos": {
        "fields": ["TOTAL_X", "TOTAL_Y", "TOTAL_Z", "TOTAL_V", "CHECKSUM"],
        "fixtures": {
            "TOTAL_X": "307760",
            "TOTAL_Y": "308284",
            "TOTAL_Z": "81787",
            "TOTAL_V": "21249",
            "CHECKSUM": "719080",
        },
    },
    "04_structures_soa": {
        "fields": ["TOTAL_X", "TOTAL_Y", "TOTAL_Z", "TOTAL_V", "CHECKSUM"],
        "fixtures": {
            "TOTAL_X": "307760",
            "TOTAL_Y": "308284",
            "TOTAL_Z": "81787",
            "TOTAL_V": "21249",
            "CHECKSUM": "719080",
        },
    },
    "05_function_calls": {
        "fields": ["FIB", "ACC", "CHECKSUM"],
        "fixtures": {
            "FIB": "5702887",
            "ACC": "556720966",
            "CHECKSUM": "562423853",
        },
    },
    "06_branching": {
        "fields": ["PRED", "UNPRED", "CHECKSUM"],
        "fixtures": {
            "PRED": "21874987500000",
            "UNPRED": "17479387",
            "CHECKSUM": "21875004979387",
        },
    },
    "07_virtual_dispatch": {
        "fields": ["DIRECT", "VIRTUAL", "CHECKSUM"],
        "fixtures": {
            "DIRECT": "850000000",
            "VIRTUAL": "850000000",
            "CHECKSUM": "1700000000",
        },
    },
    "08_memory_ops": {
        "fields": ["TRANSFORMS", "MATCHES", "BYTE_SUM", "CHECKSUM"],
        "fixtures": {
            "TRANSFORMS": "3276800",
            "MATCHES": "63000",
            "BYTE_SUM": "1531860",
            "CHECKSUM": "4871660",
        },
    },
    "09_startup": {
        "fields": [],
        "fixtures": {},
    },
    "10_region_alloc": {
        "fields": ["PARTICLES", "SCRATCH", "NESTED", "CHECKSUM"],
        "fixtures": {
            "PARTICLES": "124999750000",
            "SCRATCH": "6816132500",
            "NESTED": "3877875000",
            "CHECKSUM": "135693757500",
        },
    },
    "10a_high_churn": {
        "fields": ["SUM", "CHECKSUM"],
        "fixtures": {
            "SUM": "319999600000",
            "CHECKSUM": "319999600000",
        },
    },
    "10b_large_single": {
        "fields": ["SUM", "CHECKSUM"],
        "fixtures": {
            "SUM": "3199960000",
            "CHECKSUM": "3199960000",
        },
    },
    "10c_chunk_growth": {
        "fields": ["SUM", "CHECKSUM"],
        "fixtures": {
            "SUM": "781249375000",
            "CHECKSUM": "781249375000",
        },
    },
    "10d_empty_region": {
        "fields": ["SUM", "CHECKSUM"],
        "fixtures": {
            "SUM": "50000",
            "CHECKSUM": "50000",
        },
    },
    "10e_sparse_dtors": {
        "fields": ["SUM", "CHECKSUM"],
        "fixtures": {
            "SUM": "7998000",
            "CHECKSUM": "7998000",
        },
    },
    "10f_reuse": {
        "fields": ["SUM", "CHECKSUM"],
        "fixtures": {
            "SUM": "4999950000",
            "CHECKSUM": "4999950000",
        },
    },
    "12_adt_matching": {
        "fields": ["ADT_SUM", "TRY_SUM", "NICHE_SUM", "CHECKSUM"],
        "fixtures": {
            "ADT_SUM": "32938631754",
            "TRY_SUM": "21492632433",
            "NICHE_SUM": "187132552",
            "CHECKSUM": "54618396739",
        },
    },
}

DEFAULT_CLANG_PATHS = [
    r"C:\Program Files\LLVM\bin\clang.exe",
    shutil.which("clang") or "",
]

DEFAULT_VCVARS_PATHS = [
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    r"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
    r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
]


class BenchmarkValidationError(Exception):
    """Raised when an execution or output contract validation fails."""
    pass


class ToolchainDiscovery:
    def __init__(self, tamarin_exe=None, clang_exe=None, vcvars_bat=None,
                 cargo_exe=None, rustc_exe=None, build_dir=None):
        self.build_dir = Path(build_dir).resolve() if build_dir else ROOT_DIR / "build"
        default_tamarin = self.build_dir / "Release" / "tamarin.exe"
        if not default_tamarin.exists():
            default_tamarin = self.build_dir / "Debug" / "tamarin.exe"

        env_tamarin = os.environ.get("TAMARIN_EXE")
        env_clang = os.environ.get("CLANG_EXE")
        env_vcvars = os.environ.get("VCVARS_BAT")
        env_cargo = os.environ.get("CARGO_EXE")
        env_rustc = os.environ.get("RUSTC_EXE")

        self.tamarin_exe = Path(tamarin_exe or env_tamarin or default_tamarin).resolve()
        self.clang_exe = self._resolve_path(clang_exe or env_clang, DEFAULT_CLANG_PATHS)
        self.vcvars_bat = self._resolve_path(vcvars_bat or env_vcvars, DEFAULT_VCVARS_PATHS)
        self.cargo_exe = cargo_exe or env_cargo or shutil.which("cargo") or "cargo"
        self.rustc_exe = rustc_exe or env_rustc or shutil.which("rustc") or "rustc"

        self.msvc_env = self._get_msvc_env()
        self.versions = self._probe_versions()

    def _resolve_path(self, explicit, defaults):
        if explicit and Path(explicit).exists():
            return str(Path(explicit).resolve())
        for d in defaults:
            if d and Path(d).exists():
                return str(Path(d).resolve())
        return None

    def _get_msvc_env(self):
        if not self.vcvars_bat:
            return None
        temp_bat = BENCHMARKS_DIR / "build" / "get_vcvars.bat"
        temp_bat.parent.mkdir(parents=True, exist_ok=True)
        with open(temp_bat, "w") as f:
            f.write(f'@call "{self.vcvars_bat}" >nul\n@set\n')
        try:
            res = subprocess.run(["cmd.exe", "/c", str(temp_bat)], capture_output=True, text=True, check=True)
            env = dict(os.environ)
            for line in res.stdout.splitlines():
                if "=" in line:
                    k, v = line.split("=", 1)
                    env[k] = v
            temp_bat.unlink(missing_ok=True)
            return env
        except Exception:
            temp_bat.unlink(missing_ok=True)
            return None

    def _probe_versions(self):
        vers = {}

        # Clang
        if self.clang_exe:
            try:
                res = subprocess.run([self.clang_exe, "--version"], capture_output=True, text=True, check=True)
                vers["clang"] = res.stdout.splitlines()[0].strip()
            except Exception as e:
                vers["clang"] = f"Unknown ({e})"
        else:
            vers["clang"] = "Not Available"

        # MSVC
        if self.msvc_env:
            try:
                res = subprocess.run(["cl.exe"], capture_output=True, text=True, env=self.msvc_env)
                banner = (res.stderr or res.stdout).splitlines()
                vers["msvc"] = banner[0].strip() if banner else "Microsoft (R) C/C++ Optimizing Compiler"
            except Exception as e:
                vers["msvc"] = f"Unknown ({e})"
        else:
            vers["msvc"] = "Not Available"

        # Rust
        try:
            res = subprocess.run([self.rustc_exe, "--version"], capture_output=True, text=True, check=True)
            vers["rustc"] = res.stdout.strip()
        except Exception as e:
            vers["rustc"] = f"Not Available ({e})"

        try:
            res = subprocess.run([self.cargo_exe, "--version"], capture_output=True, text=True, check=True)
            vers["cargo"] = res.stdout.strip()
        except Exception as e:
            vers["cargo"] = f"Not Available ({e})"

        # Tamarin
        if self.tamarin_exe and self.tamarin_exe.exists():
            try:
                res = subprocess.run([str(self.tamarin_exe), "--version"], capture_output=True, text=True, check=True)
                vers["tamarin"] = res.stdout.strip()
            except Exception as e:
                vers["tamarin"] = f"Unknown ({e})"
        else:
            vers["tamarin"] = f"Not Built ({self.tamarin_exe} not found)"

        return vers


def compile_binary(toolchain: ToolchainDiscovery, workload: str, config: str) -> Path:
    """Compiles a single benchmark binary with clean unlinking, assembly archiving, and strict error handling."""
    BUILD_BIN_DIR.mkdir(parents=True, exist_ok=True)
    BUILD_OBJ_DIR.mkdir(parents=True, exist_ok=True)
    ASSEMBLY_DIR.mkdir(parents=True, exist_ok=True)

    out_exe = BUILD_BIN_DIR / f"{workload}_{config}.exe"
    if out_exe.exists():
        out_exe.unlink()

    # TAMARIN Expert configuration (-O3 --target-cpu native with expert implementation)
    if config == "tamarin_expert":
        src = BENCHMARKS_DIR / "tamarin" / f"{workload}_expert.tmn"
        if not src.exists():
            src = BENCHMARKS_DIR / "tamarin" / f"{workload}.tmn"
        if not toolchain.tamarin_exe.exists():
            raise RuntimeError(f"Tamarin executable not found at: {toolchain.tamarin_exe}")
        cmd = [str(toolchain.tamarin_exe), "build", str(src), "-O3", "--target-cpu", "native", "--emit-asm", "-o", str(out_exe)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0 or not out_exe.exists():
            raise RuntimeError(f"Tamarin Expert build failed for {workload}:\nSTDOUT: {res.stdout}\nSTDERR: {res.stderr}")
        generated_asm = src.with_suffix(".s")
        if generated_asm.exists():
            shutil.copy2(generated_asm, ASSEMBLY_DIR / f"{workload}_{config}.s")
        return out_exe

    # TAMARIN configurations
    elif config.startswith("tamarin_"):
        opt = "-" + config.split("_")[1].upper()  # -O0, -O1, -O2, -O3
        src = BENCHMARKS_DIR / "tamarin" / f"{workload}.tmn"
        if not src.exists():
            raise FileNotFoundError(f"Missing Tamarin source: {src}")
        if not toolchain.tamarin_exe.exists():
            raise RuntimeError(f"Tamarin executable not found at: {toolchain.tamarin_exe}")
        target_cpu_args = ["--target-cpu", "native"] if config in ("tamarin_o2", "tamarin_o3") else []
        cmd = [str(toolchain.tamarin_exe), "build", str(src), opt] + target_cpu_args + ["--emit-asm", "-o", str(out_exe)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0 or not out_exe.exists():
            raise RuntimeError(f"Tamarin build failed for {workload} ({config}):\nSTDOUT: {res.stdout}\nSTDERR: {res.stderr}")
        generated_asm = src.with_suffix(".s")
        if generated_asm.exists():
            shutil.copy2(generated_asm, ASSEMBLY_DIR / f"{workload}_{config}.s")
        return out_exe

    # Clang C Expert configuration (-O3 -march=native with expert implementation)
    elif config == "c_clang_expert":
        src = BENCHMARKS_DIR / "c" / f"{workload}_expert.c"
        if not src.exists():
            src = BENCHMARKS_DIR / "c" / f"{workload}.c"
        if not toolchain.clang_exe:
            raise RuntimeError("Clang compiler not found")
        cmd = [toolchain.clang_exe, "-O3", "-march=native", "-Wall", str(src), "-o", str(out_exe)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0 or not out_exe.exists():
            raise RuntimeError(f"Clang Expert build failed for {workload}:\nSTDOUT: {res.stdout}\nSTDERR: {res.stderr}")
        asm_file = ASSEMBLY_DIR / f"{workload}_c_clang_expert.s"
        cmd_asm = [toolchain.clang_exe, "-O3", "-march=native", "-Wall", "-S", str(src), "-o", str(asm_file)]
        subprocess.run(cmd_asm, capture_output=True, text=True)
        return out_exe

    # Clang C Idiomatic configurations (-O2, -O3 with native CPU)
    elif config.startswith("c_clang_"):
        opt = "-" + config.split("_")[2].upper()  # -O2, -O3
        src = BENCHMARKS_DIR / "c" / f"{workload}.c"
        if not src.exists():
            raise FileNotFoundError(f"Missing C source: {src}")
        if not toolchain.clang_exe:
            raise RuntimeError("Clang compiler not found")
        cmd = [toolchain.clang_exe, opt, "-march=native", "-Wall", str(src), "-o", str(out_exe)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0 or not out_exe.exists():
            raise RuntimeError(f"Clang build failed for {workload} ({config}):\nSTDOUT: {res.stdout}\nSTDERR: {res.stderr}")
        return out_exe

    # MSVC C Expert configuration (/O2 /arch:AVX2 with expert implementation)
    elif config == "c_msvc_expert":
        src = BENCHMARKS_DIR / "c" / f"{workload}_expert.c"
        if not src.exists():
            src = BENCHMARKS_DIR / "c" / f"{workload}.c"
        if not toolchain.msvc_env:
            raise RuntimeError("MSVC vcvars64.bat environment not found or failed to load")
        obj_file = BUILD_OBJ_DIR / f"{workload}_msvc_expert.obj"
        if obj_file.exists():
            obj_file.unlink()
        asm_file = ASSEMBLY_DIR / f"{workload}_c_msvc_expert.asm"
        cmd = ["cl.exe", "/nologo", "/O2", "/arch:AVX2", "/W3", f"/Fa{asm_file}", str(src), f"/Fe:{out_exe}", f"/Fo:{obj_file}"]
        res = subprocess.run(cmd, capture_output=True, text=True, env=toolchain.msvc_env)
        if res.returncode != 0 or not out_exe.exists():
            raise RuntimeError(f"MSVC Expert build failed for {workload}:\nSTDOUT: {res.stdout}\nSTDERR: {res.stderr}")
        return out_exe

    # MSVC C Idiomatic configuration (/O2 /arch:AVX2)
    elif config == "c_msvc_o2":
        src = BENCHMARKS_DIR / "c" / f"{workload}.c"
        if not src.exists():
            raise FileNotFoundError(f"Missing C source: {src}")
        if not toolchain.msvc_env:
            raise RuntimeError("MSVC vcvars64.bat environment not found or failed to load")
        obj_file = BUILD_OBJ_DIR / f"{workload}_msvc.obj"
        if obj_file.exists():
            obj_file.unlink()
        cmd = ["cl.exe", "/nologo", "/O2", "/arch:AVX2", "/W3", str(src), f"/Fe:{out_exe}", f"/Fo:{obj_file}"]
        res = subprocess.run(cmd, capture_output=True, text=True, env=toolchain.msvc_env)
        if res.returncode != 0 or not out_exe.exists():
            raise RuntimeError(f"MSVC build failed for {workload} ({config}):\nSTDOUT: {res.stdout}\nSTDERR: {res.stderr}")
        return out_exe

    # Rust Expert configuration (release-lto + target-cpu=native with expert binary)
    elif config == "rust_expert":
        cargo_toml = BENCHMARKS_DIR / "rust" / "Cargo.toml"
        target_dir = BENCHMARKS_DIR / "rust" / "target" / "release-lto"
        bin_name = f"{workload}_expert"
        rust_env = os.environ.copy()
        rust_env["RUSTFLAGS"] = "-C target-cpu=native"
        cmd = [toolchain.cargo_exe, "build", "--profile", "release-lto", "--manifest-path", str(cargo_toml), "--bin", bin_name]
        res = subprocess.run(cmd, capture_output=True, text=True, env=rust_env)
        built_exe = target_dir / f"{bin_name}.exe"
        if res.returncode != 0 or not built_exe.exists():
            raise RuntimeError(f"Rust Expert build failed for {workload}:\nSTDOUT: {res.stdout}\nSTDERR: {res.stderr}")
        shutil.copy2(built_exe, out_exe)
        asm_file = ASSEMBLY_DIR / f"{workload}_rust_expert.s"
        cmd_asm = [toolchain.cargo_exe, "rustc", "--profile", "release-lto", "--manifest-path", str(cargo_toml), "--bin", bin_name, "--", f"--emit=asm={asm_file}"]
        subprocess.run(cmd_asm, capture_output=True, text=True, env=rust_env)
        return out_exe

    # Rust baseline (opt-level 3, non-LTO, target-cpu=native)
    elif config == "rust":
        cargo_toml = BENCHMARKS_DIR / "rust" / "Cargo.toml"
        target_dir = BENCHMARKS_DIR / "rust" / "target" / "release"
        rust_env = os.environ.copy()
        rust_env["RUSTFLAGS"] = "-C target-cpu=native"
        cmd = [toolchain.cargo_exe, "build", "--release", "--manifest-path", str(cargo_toml), "--bin", workload]
        res = subprocess.run(cmd, capture_output=True, text=True, env=rust_env)
        built_exe = target_dir / f"{workload}.exe"
        if res.returncode != 0 or not built_exe.exists():
            raise RuntimeError(f"Rust build failed for {workload}:\nSTDOUT: {res.stdout}\nSTDERR: {res.stderr}")
        shutil.copy2(built_exe, out_exe)
        return out_exe

    # Rust with LTO (profile: release-lto, target-cpu=native)
    elif config == "rust_lto":
        cargo_toml = BENCHMARKS_DIR / "rust" / "Cargo.toml"
        target_dir = BENCHMARKS_DIR / "rust" / "target" / "release-lto"
        rust_env = os.environ.copy()
        rust_env["RUSTFLAGS"] = "-C target-cpu=native"
        cmd = [toolchain.cargo_exe, "build", "--profile", "release-lto", "--manifest-path", str(cargo_toml), "--bin", workload]
        res = subprocess.run(cmd, capture_output=True, text=True, env=rust_env)
        built_exe = target_dir / f"{workload}.exe"
        if res.returncode != 0 or not built_exe.exists():
            raise RuntimeError(f"Rust LTO build failed for {workload}:\nSTDOUT: {res.stdout}\nSTDERR: {res.stderr}")
        shutil.copy2(built_exe, out_exe)
        return out_exe

    else:
        raise ValueError(f"Unknown configuration: {config}")


def build_all_binaries(toolchain: ToolchainDiscovery, workloads, configs):
    """Builds all required binaries with strict validation."""
    print("=" * 70)
    print("  TAMARIN v0.5.9 Benchmark Suite — Binary Build Phase")
    print("=" * 70)
    print("Toolchains Probed:")
    for k, v in toolchain.versions.items():
        print(f"  {k:10s}: {v}")
    print()

    binaries = {}
    total_to_build = len(workloads) * len(configs)
    current = 0

    for w in workloads:
        binaries[w] = {}
        for cfg in configs:
            current += 1
            print(f"[{current:02d}/{total_to_build:02d}] Building {w:20s} [{cfg}]... ", end="", flush=True)
            try:
                exe = compile_binary(toolchain, w, cfg)
                size_kb = exe.stat().st_size / 1024.0
                binaries[w][cfg] = {
                    "path": str(exe),
                    "size_bytes": exe.stat().st_size,
                    "size_kb": round(size_kb, 1),
                }
                print(f"OK ({size_kb:0.1f} KB)")
            except Exception as e:
                print(f"FAILED!\nError: {e}")
                sys.exit(1)

    print("\nAll benchmark binaries built successfully and verified on disk.\n")
    return binaries


def parse_and_validate_repetition(res_stdout: str, returncode: int, external_ms: float, workload: str, mode: str, peak_ws_kb: float = 0.0):
    """Strictly validates execution return code, delimiter structure, numeric timing,
    and field-by-field reference values. Raises BenchmarkValidationError on any failure."""
    if returncode != 0:
        raise BenchmarkValidationError(f"Process exited with non-zero code {returncode}")

    if not math.isfinite(external_ms) or external_ms < 0.0:
        raise BenchmarkValidationError(f"Invalid external timing: {external_ms}")

    if mode == "B":
        return {
            "time_ms": external_ms,
            "external_ms": external_ms,
            "peak_ws_kb": peak_ws_kb,
            "fields": {},
            "checksum": "OK",
            "stdout": res_stdout.strip(),
        }

    # Mode A: Parse formatted key-value line
    raw = res_stdout.strip()
    if not raw:
        raise BenchmarkValidationError("Process emitted empty standard output")

    lines = raw.splitlines()
    target_line = None
    for line in lines:
        if line.startswith("TIME_MS:"):
            target_line = line.strip()
            break

    if target_line is None:
        raise BenchmarkValidationError(f"Missing 'TIME_MS:' formatted line in stdout:\n{res_stdout}")

    tokens = target_line.split(";")
    parsed_fields = {}
    time_ms = None

    for token in tokens:
        if not token:
            continue
        if ":" not in token:
            raise BenchmarkValidationError(f"Malformed key-value token (missing ':'): '{token}'")
        parts = token.split(":", 1)
        k, v = parts[0].strip(), parts[1].strip()

        if k == "TIME_MS":
            if time_ms is not None:
                raise BenchmarkValidationError("Duplicate 'TIME_MS' key in output")
            try:
                time_ms = float(v)
            except ValueError:
                raise BenchmarkValidationError(f"Invalid non-float TIME_MS: '{v}'")
            if not math.isfinite(time_ms) or time_ms < 0.0:
                raise BenchmarkValidationError(f"Non-finite or negative TIME_MS: {time_ms}")
        else:
            if k in parsed_fields:
                raise BenchmarkValidationError(f"Duplicate field key '{k}' in output")
            parsed_fields[k] = v

    if time_ms is None:
        raise BenchmarkValidationError("Missing 'TIME_MS' token in output line")

    # Schema verification
    schema_info = EXPECTED_SCHEMAS.get(workload, {"fields": [], "fixtures": {}})
    expected_fields = schema_info["fields"]
    expected_fixtures = schema_info["fixtures"]

    # 1. Check for unknown fields
    for k in parsed_fields:
        if k not in expected_fields:
            raise BenchmarkValidationError(f"Unexpected unknown field '{k}' in output for workload {workload}")

    # 2. Check for missing required fields
    for k in expected_fields:
        if k not in parsed_fields:
            raise BenchmarkValidationError(f"Missing required field '{k}' in output for workload {workload}")

    # 3. Exact field fixture matching
    for k, expected_val in expected_fixtures.items():
        actual_val = parsed_fields.get(k)
        if actual_val != expected_val:
            raise BenchmarkValidationError(
                f"Workload '{workload}' field '{k}' value mismatch: actual '{actual_val}' != expected '{expected_val}'"
            )

    return {
        "time_ms": time_ms,
        "external_ms": external_ms,
        "peak_ws_kb": peak_ws_kb,
        "fields": parsed_fields,
        "checksum": parsed_fields.get("CHECKSUM"),
        "stdout": target_line,
    }


def execute_repetition(exe_path: str, workload: str, mode: str, env: dict = None):
    """Executes a single benchmark run and strictly enforces the validation contract."""
    run_env = os.environ.copy()
    if env:
        run_env.update(env)
    t0 = time.perf_counter_ns()
    proc = subprocess.Popen([exe_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=run_env)
    stdout, stderr = proc.communicate()
    t1 = time.perf_counter_ns()
    external_ms = (t1 - t0) / 1_000_000.0
    peak_ws_kb = get_process_peak_ws_kb(proc)

    parsed = parse_and_validate_repetition(stdout, proc.returncode, external_ms, workload, mode, peak_ws_kb=peak_ws_kb)
    return parsed


def compute_bootstrap_ci_median(samples, num_iterations=1000, ci_level=0.95):
    """Computes bootstrap confidence interval for the sample median with fixed seed 42."""
    if len(samples) < 3:
        m = statistics.median(samples)
        return m, m
    rng = random.Random(42)
    boot_medians = []
    n = len(samples)
    for _ in range(num_iterations):
        resample = [rng.choice(samples) for _ in range(n)]
        boot_medians.append(statistics.median(resample))
    boot_medians.sort()
    lower_idx = int(num_iterations * ((1.0 - ci_level) / 2.0))
    upper_idx = int(num_iterations * (1.0 - (1.0 - ci_level) / 2.0))
    lower_idx = max(0, min(lower_idx, num_iterations - 1))
    upper_idx = max(0, min(upper_idx, num_iterations - 1))
    return round(boot_medians[lower_idx], 4), round(boot_medians[upper_idx], 4)


def compute_bootstrap_ci_ratio(numerator_samples, denominator_samples, num_iterations=1000, ci_level=0.95):
    """Computes bootstrap confidence interval for ratio of medians (num / denom)."""
    if len(numerator_samples) < 3 or len(denominator_samples) < 3:
        n_med = statistics.median(numerator_samples)
        d_med = statistics.median(denominator_samples)
        ratio = n_med / d_med if d_med > 0 else 0.0
        return ratio, ratio
    rng = random.Random(42)
    ratios = []
    n_num = len(numerator_samples)
    n_den = len(denominator_samples)
    for _ in range(num_iterations):
        resample_num = [rng.choice(numerator_samples) for _ in range(n_num)]
        resample_den = [rng.choice(denominator_samples) for _ in range(n_den)]
        den_med = statistics.median(resample_den)
        if den_med > 0:
            ratios.append(statistics.median(resample_num) / den_med)
    if not ratios:
        return 0.0, 0.0
    ratios.sort()
    lower_idx = int(len(ratios) * ((1.0 - ci_level) / 2.0))
    upper_idx = int(len(ratios) * (1.0 - (1.0 - ci_level) / 2.0))
    lower_idx = max(0, min(lower_idx, len(ratios) - 1))
    upper_idx = max(0, min(upper_idx, len(ratios) - 1))
    return round(ratios[lower_idx], 2), round(ratios[upper_idx], 2)


def compute_statistics(samples):
    """Computes median, mean, stddev, min, max, IQR, and bootstrap 95% CI."""
    if not samples:
        return {}
    samples_sorted = sorted(samples)
    n = len(samples_sorted)
    med = statistics.median(samples_sorted)
    mean_val = statistics.mean(samples_sorted)
    std_val = statistics.stdev(samples_sorted) if n > 1 else 0.0
    min_val = min(samples_sorted)
    max_val = max(samples_sorted)

    q1 = samples_sorted[n // 4]
    q3 = samples_sorted[(3 * n) // 4]
    iqr = q3 - q1

    ci_low, ci_high = compute_bootstrap_ci_median(samples)

    return {
        "count": n,
        "median_ms": round(med, 4),
        "mean_ms": round(mean_val, 4),
        "stddev_ms": round(std_val, 4),
        "min_ms": round(min_val, 4),
        "max_ms": round(max_val, 4),
        "iqr_ms": round(iqr, 4),
        "ci_95_low_ms": ci_low,
        "ci_95_high_ms": ci_high,
    }


def run_benchmarks(binaries, workloads, configs, reps_compute=30, warmup_compute=5,
                   reps_startup=100, warmup_startup=10, rcache_policy=1, rcache_memcap=131072):
    """Executes benchmarks with warmup validation, round-robin interleaved scheduling,
    and immediate hard termination on any contract violation."""
    print("=" * 70)
    print("  TAMARIN v0.5.9 Benchmark Suite — Execution Phase")
    print("=" * 70)
    print(f"Mode A (Compute): {reps_compute} repetitions, {warmup_compute} warmups (Internal QPC)")
    print(f"Mode B (Startup): {reps_startup} repetitions, {warmup_startup} warmups (External Launch)")
    print(f"Reservation Cache: Policy {rcache_policy}, Memory Cap {rcache_memcap} bytes")
    print("Execution Order : Interleaved round-robin across configurations")
    print("Strict Validation: Warmup and measured runs enforced on every repetition")
    print("=" * 70)

    cfg_envs = {}
    for cfg in configs:
        env = {}
        if cfg == "tamarin_o2_policy0":
            env["TAMARIN_RCACHE_POLICY"] = "0"
        elif cfg.startswith("tamarin"):
            env["TAMARIN_RCACHE_POLICY"] = str(rcache_policy)
        if cfg.startswith("tamarin") and rcache_memcap:
            env["TAMARIN_RCACHE_MEMCAP"] = str(rcache_memcap)
        cfg_envs[cfg] = env

    results = {
        "compute": {},
        "startup": {},
        "metadata": {
            "rcache_policy": rcache_policy,
            "rcache_memcap": rcache_memcap,
        },
    }

    # ── 1. Execute Mode A (Compute Workloads) ───────────────────────────────────
    for w in [w for w in workloads if w in COMPUTE_WORKLOADS]:
        print(f"\n[Mode A] Workload: {w}")
        results["compute"][w] = {}

        # 1a. Warmup phase with full contract enforcement
        for cfg in configs:
            exe = binaries[w][cfg]["path"]
            env = cfg_envs[cfg]
            for warmup_idx in range(warmup_compute):
                try:
                    execute_repetition(exe, workload=w, mode="A", env=env)
                except Exception as e:
                    print(f"\nFATAL: Warmup {warmup_idx+1}/{warmup_compute} failed for {w} [{cfg}]: {e}")
                    sys.exit(1)

        # 1b. Measured repetitions (Interleaved round-robin)
        samples = {cfg: [] for cfg in configs}
        ws_samples = {cfg: [] for cfg in configs}
        checksums = {cfg: [] for cfg in configs}
        repetition_records = {cfg: [] for cfg in configs}

        for rep in range(reps_compute):
            for cfg in configs:
                exe = binaries[w][cfg]["path"]
                env = cfg_envs[cfg]
                try:
                    res = execute_repetition(exe, workload=w, mode="A", env=env)
                except Exception as e:
                    print(f"\nFATAL: Repetition {rep+1}/{reps_compute} failed for {w} [{cfg}]: {e}")
                    sys.exit(1)

                samples[cfg].append(res["time_ms"])
                ws_samples[cfg].append(res.get("peak_ws_kb", 0.0))
                checksums[cfg].append(res["checksum"])
                repetition_records[cfg].append({
                    "rep": rep,
                    "time_ms": res["time_ms"],
                    "peak_ws_kb": res.get("peak_ws_kb", 0.0),
                    "fields": res["fields"],
                    "checksum": res["checksum"],
                })

        # 1c. Aggregate statistics and validation metadata
        for cfg in configs:
            cfg_samples = samples[cfg]
            stats = compute_statistics(cfg_samples)
            ws_cfg_samples = [x for x in ws_samples[cfg] if x > 0]
            stats["peak_ws_kb_median"] = round(statistics.median(ws_cfg_samples), 1) if ws_cfg_samples else 0.0
            stats["peak_ws_kb_max"] = round(max(ws_cfg_samples), 1) if ws_cfg_samples else 0.0
            stats["checksum"] = checksums[cfg][0]
            stats["checksum_verified"] = True
            stats["binary_size_bytes"] = binaries[w][cfg]["size_bytes"]
            stats["binary_size_kb"] = binaries[w][cfg]["size_kb"]
            stats["raw_samples"] = [round(x, 4) for x in cfg_samples]
            stats["repetitions"] = repetition_records[cfg]
            stats["rcache_policy"] = 0 if cfg == "tamarin_o2_policy0" else (rcache_policy if cfg.startswith("tamarin") else None)
            stats["rcache_memcap"] = rcache_memcap if cfg.startswith("tamarin") else None

            results["compute"][w][cfg] = stats
            ws_info = f"  ws={stats['peak_ws_kb_median']} KB" if stats['peak_ws_kb_median'] > 0 else ""
            print(f"  {cfg:14s}: med={stats['median_ms']:8.4f} ms [95% CI: {stats['ci_95_low_ms']:0.4f}-{stats['ci_95_high_ms']:0.4f}]  mean={stats['mean_ms']:8.4f} ms  std={stats['stddev_ms']:7.4f} ms{ws_info}  [VERIFIED]")

    # ── 2. Execute Mode B (Startup Workload) ───────────────────────────────────
    if STARTUP_WORKLOAD in workloads:
        w = STARTUP_WORKLOAD
        print(f"\n[Mode B] Process Startup Overhead: {w}")
        results["startup"][w] = {}

        # 2a. Warmup phase
        for cfg in configs:
            exe = binaries[w][cfg]["path"]
            for warmup_idx in range(warmup_startup):
                try:
                    execute_repetition(exe, workload=w, mode="B")
                except Exception as e:
                    print(f"\nFATAL: Startup warmup {warmup_idx+1}/{warmup_startup} failed for {w} [{cfg}]: {e}")
                    sys.exit(1)

        # 2b. Measured repetitions (Interleaved round-robin)
        samples = {cfg: [] for cfg in configs}
        ws_samples = {cfg: [] for cfg in configs}
        repetition_records = {cfg: [] for cfg in configs}

        for rep in range(reps_startup):
            for cfg in configs:
                exe = binaries[w][cfg]["path"]
                try:
                    res = execute_repetition(exe, workload=w, mode="B")
                except Exception as e:
                    print(f"\nFATAL: Startup repetition {rep+1}/{reps_startup} failed for {w} [{cfg}]: {e}")
                    sys.exit(1)

                samples[cfg].append(res["time_ms"])
                ws_samples[cfg].append(res.get("peak_ws_kb", 0.0))
                repetition_records[cfg].append({
                    "rep": rep,
                    "external_ms": res["time_ms"],
                    "peak_ws_kb": res.get("peak_ws_kb", 0.0),
                })

        for cfg in configs:
            cfg_samples = samples[cfg]
            stats = compute_statistics(cfg_samples)
            ws_cfg_samples = [x for x in ws_samples[cfg] if x > 0]
            stats["peak_ws_kb_median"] = round(statistics.median(ws_cfg_samples), 1) if ws_cfg_samples else 0.0
            stats["peak_ws_kb_max"] = round(max(ws_cfg_samples), 1) if ws_cfg_samples else 0.0
            stats["binary_size_bytes"] = binaries[w][cfg]["size_bytes"]
            stats["binary_size_kb"] = binaries[w][cfg]["size_kb"]
            stats["raw_samples"] = [round(x, 4) for x in cfg_samples]
            stats["repetitions"] = repetition_records[cfg]

            results["startup"][w][cfg] = stats
            ws_info = f"  ws={stats['peak_ws_kb_median']} KB" if stats['peak_ws_kb_median'] > 0 else ""
            print(f"  {cfg:14s}: med={stats['median_ms']:7.3f} ms [95% CI: {stats['ci_95_low_ms']:0.3f}-{stats['ci_95_high_ms']:0.3f}]  mean={stats['mean_ms']:7.3f} ms  std={stats['stddev_ms']:6.3f} ms{ws_info}  (size: {stats['binary_size_kb']} KB)")

    return results


def generate_reports(results, toolchain: ToolchainDiscovery, configs, is_partial=False):
    """Outputs machine-readable JSON with full metadata and a Markdown summary report."""
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    json_path = RESULTS_DIR / "benchmark_data.json"
    md_path = RESULTS_DIR / "benchmark_summary.md"

    full_data = {
        "tamarin_version": "0.5.9",
        "benchmark_version": "v0.5.9",
        "is_partial_campaign": is_partial,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime()),
        "environment": {
            "os": platform.platform(),
            "cpu": platform.processor(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "cpu_count": os.cpu_count(),
        },
        "toolchains": toolchain.versions,
        "configurations": {
            "tamarin_o0": "Tamarin compiler -O0 (baseline unoptimized)",
            "tamarin_o1": "Tamarin compiler -O1 (basic optimization)",
            "tamarin_o2": "Tamarin compiler -O2 (standard optimization with bounds-check canonicalization)",
            "tamarin_o3": "Tamarin compiler -O3 (aggressive optimization with native target-cpu autovectorization)",
            "tamarin_expert": "Tamarin Expert -O3 --target-cpu native (always_inline, unroll, bitops, fast memory and sidecar cache)",
            "c_clang_o2": "Clang C Idiomatic -O2 -march=native (x86_64-pc-windows-msvc)",
            "c_clang_o3": "Clang C Idiomatic -O3 -march=native (x86_64-pc-windows-msvc)",
            "c_msvc_o2": "Microsoft Visual C++ 2022 Idiomatic /O2 /arch:AVX2 /W3",
            "rust": "Rust rustc 2021 release profile (opt-level=3, non-LTO, target-cpu=native)",
            "rust_lto": "Rust rustc 2021 release-lto profile (opt-level=3, LTO=fat, target-cpu=native)",
            "c_clang_expert": "Clang C Expert -O3 -march=native (SIMD intrinsics, unrolling, reusable arena)",
            "c_msvc_expert": "MSVC C Expert /O2 /arch:AVX2 (SIMD intrinsics, unrolling, reusable arena)",
            "rust_expert": "Rust Expert release-lto target-cpu=native (AVX2, unrolling, reusable arena)",
        },
        "results": results,
    }

    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(full_data, f, indent=2)
    print(f"\nMachine-readable benchmark data saved to: {json_path}")

    # Generate Markdown Summary
    with open(md_path, "w", encoding="utf-8") as f:
        f.write("# TAMARIN v0.5.9 — Expert Performance Optimization\n\n")
        f.write(f"**Date:** {time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime())}  \n")
        f.write(f"**Host OS:** {platform.platform()}  \n")
        f.write(f"**Processor:** {platform.processor()} (Logical Cores: {os.cpu_count()})  \n")
        f.write("**Methodology:** Mode A pure computational kernels timed via internal high-resolution QPC (setup/IO excluded); Mode B startup timed via external parent wall clock; 30 repetitions, 5 warmups; interleaved round-robin scheduling; fail-fast checksum validation; resident working set measured via Windows PSAPI `GetProcessMemoryInfo`.\n\n")

        if is_partial:
            f.write("> [!WARNING]\n> **PARTIAL BENCHMARK CAMPAIGN:** Only a subset of configurations or workloads was evaluated. This run does not constitute a full formal comparison.\n\n")

        f.write("## 1. Toolchains & Compilers Tested\n\n")
        f.write("| Toolchain | Version / Build | Target CPU / Architecture Flags |\n")
        f.write("| :--- | :--- | :--- |\n")
        for k, v in toolchain.versions.items():
            flags = "-march=native" if k in ["clang", "tamarin"] else ("/arch:AVX2" if k == "msvc" else "-C target-cpu=native")
            f.write(f"| **{k.upper()}** | `{v}` | `{flags}` |\n")
        f.write("\n")

        # ── Mode A Table ───────────────────────────────────────────────────────
        f.write("## 2. Mode A — Pure Computational Kernel Throughput (Median ms)\n\n")
        f.write("| Workload | T-O2 (ms) | T-O3 (ms) | T-Exp (ms) | Clang-O3 (ms) | MSVC-O2 (ms) | Rust-LTO (ms) | Clang-Exp (ms) | MSVC-Exp (ms) | Rust-Exp (ms) | Verification |\n")
        f.write("| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |\n")

        compute_res = results.get("compute", {})
        for w, cfg_data in compute_res.items():
            t_o2 = cfg_data.get("tamarin_o2", {}).get("median_ms", "N/A")
            t_o3 = cfg_data.get("tamarin_o3", {}).get("median_ms", "N/A")
            t_exp = cfg_data.get("tamarin_expert", {}).get("median_ms", "N/A")
            c_o3 = cfg_data.get("c_clang_o3", {}).get("median_ms", "N/A")
            c_ms = cfg_data.get("c_msvc_o2", {}).get("median_ms", "N/A")
            r_lto = cfg_data.get("rust_lto", {}).get("median_ms") or cfg_data.get("rust", {}).get("median_ms", "N/A")
            c_exp = cfg_data.get("c_clang_expert", {}).get("median_ms", "N/A")
            m_exp = cfg_data.get("c_msvc_expert", {}).get("median_ms", "N/A")
            r_exp = cfg_data.get("rust_expert", {}).get("median_ms", "N/A")

            f.write(f"| `{w}` | {t_o2} | {t_o3} | {t_exp} | {c_o3} | {c_ms} | {r_lto} | {c_exp} | {m_exp} | {r_exp} | Exact Match |\n")

        f.write("\n")

        # ── Mode A Ratios & Geometric Means ────────────────────────────────────
        f.write("## 2.1. Relative Performance Ratios (TAMARIN -O3 Relative to Competitors)\n\n")
        f.write("> **Interpretation:** Values `< 1.00x` indicate TAMARIN is faster than the competitor. Values `> 1.00x` indicate the competitor is faster. Ratios include 95% bootstrap confidence intervals.\n\n")
        f.write("| Workload | vs Clang Idiom (-O3) | vs MSVC Idiom (/O2) | vs Rust Idiom (LTO) | vs Clang Expert | vs MSVC Expert | vs Rust Expert |\n")
        f.write("| :--- | :---: | :---: | :---: | :---: | :---: | :---: |\n")

        ratios_c_idio = []
        ratios_m_idio = []
        ratios_r_idio = []
        ratios_c_exp = []
        ratios_m_exp = []
        ratios_r_exp = []

        for w, cfg_data in compute_res.items():
            t_o3_raw = cfg_data.get("tamarin_o3", {}).get("raw_samples", [])
            c_o3_raw = cfg_data.get("c_clang_o3", {}).get("raw_samples", [])
            m_o2_raw = cfg_data.get("c_msvc_o2", {}).get("raw_samples", [])
            r_lto_raw = cfg_data.get("rust_lto", {}).get("raw_samples") or cfg_data.get("rust", {}).get("raw_samples", [])
            c_exp_raw = cfg_data.get("c_clang_expert", {}).get("raw_samples", [])
            m_exp_raw = cfg_data.get("c_msvc_expert", {}).get("raw_samples", [])
            r_exp_raw = cfg_data.get("rust_expert", {}).get("raw_samples", [])

            t_o3_med = cfg_data.get("tamarin_o3", {}).get("median_ms")

            def format_ratio_cell(t_raw, comp_raw, comp_med, ratio_list):
                if t_raw and comp_raw and comp_med and comp_med > 0 and t_o3_med:
                    ratio = t_o3_med / comp_med
                    ratio_list.append(ratio)
                    ci_low, ci_high = compute_bootstrap_ci_ratio(t_raw, comp_raw)
                    return f"**{ratio:0.2f}x** [{ci_low:0.2f}-{ci_high:0.2f}]"
                return "N/A"

            c_idio_str = format_ratio_cell(t_o3_raw, c_o3_raw, cfg_data.get("c_clang_o3", {}).get("median_ms"), ratios_c_idio)
            m_idio_str = format_ratio_cell(t_o3_raw, m_o2_raw, cfg_data.get("c_msvc_o2", {}).get("median_ms"), ratios_m_idio)
            r_idio_str = format_ratio_cell(t_o3_raw, r_lto_raw, cfg_data.get("rust_lto", {}).get("median_ms") or cfg_data.get("rust", {}).get("median_ms"), ratios_r_idio)
            c_exp_str = format_ratio_cell(t_o3_raw, c_exp_raw, cfg_data.get("c_clang_expert", {}).get("median_ms"), ratios_c_exp)
            m_exp_str = format_ratio_cell(t_o3_raw, m_exp_raw, cfg_data.get("c_msvc_expert", {}).get("median_ms"), ratios_m_exp)
            r_exp_str = format_ratio_cell(t_o3_raw, r_exp_raw, cfg_data.get("rust_expert", {}).get("median_ms"), ratios_r_exp)

            f.write(f"| `{w}` | {c_idio_str} | {m_idio_str} | {r_idio_str} | {c_exp_str} | {m_exp_str} | {r_exp_str} |\n")

        # Geometric Mean Summary Row
        gm_c_idio = compute_geomean(ratios_c_idio)
        gm_m_idio = compute_geomean(ratios_m_idio)
        gm_r_idio = compute_geomean(ratios_r_idio)
        gm_c_exp = compute_geomean(ratios_c_exp)
        gm_m_exp = compute_geomean(ratios_m_exp)
        gm_r_exp = compute_geomean(ratios_r_exp)

        f.write(f"| **GEOMETRIC MEAN** | **{gm_c_idio:0.2f}x** | **{gm_m_idio:0.2f}x** | **{gm_r_idio:0.2f}x** | **{gm_c_exp:0.2f}x** | **{gm_m_exp:0.2f}x** | **{gm_r_exp:0.2f}x** |\n\n")

        # ── Mode A Expert TAMARIN Ratios ───────────────────────────────────────
        f.write("## 2.2. Relative Performance Ratios (Expert TAMARIN Relative to Competitors)\n\n")
        f.write("> **Interpretation:** Values `< 1.00x` indicate Expert TAMARIN is faster than the competitor. Values `> 1.00x` indicate the competitor is faster. Ratios include 95% bootstrap confidence intervals.\n\n")
        f.write("| Workload | vs Clang Expert | vs MSVC Expert | vs Rust Expert | vs Normal TAMARIN (-O3) |\n")
        f.write("| :--- | :---: | :---: | :---: | :---: |\n")

        exp_ratios_c_exp = []
        exp_ratios_m_exp = []
        exp_ratios_r_exp = []
        exp_ratios_t_norm = []

        for w, cfg_data in compute_res.items():
            t_exp_raw = cfg_data.get("tamarin_expert", {}).get("raw_samples", [])
            t_exp_med = cfg_data.get("tamarin_expert", {}).get("median_ms")
            t_o3_raw = cfg_data.get("tamarin_o3", {}).get("raw_samples", [])
            c_exp_raw = cfg_data.get("c_clang_expert", {}).get("raw_samples", [])
            m_exp_raw = cfg_data.get("c_msvc_expert", {}).get("raw_samples", [])
            r_exp_raw = cfg_data.get("rust_expert", {}).get("raw_samples", [])

            def format_exp_ratio_cell(comp_raw, comp_med, ratio_list):
                if t_exp_raw and comp_raw and comp_med and comp_med > 0 and t_exp_med:
                    ratio = t_exp_med / comp_med
                    ratio_list.append(ratio)
                    ci_low, ci_high = compute_bootstrap_ci_ratio(t_exp_raw, comp_raw)
                    return f"**{ratio:0.2f}x** [{ci_low:0.2f}-{ci_high:0.2f}]"
                return "N/A"

            ec_exp_str = format_exp_ratio_cell(c_exp_raw, cfg_data.get("c_clang_expert", {}).get("median_ms"), exp_ratios_c_exp)
            em_exp_str = format_exp_ratio_cell(m_exp_raw, cfg_data.get("c_msvc_expert", {}).get("median_ms"), exp_ratios_m_exp)
            er_exp_str = format_exp_ratio_cell(r_exp_raw, cfg_data.get("rust_expert", {}).get("median_ms"), exp_ratios_r_exp)
            et_norm_str = format_exp_ratio_cell(t_o3_raw, cfg_data.get("tamarin_o3", {}).get("median_ms"), exp_ratios_t_norm)

            f.write(f"| `{w}` | {ec_exp_str} | {em_exp_str} | {er_exp_str} | {et_norm_str} |\n")

        gm_ec_exp = compute_geomean(exp_ratios_c_exp)
        gm_em_exp = compute_geomean(exp_ratios_m_exp)
        gm_er_exp = compute_geomean(exp_ratios_r_exp)
        gm_et_norm = compute_geomean(exp_ratios_t_norm)

        f.write(f"| **GEOMETRIC MEAN** | **{gm_ec_exp:0.2f}x** | **{gm_em_exp:0.2f}x** | **{gm_er_exp:0.2f}x** | **{gm_et_norm:0.2f}x** |\n\n")

        # ── Geometric Mean Summary Table ───────────────────────────────────────
        f.write("## 2.3. Geometric Mean Performance Summary\n\n")
        f.write("| Comparison Benchmark | Geometric Mean Ratio | Status |\n")
        f.write("| :--- | :---: | :--- |\n")
        f.write(f"| Normal TAMARIN -O3 vs **Clang Idiomatic (-O3)** | **{gm_c_idio:0.2f}x** | {'TAMARIN Leads' if gm_c_idio <= 1.0 else 'Competitor Leads'} |\n")
        f.write(f"| Normal TAMARIN -O3 vs **MSVC Idiomatic (/O2)** | **{gm_m_idio:0.2f}x** | {'TAMARIN Leads' if gm_m_idio <= 1.0 else 'Competitor Leads'} |\n")
        f.write(f"| Normal TAMARIN -O3 vs **Rust Idiomatic (Release-LTO)** | **{gm_r_idio:0.2f}x** | {'TAMARIN Leads' if gm_r_idio <= 1.0 else 'Competitor Leads'} |\n")
        f.write(f"| Normal TAMARIN -O3 vs **Clang Expert (-O3 Native SIMD)** | **{gm_c_exp:0.2f}x** | {'TAMARIN Leads' if gm_c_exp <= 1.0 else 'Competitor Leads'} |\n")
        f.write(f"| Normal TAMARIN -O3 vs **MSVC Expert (/O2 AVX2)** | **{gm_m_exp:0.2f}x** | {'TAMARIN Leads' if gm_m_exp <= 1.0 else 'Competitor Leads'} |\n")
        f.write(f"| Normal TAMARIN -O3 vs **Rust Expert (Release-LTO Native)** | **{gm_r_exp:0.2f}x** | {'TAMARIN Leads' if gm_r_exp <= 1.0 else 'Competitor Leads'} |\n")
        f.write(f"| **Expert TAMARIN** vs **Clang Expert (-O3 Native SIMD)** | **{gm_ec_exp:0.2f}x** | {'Expert TAMARIN Leads' if gm_ec_exp <= 1.0 else 'Competitor Leads'} |\n")
        f.write(f"| **Expert TAMARIN** vs **MSVC Expert (/O2 AVX2)** | **{gm_em_exp:0.2f}x** | {'Expert TAMARIN Leads' if gm_em_exp <= 1.0 else 'Competitor Leads'} |\n")
        f.write(f"| **Expert TAMARIN** vs **Rust Expert (Release-LTO Native)** | **{gm_er_exp:0.2f}x** | {'Expert TAMARIN Leads' if gm_er_exp <= 1.0 else 'Competitor Leads'} |\n")
        f.write(f"| **Expert TAMARIN** vs **Normal TAMARIN (-O3)** | **{gm_et_norm:0.2f}x** | {'Expert TAMARIN Leads' if gm_et_norm <= 1.0 else 'Normal TAMARIN Leads'} |\n\n")

        # ── Memory and Binary Size ─────────────────────────────────────────────
        f.write("## 2.4. Peak Working-Set Memory (KB) & Binary Size (KB)\n\n")
        f.write("| Workload | T-O3 WS (KB) | T-Exp WS (KB) | Clang-Exp WS (KB) | Rust-Exp WS (KB) | T-O3 Bin (KB) | T-Exp Bin (KB) | Clang-Exp Bin (KB) | Rust-Exp Bin (KB) |\n")
        f.write("| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |\n")

        for w, cfg_data in compute_res.items():
            t_ws = cfg_data.get("tamarin_o3", {}).get("peak_ws_kb_median", "N/A")
            te_ws = cfg_data.get("tamarin_expert", {}).get("peak_ws_kb_median", "N/A")
            c_ws = cfg_data.get("c_clang_expert", {}).get("peak_ws_kb_median", "N/A")
            r_ws = cfg_data.get("rust_expert", {}).get("peak_ws_kb_median", "N/A")
            t_bin = cfg_data.get("tamarin_o3", {}).get("binary_size_kb", "N/A")
            te_bin = cfg_data.get("tamarin_expert", {}).get("binary_size_kb", "N/A")
            c_bin = cfg_data.get("c_clang_expert", {}).get("binary_size_kb", "N/A")
            r_bin = cfg_data.get("rust_expert", {}).get("binary_size_kb", "N/A")
            f.write(f"| `{w}` | {t_ws} | {te_ws} | {c_ws} | {r_ws} | {t_bin} | {te_bin} | {c_bin} | {r_bin} |\n")

        f.write("\n")

        # ── Mode B ─────────────────────────────────────────────────────────────
        f.write("## 3. Mode B — Process Startup and Runtime Initialization Overhead\n\n")
        f.write("> **Methodology:** External parent-process wall clock timing (`time.perf_counter_ns()`) measuring launch, Windows CRT/runtime initialization, main function entry, and termination of a minimal no-op executable. 100 repetitions, 10 warmups, interleaved scheduling.\n\n")

        f.write("| Configuration | Median (ms) | 95% CI (ms) | Mean (ms) | StdDev (ms) | Peak WS (KB) | Binary Size (KB) |\n")
        f.write("| :--- | :---: | :---: | :---: | :---: | :---: | :---: |\n")

        startup_res = results.get("startup", {}).get(STARTUP_WORKLOAD, {})
        for cfg in configs:
            if cfg in startup_res:
                s = startup_res[cfg]
                ci_str = f"[{s['ci_95_low_ms']:0.3f}-{s['ci_95_high_ms']:0.3f}]"
                ws = s.get("peak_ws_kb_median", "N/A")
                f.write(f"| `{cfg}` | {s['median_ms']} | {ci_str} | {s['mean_ms']} | {s['stddev_ms']} | {ws} | {s['binary_size_kb']} KB |\n")

        f.write("\n")

        # ── AoS vs SoA Comparison ──────────────────────────────────────────────
        f.write("## 4. Architectural Analysis: AoS vs SoA SIMD Vectorization\n\n")
        f.write("Comparison of Array of Structures (`@layout(point)`) versus Structure of Arrays (`@layout(scan)`) across 5,000 particles and 200 simulation steps:\n\n")
        f.write("| Language / Toolchain | AoS Median (ms) | SoA Median (ms) | SoA Speedup Factor |\n")
        f.write("| :--- | :---: | :---: | :---: |\n")

        aos_data = compute_res.get("04_structures_aos", {})
        soa_data = compute_res.get("04_structures_soa", {})
        for cfg in ["tamarin_o3", "tamarin_expert", "c_clang_o3", "c_clang_expert", "c_msvc_o2", "c_msvc_expert", "rust_lto", "rust_expert"]:
            aos_t = aos_data.get(cfg, {}).get("median_ms")
            soa_t = soa_data.get(cfg, {}).get("median_ms")
            if aos_t and soa_t and soa_t > 0:
                speedup = f"{aos_t / soa_t:0.2f}x"
                f.write(f"| `{cfg}` | {aos_t} ms | {soa_t} ms | **{speedup}** |\n")

        f.write("\n")

        # ── Tamarin Optimization Scaling ───────────────────────────────────────
        f.write("## 5. Tamarin Optimization Level Progression\n\n")
        f.write("| Workload | -O0 (Unoptimized) | -O1 (Basic) | -O2 (Standard) | -O3 (Aggressive) | Expert | Expert vs -O3 Speedup | -O3 vs -O0 Speedup |\n")
        f.write("| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |\n")
        for w, cfg_data in compute_res.items():
            o0 = cfg_data.get("tamarin_o0", {}).get("median_ms")
            o1 = cfg_data.get("tamarin_o1", {}).get("median_ms")
            o2 = cfg_data.get("tamarin_o2", {}).get("median_ms")
            o3 = cfg_data.get("tamarin_o3", {}).get("median_ms")
            exp = cfg_data.get("tamarin_expert", {}).get("median_ms")
            sp_exp = f"{o3 / exp:0.2f}x" if (o3 and exp and exp > 0) else "N/A"
            sp_o3 = f"{o0 / o3:0.2f}x" if (o0 and o3 and o3 > 0) else "N/A"
            f.write(f"| `{w}` | {o0 or 'N/A'} ms | {o1 or 'N/A'} ms | {o2 or 'N/A'} ms | {o3 or 'N/A'} ms | {exp or 'N/A'} ms | **{sp_exp}** | **{sp_o3}** |\n")

        f.write("\n")

    print(f"Summary Markdown report written to: {md_path}\n")


def main():
    parser = argparse.ArgumentParser(description="TAMARIN v0.5.9 Benchmark Runner")
    parser.add_argument("--reps", type=int, default=30, help="Repetitions for Mode A compute workloads (default: 30)")
    parser.add_argument("--warmup", type=int, default=5, help="Warmup runs for Mode A (default: 5)")
    parser.add_argument("--startup-reps", type=int, default=100, help="Repetitions for Mode B startup workload (default: 100)")
    parser.add_argument("--startup-warmup", type=int, default=10, help="Warmup runs for Mode B (default: 10)")
    parser.add_argument("--workloads", type=str, default="", help="Comma-separated workload names to run (default: all)")
    parser.add_argument("--configs", type=str, default="", help="Comma-separated configuration names (default: all)")
    parser.add_argument("--skip-build", action="store_true", help="Skip binary compilation phase")
    parser.add_argument("--include-lto", action="store_true", help="Also build and test rust_lto")
    parser.add_argument("--tamarin-exe", type=str, default="", help="Explicit path to tamarin.exe")
    parser.add_argument("--clang-exe", type=str, default="", help="Explicit path to clang.exe")
    parser.add_argument("--vcvars-bat", type=str, default="", help="Explicit path to vcvars64.bat")
    parser.add_argument("--cargo-exe", type=str, default="", help="Explicit path to cargo executable")
    parser.add_argument("--rustc-exe", type=str, default="", help="Explicit path to rustc executable")
    parser.add_argument("--build-dir", type=str, default="", help="Explicit path to compiler build directory")
    parser.add_argument("--rcache-policy", type=int, default=1, choices=[0, 1], help="Reservation cache policy for Tamarin (default: 1 = fast small-region pool)")
    parser.add_argument("--rcache-memcap", type=int, default=131072, help="Aggregate committed-memory cap in bytes across thread-local pool (default: 131072 = 128 KB)")
    args = parser.parse_args()

    toolchain = ToolchainDiscovery(
        tamarin_exe=args.tamarin_exe,
        clang_exe=args.clang_exe,
        vcvars_bat=args.vcvars_bat,
        cargo_exe=args.cargo_exe,
        rustc_exe=args.rustc_exe,
        build_dir=args.build_dir,
    )

    if args.workloads:
        selected_workloads = [w.strip() for w in args.workloads.split(",") if w.strip()]
    else:
        selected_workloads = ALL_WORKLOADS

    default_configs = list(STANDARD_FULL_CONFIGS)
    if args.include_lto and "rust_lto" not in default_configs:
        default_configs.append("rust_lto")

    if args.configs:
        selected_configs = [c.strip() for c in args.configs.split(",") if c.strip()]
        is_partial = set(selected_configs) != set(default_configs) or set(selected_workloads) != set(ALL_WORKLOADS)
    else:
        selected_configs = default_configs
        is_partial = set(selected_workloads) != set(ALL_WORKLOADS)

    # 1. Build Phase
    if not args.skip_build:
        binaries = build_all_binaries(toolchain, selected_workloads, selected_configs)
    else:
        binaries = {}
        for w in selected_workloads:
            binaries[w] = {}
            for cfg in selected_configs:
                exe = BUILD_BIN_DIR / f"{w}_{cfg}.exe"
                if not exe.exists():
                    print(f"Error: Executable not found for --skip-build: {exe}")
                    sys.exit(1)
                binaries[w][cfg] = {
                    "path": str(exe),
                    "size_bytes": exe.stat().st_size,
                    "size_kb": round(exe.stat().st_size / 1024.0, 1),
                }

    # 2. Benchmark Execution Phase
    results = run_benchmarks(
        binaries,
        selected_workloads,
        selected_configs,
        reps_compute=args.reps,
        warmup_compute=args.warmup,
        reps_startup=args.startup_reps,
        warmup_startup=args.startup_warmup,
        rcache_policy=args.rcache_policy,
        rcache_memcap=args.rcache_memcap,
    )

    # 3. Report Generation Phase
    generate_reports(results, toolchain, selected_configs, is_partial=is_partial)


if __name__ == "__main__":
    main()
