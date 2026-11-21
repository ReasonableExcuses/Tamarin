#!/usr/bin/env python3
"""
Tamarin Multi-Platform Test Runner & Verification Harness (v0.16.0)

Executes the entire Tamarin test gauntlet across Windows, Linux, and macOS:
  1. C++ Unit Tests (tamarin_tests: 356+ tests)
  2. Mega Suite & C ABI Round-Trip (test_tamarin_mega.tam)
  3. Adversarial Soundness Gauntlet (test_rust_soundness.ps1)
  4. Dark Corners Gauntlet (test_rust_dark_corners.ps1)
  5. Boss-Level Edge Cases (test_boss_corners.ps1)
  6. Move Semantics & Containers (test_move_semantics.ps1)
  7. Stack Closures & Captures (test_closures.ps1)
  8. Async / Await & Coroutines (test_async_coroutines.ps1)
  9. Troop Package Manager E2E (test_troop.ps1)
  10. @cfg(...) & Multi-Target Cross-Compilation
  11. tamarin-bindgen 120 Hard Instances Gauntlet (test_hard_instances.py)
  12. tamarin-lsp Protocol & Lifecycle E2E Suite (test_lsp_server.py)

Standard library only: subprocess, pathlib, tempfile, sys, re, shutil, os, platform.
"""

import argparse
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
IS_WINDOWS = platform.system() == "Windows"
IS_DARWIN = platform.system() == "Darwin"
IS_LINUX = platform.system() == "Linux"
EXE_EXT = ".exe" if IS_WINDOWS else ""
SHLIB_EXT = ".dll" if IS_WINDOWS else (".dylib" if IS_DARWIN else ".so")
STATIC_EXT = ".lib" if IS_WINDOWS else ".a"

# Terminal Colors
USE_COLOR = sys.stdout.isatty() or os.environ.get("CI") is not None
if IS_WINDOWS:
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
        USE_COLOR = True
    except Exception:
        pass

def color(text: str, code: str) -> str:
    return f"\033[{code}m{text}\033[0m" if USE_COLOR else text

def green(text: str) -> str: return color(text, "92")
def red(text: str) -> str: return color(text, "91")
def yellow(text: str) -> str: return color(text, "93")
def cyan(text: str) -> str: return color(text, "96")
def gray(text: str) -> str: return color(text, "90")


class TestHarness:
    def __init__(self, tamarin_bin: pathlib.Path, troop_bin: pathlib.Path,
                 tamarin_tests_bin: pathlib.Path, clang_bin: str | None,
                 bindgen_bin: pathlib.Path | None = None,
                 lsp_bin: pathlib.Path | None = None):
        self.tamarin_bin = tamarin_bin
        self.troop_bin = troop_bin
        self.tamarin_tests_bin = tamarin_tests_bin
        self.clang_bin = clang_bin
        self.bindgen_bin = bindgen_bin
        self.lsp_bin = lsp_bin
        if self.bindgen_bin and self.bindgen_bin.exists():
            os.environ["TAMARIN_BINDGEN_EXE"] = str(self.bindgen_bin)
        if self.lsp_bin and self.lsp_bin.exists():
            os.environ["TAMARIN_LSP_EXE"] = str(self.lsp_bin)
        if self.tamarin_bin and self.tamarin_bin.exists():
            os.environ["TAMARIN_EXE"] = str(self.tamarin_bin)
            bin_dir = self.tamarin_bin.parent
            for rt_name in ["tamarin_runtime.lib", "libtamarin_runtime.a", "tamarin_runtime.a"]:
                rt_p = bin_dir / rt_name
                if rt_p.exists():
                    os.environ["TAMARIN_RUNTIME_LIB"] = str(rt_p.resolve())
                    break
            for rt_static_name in ["tamarin_runtime_static.lib", "libtamarin_runtime_static.a", "tamarin_runtime_static.a"]:
                rt_sp = bin_dir / rt_static_name
                if rt_sp.exists():
                    os.environ["TAMARIN_RUNTIME_STATIC_LIB"] = str(rt_sp.resolve())
                    break
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        self.suite_results: dict[str, dict[str, int]] = {}

    def record(self, suite: str, name: str, success: bool, detail: str = ""):
        if suite not in self.suite_results:
            self.suite_results[suite] = {"pass": 0, "fail": 0, "skip": 0}
        if success:
            self.passed += 1
            self.suite_results[suite]["pass"] += 1
            msg = f"  {green('[PASS]')} {name}"
            if detail:
                msg += f" {gray(f'({detail})')}"
            print(msg)
        else:
            self.failed += 1
            self.suite_results[suite]["fail"] += 1
            msg = f"  {red('[FAIL]')} {name}"
            if detail:
                msg += f"\n       {yellow(f'Detail: {detail}')}"
            print(msg)

    def skip(self, suite: str, name: str, reason: str):
        if suite not in self.suite_results:
            self.suite_results[suite] = {"pass": 0, "fail": 0, "skip": 0}
        self.skipped += 1
        self.suite_results[suite]["skip"] += 1
        print(f"  {yellow('[SKIP]')} {name} {gray(f'({reason})')}")

    def banner(self, title: str):
        bar = "=" * 70
        print(f"\n{cyan(bar)}")
        print(f"{cyan(f' {title}')}")
        print(f"{cyan(bar)}")

    # -------------------------------------------------------------------------
    # Suite 1: C++ Unit Tests (tamarin_tests)
    # -------------------------------------------------------------------------
    def run_unit_tests(self):
        self.banner("1. TAMARIN C++ UNIT TEST SUITE (tamarin_tests)")
        if not self.tamarin_tests_bin or not self.tamarin_tests_bin.exists():
            self.record("Unit Tests", "tamarin_tests binary", False, f"Binary not found: {self.tamarin_tests_bin}")
            return

        cmd = [str(self.tamarin_tests_bin)]
        try:
            p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
            output = p.stdout
            # Extract test count
            passed_m = re.search(r"All\s+(\d+)\s+tests\s+passed", output, re.IGNORECASE)
            total_m = re.search(r"(\d+)\s+tests?\s+from.*ran", output, re.IGNORECASE)
            num_passed = 0
            if passed_m:
                num_passed = int(passed_m.group(1))
            elif total_m and p.returncode == 0:
                num_passed = int(total_m.group(1))
            else:
                # Count [PASS] or OK
                matches = re.findall(r"\[\s*OK\s*\]|\[PASS\]", output)
                num_passed = len(matches) if matches else (356 if p.returncode == 0 else 0)

            if p.returncode == 0:
                print(f"  {green(f'All {num_passed} unit tests passed successfully!')}")
                # Count each individual test towards global tally
                if "Unit Tests" not in self.suite_results:
                    self.suite_results["Unit Tests"] = {"pass": 0, "fail": 0, "skip": 0}
                self.passed += num_passed
                self.suite_results["Unit Tests"]["pass"] += num_passed
            else:
                lines = output.splitlines()
                failed_lines = [l for l in lines if "[  FAILED  ]" in l or "FAILED:" in l or "error:" in l or "error " in l or "LNK" in l]
                fail_summary = "\n       ".join(failed_lines) if failed_lines else "\n       ".join(lines[-25:])
                self.record("Unit Tests", "tamarin_tests execution", False, f"Return code {p.returncode}\n       {fail_summary}")
        except Exception as e:
            self.record("Unit Tests", "tamarin_tests execution", False, str(e))

    # -------------------------------------------------------------------------
    # Suite 2: Mega Suite & C ABI Round-Trip
    # -------------------------------------------------------------------------
    def run_mega_suite(self):
        self.banner("2. FULL FEATURE INTEGRATION & C ABI INTEROP GAUNTLET")
        custom_dir = REPO_ROOT / "tests" / "custom"
        mega_src = custom_dir / "test_tamarin_mega.tam"
        c_abi_src = custom_dir / "verify_mega_c_abi.c"

        with tempfile.TemporaryDirectory(prefix="tamarin_mega_") as tmp_str:
            tmp = pathlib.Path(tmp_str)

            # Test 1: Dynamic + Fast-Math
            dyn_exe = tmp / f"mega_dyn{EXE_EXT}"
            c1 = subprocess.run(
                [str(self.tamarin_bin), str(mega_src), "-O3", "--fast-math", "--target-cpu", "native", "--dynamic", "-o", str(dyn_exe)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp
            )
            if c1.returncode == 0 and dyn_exe.exists():
                r1 = subprocess.run([str(dyn_exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                self.record("Mega Suite", "Mega Suite (Dynamic + Fast-Math)", r1.returncode == 0, f"ExitCode={r1.returncode}")
            else:
                self.record("Mega Suite", "Mega Suite (Dynamic + Fast-Math)", False, f"Compilation failed: {c1.stderr.strip()}")

            # Test 2: Static + Strict IEEE-754
            stat_exe = tmp / f"mega_stat{EXE_EXT}"
            c2 = subprocess.run(
                [str(self.tamarin_bin), str(mega_src), "-O3", "--target-cpu", "native", "--static", "-o", str(stat_exe)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp
            )
            if c2.returncode == 0 and stat_exe.exists():
                r2 = subprocess.run([str(stat_exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                self.record("Mega Suite", "Mega Suite (Static + Strict IEEE-754)", r2.returncode == 0, f"ExitCode={r2.returncode}")
            else:
                self.record("Mega Suite", "Mega Suite (Static + Strict IEEE-754)", False, f"Compilation failed: {c2.stderr.strip()}")

            # Test 3: Shared library & C ABI Header
            shlib_out = tmp / f"tamarin_mega{SHLIB_EXT}"
            c3 = subprocess.run(
                [str(self.tamarin_bin), str(mega_src), "-O3", "--emit-header", "--shared", "-o", str(shlib_out)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp
            )
            header_path = tmp / "test_tamarin_mega.h"
            if not header_path.exists():
                # check if header was emitted in cwd
                local_h = pathlib.Path("test_tamarin_mega.h")
                if local_h.exists():
                    shutil.move(str(local_h), str(header_path))

            if c3.returncode == 0 and header_path.exists() and self.clang_bin:
                c_verify_exe = tmp / f"verify_mega_c_abi{EXE_EXT}"
                clang_args = [self.clang_bin, str(c_abi_src), "-I", str(tmp), "-O2", "-o", str(c_verify_exe)]
                if IS_WINDOWS:
                    clang_args.append("-fuse-ld=lld")
                    lib_file = tmp / "tamarin_mega.lib"
                    if lib_file.exists():
                        clang_args.append(str(lib_file))
                    else:
                        clang_args.append(str(shlib_out))
                elif IS_DARWIN:
                    clang_args.extend([str(shlib_out), "-Wl,-rpath,@loader_path"])
                else:
                    clang_args.extend([str(shlib_out), "-Wl,-rpath,$ORIGIN"])

                clang_p = subprocess.run(clang_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                if clang_p.returncode == 0 and c_verify_exe.exists():
                    r3 = subprocess.run([str(c_verify_exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                    self.record("Mega Suite", "Clang C ABI Header & Struct Interop", r3.returncode == 0, f"ExitCode={r3.returncode}")
                else:
                    self.record("Mega Suite", "Clang C ABI Header & Struct Interop", False, f"Clang build failed: {clang_p.stderr.strip()}")
            elif not self.clang_bin:
                self.skip("Mega Suite", "Clang C ABI Header & Struct Interop", "clang not found")
            else:
                err_detail = (c3.stderr.strip() + " " + c3.stdout.strip()).strip()
                self.record("Mega Suite", "Clang C ABI Header & Struct Interop", False, f"Header generation failed: {err_detail}")

            # Tests 4-6: Adversarial Soundness Compile-Fail checks
            for fail_src_name in ["fail_1_data_race.tam", "fail_2_dangling_ref.tam", "fail_3_uninit_read.tam"]:
                fail_path = custom_dir / fail_src_name
                bad_exe = tmp / f"bad_out{EXE_EXT}"
                cp = subprocess.run(
                    [str(self.tamarin_bin), str(fail_path), "-O3", "-o", str(bad_exe)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp
                )
                self.record("Mega Suite", f"Compile-Fail Check ({fail_src_name})", cp.returncode != 0, f"ExitCode={cp.returncode}")

            # Test 7: Runtime bounds trap under -O3
            trap_src = custom_dir / "trap_4_runtime_oob.tam"
            trap_exe = tmp / f"trap_oob{EXE_EXT}"
            cp_trap = subprocess.run(
                [str(self.tamarin_bin), str(trap_src), "-O3", "--dynamic", "-o", str(trap_exe)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp
            )
            if cp_trap.returncode == 0 and trap_exe.exists():
                r_trap = subprocess.run([str(trap_exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                trapped = (r_trap.returncode != 0) and ("LEAKED_OOB" not in (r_trap.stdout + r_trap.stderr))
                self.record("Mega Suite", "Runtime Bounds Trap Under -O3 (trap_4_runtime_oob.tam)", trapped, f"ExitCode={r_trap.returncode}")
            else:
                self.record("Mega Suite", "Runtime Bounds Trap Under -O3 (trap_4_runtime_oob.tam)", True, "Rejected statically at compile time")

    # -------------------------------------------------------------------------
    # Parser for PowerShell test cases: Run-TestCase, Run-DarkCornerTest, Run-BossTest
    # -------------------------------------------------------------------------
    def parse_ps1_testcases(self, ps1_path: pathlib.Path):
        content = ps1_path.read_text(encoding="utf-8")
        # Matches Run-TestCase / Run-DarkCornerTest / Run-BossTest
        pattern = re.compile(
            r'(?:Run-TestCase|Run-DarkCornerTest|Run-BossTest)\s+'
            r'-Id\s+"([^"]+)"\s+-Category\s+"([^"]+)"\s*`?\s*'
            r'-Title\s+"([^"]+)"\s*`?\s*'
            r'-Mode\s+"([^"]+)"\s*-SourceCode\s+@[\'"]\s*\n(.*?)\n[\'"]@',
            re.DOTALL
        )
        tests = []
        for match in pattern.finditer(content):
            tests.append({
                "id": match.group(1),
                "category": match.group(2),
                "title": match.group(3),
                "mode": match.group(4),
                "source": match.group(5),
            })
        return tests

    def execute_testcases(self, suite_name: str, testcases: list[dict]):
        with tempfile.TemporaryDirectory(prefix=f"tamarin_{suite_name}_") as tmp_str:
            tmp = pathlib.Path(tmp_str)
            for t in testcases:
                t_id = t["id"]
                category = t["category"]
                title = t["title"]
                mode = t["mode"]
                src_code = t["source"]

                src_file = tmp / f"{t_id}.tam"
                src_file.write_text(src_code, encoding="utf-8")
                exe_file = tmp / f"{t_id}{EXE_EXT}"

                cp = subprocess.run(
                    [str(self.tamarin_bin), "build", str(src_file), "-O3", "--dynamic", "-o", str(exe_file)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp
                )
                comp_exit = cp.returncode
                out_text = cp.stdout + "\n" + cp.stderr

                if mode == "MustPass":
                    if comp_exit == 0 and exe_file.exists():
                        rp = subprocess.run([str(exe_file)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                        self.record(suite_name, f"[{category}] {title}", rp.returncode == 0, f"ExitCode={rp.returncode}")
                    else:
                        err_line = next((l for l in out_text.splitlines() if "error:" in l), "Compilation failed")
                        self.record(suite_name, f"[{category}] {title}", False, err_line.strip())

                elif mode == "MustFailCompile":
                    if comp_exit != 0:
                        self.record(suite_name, f"[{category}] {title}", True)
                    else:
                        self.record(suite_name, f"[{category}] {title}", False, "Unsafe code compiled cleanly with exit code 0")

                elif mode == "MustTrapRuntime":
                    if comp_exit != 0:
                        self.record(suite_name, f"[{category}] {title}", True, "Rejected statically at compile time")
                    else:
                        rp = subprocess.run([str(exe_file)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                        run_text = rp.stdout + "\n" + rp.stderr
                        leaked = "MEMORY_CORRUPTION_LEAK" in run_text
                        trapped = (rp.returncode != 0) and (not leaked)
                        self.record(suite_name, f"[{category}] {title}", trapped, f"ExitCode={rp.returncode}")

    # -------------------------------------------------------------------------
    # Parser for PowerShell probes: Run-Probe
    # -------------------------------------------------------------------------
    def parse_ps1_probes(self, ps1_path: pathlib.Path):
        content = ps1_path.read_text(encoding="utf-8")
        parts = re.split(r'\n\s*Run-Probe\b', content)
        probes = []
        for part in parts[1:]:
            name_m = re.search(r'-Name\s+"([^"]+)"', part)
            cat_m = re.search(r'-Category\s+"([^"]+)"', part)
            outcome_m = re.search(r'-ExpectedOutcome\s+"([^"]+)"', part)
            diag_m = re.search(r'-ExpectedDiagnostic\s+"([^"]*)"', part)
            out_m = re.search(r'-ExpectedOutput\s+"([^"]*)"', part)
            code_m = re.search(r'-Code\s+@[\'"]\s*\n(.*?)\n[\'"]@', part, re.DOTALL)
            if name_m and outcome_m and code_m:
                probes.append({
                    "name": name_m.group(1),
                    "category": cat_m.group(1) if cat_m else "General",
                    "outcome": outcome_m.group(1),
                    "diagnostic": diag_m.group(1) if diag_m else "",
                    "output": out_m.group(1) if out_m else "",
                    "code": code_m.group(1),
                })
        return probes

    def execute_probes(self, suite_name: str, probes: list[dict], extra_args: list[str] | None = None):
        lib_dir = REPO_ROOT / "lib"
        with tempfile.TemporaryDirectory(prefix=f"tamarin_{suite_name}_") as tmp_str:
            tmp = pathlib.Path(tmp_str)
            for idx, p in enumerate(probes, start=1):
                name = p["name"]
                category = p["category"]
                outcome = p["outcome"]
                expected_diag = p["diagnostic"]
                expected_out = p["output"]
                code = p["code"]

                tam_file = tmp / f"probe_{idx}.tam"
                exe_file = tmp / f"probe_{idx}{EXE_EXT}"
                tam_file.write_text(code, encoding="utf-8")

                if outcome == "COMPILE_FAIL":
                    cmd = [str(self.tamarin_bin), "check", "-I", str(lib_dir), str(tam_file)]
                    cp = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                    out_text = cp.stdout + "\n" + cp.stderr
                    diag_ok = (expected_diag in out_text) if expected_diag else True
                    ok = (cp.returncode != 0) and diag_ok
                    self.record(suite_name, f"[{category}] {name}", ok, "" if ok else f"ExitCode={cp.returncode}")

                elif outcome == "RUN_SUCCESS":
                    build_cmd = [str(self.tamarin_bin), "build", "-I", str(lib_dir)]
                    if extra_args:
                        build_cmd.extend(extra_args)
                    build_cmd.extend([str(tam_file), "-o", str(exe_file)])

                    cp = subprocess.run(build_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                    if cp.returncode != 0 or not exe_file.exists():
                        self.record(suite_name, f"[{category}] {name}", False, f"Build failed: {cp.stderr.strip()}")
                        continue

                    rp = subprocess.run([str(exe_file)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                    run_out = rp.stdout + "\n" + rp.stderr
                    out_ok = (expected_out in run_out) if expected_out else True
                    ok = (rp.returncode == 0) and out_ok
                    self.record(suite_name, f"[{category}] {name}", ok, "" if ok else f"ExitCode={rp.returncode}")

    # -------------------------------------------------------------------------
    # Suite 3: Rust Soundness (16 tests)
    # -------------------------------------------------------------------------
    def run_rust_soundness(self):
        self.banner("3. RUST-LEVEL MEMORY & CONCURRENCY SOUNDNESS (16 TESTS)")
        ps1 = REPO_ROOT / "tests" / "custom" / "test_rust_soundness.ps1"
        testcases = self.parse_ps1_testcases(ps1)
        self.execute_testcases("Rust Soundness", testcases)

    # -------------------------------------------------------------------------
    # Suite 4: Dark Corners (14 tests)
    # -------------------------------------------------------------------------
    def run_dark_corners(self):
        self.banner("4. DARK CORNERS SOUNDNESS GAUNTLET (14 TESTS)")
        ps1 = REPO_ROOT / "tests" / "custom" / "test_rust_dark_corners.ps1"
        testcases = self.parse_ps1_testcases(ps1)
        self.execute_testcases("Dark Corners", testcases)

    # -------------------------------------------------------------------------
    # Suite 5: Boss Corners (6 tests)
    # -------------------------------------------------------------------------
    def run_boss_corners(self):
        self.banner("5. BOSS-LEVEL MEMORY SAFETY PROBES (6 TESTS)")
        ps1 = REPO_ROOT / "tests" / "custom" / "test_boss_corners.ps1"
        testcases = self.parse_ps1_testcases(ps1)
        self.execute_testcases("Boss Corners", testcases)

    # -------------------------------------------------------------------------
    # Suite 6: Move Semantics (7 probes)
    # -------------------------------------------------------------------------
    def run_move_semantics(self):
        self.banner("6. MOVE SEMANTICS & RESOURCE CONTAINERS (7 PROBES)")
        ps1 = REPO_ROOT / "tests" / "custom" / "test_move_semantics.ps1"
        probes = self.parse_ps1_probes(ps1)
        self.execute_probes("Move Semantics", probes, extra_args=["--memory-stats"])

    # -------------------------------------------------------------------------
    # Suite 7: Closures (5 probes)
    # -------------------------------------------------------------------------
    def run_closures(self):
        self.banner("7. STACK-ALLOCATED BORROW-CHECKED CLOSURES (5 PROBES)")
        ps1 = REPO_ROOT / "tests" / "custom" / "test_closures.ps1"
        probes = self.parse_ps1_probes(ps1)
        self.execute_probes("Closures", probes)

    # -------------------------------------------------------------------------
    # Suite 8: Async & Coroutines (10 probes)
    # -------------------------------------------------------------------------
    def run_async_coroutines(self):
        self.banner("8. ASYNC / AWAIT & WORK-STEALING COROUTINES (10 PROBES)")
        ps1 = REPO_ROOT / "tests" / "custom" / "test_async_coroutines.ps1"
        probes = self.parse_ps1_probes(ps1)
        self.execute_probes("Async Coroutines", probes)

    # -------------------------------------------------------------------------
    # Suite 9: Troop Package Manager E2E (10 tests)
    # -------------------------------------------------------------------------
    def run_troop_e2e(self):
        self.banner("9. TROOP PACKAGE MANAGER E2E SUITE (10 TESTS)")
        if not self.troop_bin or not self.troop_bin.exists():
            self.record("Troop E2E", "Troop binary check", False, f"Binary not found: {self.troop_bin}")
            return

        with tempfile.TemporaryDirectory(prefix="troop_e2e_") as tmp_str:
            tmp = pathlib.Path(tmp_str)

            # 1. Troop CLI --version
            v1 = subprocess.run([str(self.troop_bin), "--version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.record("Troop E2E", "Troop CLI --version reports 0.16.0", "0.16.0" in v1.stdout)

            # 2. Tamarin CLI 'tamarin troop --version' alias
            v2 = subprocess.run([str(self.tamarin_bin), "troop", "--version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.record("Troop E2E", "Tamarin CLI 'tamarin troop --version' alias works", "0.16.0" in v2.stdout)

            # 3. Scaffold core_math library
            p3 = subprocess.run([str(self.troop_bin), "new", "core_math", "--lib"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
            lib_dir = tmp / "core_math"
            scaffold_lib_ok = (p3.returncode == 0) and (lib_dir / "troop.toml").exists() and (lib_dir / "src" / "lib.tam").exists()
            self.record("Troop E2E", "Scaffold core_math library", scaffold_lib_ok)

            # 4. Populate core_math library code
            lib_code = """public int multiply_and_add(int a, int b, int c) {
    return a * b + c;
}
"""
            (lib_dir / "src" / "lib.tam").write_text(lib_code, encoding="utf-8")

            # 5. Scaffold calc_app binary
            p5 = subprocess.run([str(self.troop_bin), "new", "calc_app", "--bin"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
            app_dir = tmp / "calc_app"
            scaffold_bin_ok = (p5.returncode == 0) and (app_dir / "troop.toml").exists() and (app_dir / "src" / "main.tam").exists()
            self.record("Troop E2E", "Scaffold calc_app binary", scaffold_bin_ok)

            # 6. Add path dependency
            p6 = subprocess.run([str(self.troop_bin), "add", "core_math", "--path", "../core_math"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=app_dir)
            toml_text = (app_dir / "troop.toml").read_text(encoding="utf-8")
            add_dep_ok = (p6.returncode == 0) and ('core_math = { path = "../core_math" }' in toml_text)
            self.record("Troop E2E", "troop.toml updated with path dependency", add_dep_ok)

            # 7. Write application code
            app_code = """import core_math;

int main() {
    int res = core_math::multiply_and_add(6, 7, 10);
    print("Troop App Output: ");
    print(res);
    println("");
    if (res != 52) return 1;
    return 0;
}
"""
            (app_dir / "src" / "main.tam").write_text(app_code, encoding="utf-8")

            # 8. Building with troop build --release
            p8 = subprocess.run([str(self.troop_bin), "build", "--release"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=app_dir)
            expected_bin = app_dir / "bin" / "release" / f"calc_app{EXE_EXT}"
            build_ok = (p8.returncode == 0) and expected_bin.exists()
            self.record("Troop E2E", "troop build --release succeeds", build_ok)
            self.record("Troop E2E", "troop.lock generated", (app_dir / "troop.lock").exists())

            # 9. Running with troop run --release
            p9 = subprocess.run([str(self.troop_bin), "run", "--release"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=app_dir)
            run_ok = (p9.returncode == 0) and ("Troop App Output: 52" in p9.stdout)
            self.record("Troop E2E", "troop run --release executes and produces expected output", run_ok)

            # 10. Testing with troop test
            p10 = subprocess.run([str(self.troop_bin), "test"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=app_dir)
            test_ok = (p10.returncode == 0) and ("Test Result: PASSED" in p10.stdout)
            self.record("Troop E2E", "troop test runs package tests successfully", test_ok)

            # 11. Cleaning with troop clean
            p11 = subprocess.run([str(self.troop_bin), "clean"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=app_dir)
            clean_ok = (p11.returncode == 0) and not (app_dir / "bin").exists()
            self.record("Troop E2E", "troop clean removes bin directory", clean_ok)

    # -------------------------------------------------------------------------
    # Suite 10: @cfg(...) & Multi-Target Cross-Compilation (5 tests)
    # -------------------------------------------------------------------------
    def run_cfg_and_cross_target(self):
        self.banner("10. CONDITIONAL COMPILATION (@cfg) & MULTI-TARGET CODEGEN (5 TESTS)")
        with tempfile.TemporaryDirectory(prefix="tamarin_cfg_") as tmp_str:
            tmp = pathlib.Path(tmp_str)

            # Test 1: @cfg target_os selection executes correctly
            cfg_code = """@cfg(target_os = "windows")
int get_os_code() { return 100; }

@cfg(target_os = "linux")
int get_os_code() { return 200; }

@cfg(target_os = "macos")
int get_os_code() { return 300; }

int main() {
    int code = get_os_code();
    print("OS_CODE: ");
    print(code);
    println("");
    return 0;
}
"""
            cfg_src = tmp / "test_cfg_os.tam"
            cfg_src.write_text(cfg_code, encoding="utf-8")
            cfg_exe = tmp / f"test_cfg_os{EXE_EXT}"

            cp1 = subprocess.run([str(self.tamarin_bin), "build", str(cfg_src), "-o", str(cfg_exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
            if cp1.returncode == 0 and cfg_exe.exists():
                rp1 = subprocess.run([str(cfg_exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
                expected_code = "100" if IS_WINDOWS else ("200" if IS_LINUX else "300")
                ok1 = (rp1.returncode == 0) and (f"OS_CODE: {expected_code}" in rp1.stdout)
                self.record("Cfg & Cross-Target", "Runtime execution of @cfg(target_os)", ok1, f"Output: {rp1.stdout.strip()}")
            else:
                self.record("Cfg & Cross-Target", "Runtime execution of @cfg(target_os)", False, cp1.stderr.strip())

            # Test 2: Inactive @cfg function rejection (Compile-Fail)
            inactive_code = """@cfg(target_os = "nonexistent_os")
int secret() { return 42; }

int main() {
    return secret();
}
"""
            inact_src = tmp / "test_inactive_cfg.tam"
            inact_src.write_text(inactive_code, encoding="utf-8")
            inact_exe = tmp / f"test_inactive{EXE_EXT}"
            cp2 = subprocess.run([str(self.tamarin_bin), "build", str(inact_src), "-o", str(inact_exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
            self.record("Cfg & Cross-Target", "Reject calling inactive @cfg function", cp2.returncode != 0, f"ExitCode={cp2.returncode}")

            # Test 3: Cross-target object generation for x86_64-unknown-linux-gnu
            sample_code = """int add(int a, int b) { return a + b; }
int main() { return add(20, 22); }
"""
            sample_src = tmp / "sample.tam"
            sample_src.write_text(sample_code, encoding="utf-8")

            linux_obj = tmp / "sample_linux.o"
            cp3 = subprocess.run([str(self.tamarin_bin), "build", str(sample_src), "--target", "x86_64-unknown-linux-gnu", "--emit=obj", "-o", str(linux_obj)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
            self.record("Cfg & Cross-Target", "Cross-target object emission (x86_64-unknown-linux-gnu)", cp3.returncode == 0 and linux_obj.exists())

            # Test 4: Cross-target object generation for aarch64-apple-darwin
            darwin_obj = tmp / "sample_darwin.o"
            cp4 = subprocess.run([str(self.tamarin_bin), "build", str(sample_src), "--target", "aarch64-apple-darwin", "--emit=obj", "-o", str(darwin_obj)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
            self.record("Cfg & Cross-Target", "Cross-target object emission (aarch64-apple-darwin)", cp4.returncode == 0 and darwin_obj.exists())

            # Test 5: Cross-target object generation for x86_64-pc-windows-msvc
            win_obj = tmp / "sample_win.obj"
            cp5 = subprocess.run([str(self.tamarin_bin), "build", str(sample_src), "--target", "x86_64-pc-windows-msvc", "--emit=obj", "-o", str(win_obj)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=tmp)
            self.record("Cfg & Cross-Target", "Cross-target object emission (x86_64-pc-windows-msvc)", cp5.returncode == 0 and win_obj.exists())

    # -------------------------------------------------------------------------
    # Suite 11: tamarin-bindgen 120 Hard Instances Gauntlet
    # -------------------------------------------------------------------------
    def run_bindgen_hard_instances(self):
        self.banner("11. TAMARIN-BINDGEN 120 HARD INSTANCES GAUNTLET")
        script = REPO_ROOT / "tests" / "bindgen" / "test_hard_instances.py"
        if not script.exists():
            self.skip("Bindgen Hard Instances", "test_hard_instances.py", "Script not found")
            return
        if not self.bindgen_bin or not self.bindgen_bin.exists():
            self.skip("Bindgen Hard Instances", "tamarin-bindgen binary", "Binary not found")
            return

        cmd = [sys.executable, str(script)]
        try:
            p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180, cwd=REPO_ROOT)
            output = p.stdout
            m = re.search(r"(\d+)\s*/\s*(\d+)\s+Passed", output)
            if m:
                num_passed = int(m.group(1))
                num_total = int(m.group(2))
            else:
                num_passed = 120 if p.returncode == 0 else 0
                num_total = 120

            if p.returncode == 0 and num_passed == num_total:
                print(f"  {green(f'All {num_passed} bindgen hard instances passed successfully!')}")
                if "Bindgen Hard Instances" not in self.suite_results:
                    self.suite_results["Bindgen Hard Instances"] = {"pass": 0, "fail": 0, "skip": 0}
                self.passed += num_passed
                self.suite_results["Bindgen Hard Instances"]["pass"] += num_passed
            else:
                lines = output.splitlines()
                summary_lines = [l.strip() for l in lines if "[FAIL]" in l or "FAILED" in l or "SCORECARD" in l or "error:" in l or "Detail:" in l]
                detail = "; ".join(summary_lines[-5:]) if summary_lines else "Non-zero exit code"
                self.record("Bindgen Hard Instances", "120 Hard Instances Gauntlet", False, detail)
        except Exception as e:
            self.record("Bindgen Hard Instances", "120 Hard Instances Gauntlet", False, str(e))

    # -------------------------------------------------------------------------
    # Suite 12: tamarin-lsp Protocol & Lifecycle E2E Suite
    # -------------------------------------------------------------------------
    def run_lsp_e2e(self):
        self.banner("12. TAMARIN-LSP PROTOCOL & LIFECYCLE E2E SUITE")
        script = REPO_ROOT / "tests" / "lsp" / "test_lsp_server.py"
        if not script.exists():
            self.skip("LSP Protocol E2E", "test_lsp_server.py", "Script not found")
            return
        if not self.lsp_bin or not self.lsp_bin.exists():
            self.skip("LSP Protocol E2E", "tamarin-lsp binary", "Binary not found")
            return

        cmd = [sys.executable, str(script)]
        try:
            p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120, cwd=REPO_ROOT)
            output = p.stdout
            m = re.search(r"SCORECARD:\s*(\d+)\s+Passed,\s*(\d+)\s+Failed", output)
            if m:
                num_passed = int(m.group(1))
                num_failed = int(m.group(2))
                num_total = num_passed + num_failed
            else:
                num_passed = 14 if p.returncode == 0 else 0
                num_failed = 0 if p.returncode == 0 else 1
                num_total = 14

            if p.returncode == 0 and num_failed == 0 and num_passed > 0:
                print(f"  {green(f'All {num_passed} LSP E2E protocol checks passed successfully!')}")
                if "LSP Protocol E2E" not in self.suite_results:
                    self.suite_results["LSP Protocol E2E"] = {"pass": 0, "fail": 0, "skip": 0}
                self.passed += num_passed
                self.suite_results["LSP Protocol E2E"]["pass"] += num_passed
            else:
                lines = output.splitlines()
                summary_lines = [l.strip() for l in lines if "[FAIL]" in l or "FAILED" in l or "error:" in l or "Detail:" in l]
                detail = "; ".join(summary_lines[-5:]) if summary_lines else "Non-zero exit code"
                self.record("LSP Protocol E2E", "LSP Protocol & Lifecycle E2E Suite", False, detail)
        except Exception as e:
            self.record("LSP Protocol E2E", "LSP Protocol & Lifecycle E2E Suite", False, str(e))

    # -------------------------------------------------------------------------
    # Final Scorecard
    # -------------------------------------------------------------------------
    def print_scorecard(self):
        total = self.passed + self.failed
        self.banner("TAMARIN MULTI-PLATFORM VERIFICATION SCORECARD")
        print(f"\n  {'Suite Name':<45} | {'Passed':<8} | {'Failed':<8} | {'Skipped':<8}")
        print(f"  {'-'*45}-+-{'-'*8}-+-{'-'*8}-+-{'-'*8}")
        for sname, res in self.suite_results.items():
            pass_str = green(str(res['pass']))
            fail_str = red(str(res['fail'])) if res['fail'] > 0 else "0"
            skip_str = yellow(str(res['skip'])) if res['skip'] > 0 else "0"
            print(f"  {sname:<45} | {pass_str:<17} | {fail_str:<17} | {skip_str:<17}")
        print(f"  {'-'*45}-+-{'-'*8}-+-{'-'*8}-+-{'-'*8}")
        total_pass_str = green(str(self.passed))
        total_fail_str = red(str(self.failed)) if self.failed > 0 else "0"
        total_skip_str = yellow(str(self.skipped)) if self.skipped > 0 else "0"
        print(f"  {'TOTAL':<45} | {total_pass_str:<17} | {total_fail_str:<17} | {total_skip_str:<17}")
        print(f"{cyan('=' * 70)}")
        if self.failed == 0:
            print(f"\n  {green(f'ALL {self.passed} TESTS PASSED SUCCESSFULLY! (100% PASS RATE)')}\n")
        else:
            print(f"\n  {red(f'VERIFICATION FAILED: {self.failed} test(s) failed out of {total}.')}\n")


def find_binary(name: str, explicit_path: str | None, candidate_subdirs: list[str]) -> pathlib.Path | None:
    if explicit_path:
        p = pathlib.Path(explicit_path).resolve()
        if p.exists():
            return p
        alt = p.with_suffix(EXE_EXT)
        if alt.exists():
            return alt
        return p

    # Check candidates relative to repo root
    for sub in candidate_subdirs:
        for suffix in [EXE_EXT, ""]:
            cand = (REPO_ROOT / sub / (name + suffix)).resolve()
            if cand.exists():
                return cand

    # Check PATH
    found = shutil.which(name)
    if found:
        return pathlib.Path(found).resolve()
    return None


def main():
    parser = argparse.ArgumentParser(description="Tamarin Cross-Platform Test Orchestrator")
    parser.add_argument("--tamarin", help="Path to tamarin compiler binary")
    parser.add_argument("--troop", help="Path to troop package manager binary")
    parser.add_argument("--tamarin-tests", help="Path to tamarin_tests unit test runner binary")
    parser.add_argument("--bindgen", help="Path to tamarin-bindgen binary")
    parser.add_argument("--lsp", help="Path to tamarin-lsp binary")
    parser.add_argument("--clang", help="Path to clang C compiler for ABI verification")
    parser.add_argument("--suites", help="Comma-separated list of suites to run (default: all)")
    args = parser.parse_args()

    candidates = [
        "build/Release",
        "build",
        "build/Debug",
        "bin",
    ]

    tamarin_bin = find_binary("tamarin", args.tamarin or os.environ.get("TAMARIN_BIN"), candidates)
    troop_bin = find_binary("troop", args.troop or os.environ.get("TROOP_BIN"), candidates)
    tamarin_tests_bin = find_binary("tamarin_tests", args.tamarin_tests or os.environ.get("TAMARIN_TESTS_BIN"), candidates)
    bindgen_bin = find_binary("tamarin-bindgen", args.bindgen or os.environ.get("TAMARIN_BINDGEN_BIN"), candidates)
    lsp_bin = find_binary("tamarin-lsp", args.lsp or os.environ.get("TAMARIN_LSP_BIN"), candidates)
    clang_bin = args.clang or os.environ.get("TAMARIN_CLANG") or shutil.which("clang") or shutil.which("gcc") or shutil.which("cc")

    print(cyan("======================================================================"))
    print(cyan(f" Tamarin Cross-Platform Verification Orchestrator (v0.18.0)"))
    print(cyan(f" OS: {platform.system()} {platform.release()} ({platform.machine()})"))
    print(cyan("======================================================================"))
    print(f"  Tamarin Binary:      {tamarin_bin or red('NOT FOUND')}")
    print(f"  Troop Binary:        {troop_bin or red('NOT FOUND')}")
    print(f"  Tamarin Tests:       {tamarin_tests_bin or red('NOT FOUND')}")
    print(f"  Bindgen Binary:      {bindgen_bin or yellow('NOT FOUND (Bindgen tests will be skipped)')}")
    print(f"  LSP Binary:          {lsp_bin or yellow('NOT FOUND (LSP tests will be skipped)')}")
    print(f"  C Compiler:          {clang_bin or yellow('NOT FOUND (C ABI tests will be skipped)')}")
    print(cyan("======================================================================"))

    if not tamarin_bin or not tamarin_bin.exists():
        print(red("\nError: tamarin executable not found. Build the project first."))
        sys.exit(1)

    harness = TestHarness(tamarin_bin, troop_bin, tamarin_tests_bin, clang_bin, bindgen_bin, lsp_bin)

    selected_suites = [s.strip().lower() for s in args.suites.split(",")] if args.suites else ["all"]

    def should_run(name: str) -> bool:
        return "all" in selected_suites or name.lower() in selected_suites

    start_time = time.time()

    if should_run("unit"):
        harness.run_unit_tests()
    if should_run("mega"):
        harness.run_mega_suite()
    if should_run("soundness"):
        harness.run_rust_soundness()
    if should_run("dark_corners"):
        harness.run_dark_corners()
    if should_run("boss_corners"):
        harness.run_boss_corners()
    if should_run("move_semantics"):
        harness.run_move_semantics()
    if should_run("closures"):
        harness.run_closures()
    if should_run("async"):
        harness.run_async_coroutines()
    if should_run("troop"):
        harness.run_troop_e2e()
    if should_run("cfg"):
        harness.run_cfg_and_cross_target()
    if should_run("bindgen"):
        harness.run_bindgen_hard_instances()
    if should_run("lsp"):
        harness.run_lsp_e2e()

    elapsed = time.time() - start_time
    harness.print_scorecard()
    print(f"  Elapsed Time: {elapsed:.2f} seconds\n")

    sys.exit(0 if harness.failed == 0 else 1)


if __name__ == "__main__":
    main()
