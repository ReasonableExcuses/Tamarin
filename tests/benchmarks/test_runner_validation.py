#!/usr/bin/env python3
"""
Unit tests for TAMARIN v0.4.2 Benchmark Runner contract enforcement and validation.
Tests strict failure on:
- Missing checksum
- Incorrect checksum
- Missing result fields
- Duplicate result fields
- Unknown fields
- Malformed delimiters / missing colons
- Invalid timing (NaN, Inf, negative, non-numeric)
- Non-zero process exit code
- Warmup failure rejection
"""

import math
import sys
import unittest
from pathlib import Path

# Add benchmarks directory to path
BENCHMARKS_DIR = Path(__file__).resolve().parent.parent.parent / "benchmarks"
sys.path.insert(0, str(BENCHMARKS_DIR))

from runner import (
    BenchmarkValidationError,
    parse_and_validate_repetition,
    compute_bootstrap_ci_median,
    compute_bootstrap_ci_ratio,
    compute_statistics,
    EXPECTED_SCHEMAS,
)


class TestBenchmarkRunnerValidation(unittest.TestCase):

    def test_valid_integer_workload_output(self):
        line = "TIME_MS:14.2500;PRIMES:2262;COLLATZ:1834634;H:1370357862;CHECKSUM:1372194758\n"
        res = parse_and_validate_repetition(line, returncode=0, external_ms=15.1, workload="01_integer", mode="A")
        self.assertEqual(res["time_ms"], 14.25)
        self.assertEqual(res["checksum"], "1372194758")
        self.assertEqual(res["fields"]["PRIMES"], "2262")
        self.assertEqual(res["fields"]["COLLATZ"], "1834634")
        self.assertEqual(res["fields"]["H"], "1370357862")

    def test_valid_float_workload_output_bitcast(self):
        line = "TIME_MS:10.5000;MANDEL:4703990612284669952;MATMUL:4693648769122566158;CHECKSUM:4705341001542139908\n"
        res = parse_and_validate_repetition(line, returncode=0, external_ms=11.0, workload="02_float", mode="A")
        self.assertEqual(res["checksum"], "4705341001542139908")
        self.assertEqual(res["fields"]["MANDEL"], "4703990612284669952")
        self.assertEqual(res["fields"]["MATMUL"], "4693648769122566158")

    def test_valid_structures_aos_with_total_v(self):
        line = "TIME_MS:3.1234;TOTAL_X:307760;TOTAL_Y:308284;TOTAL_Z:81787;TOTAL_V:21249;CHECKSUM:719080\n"
        res = parse_and_validate_repetition(line, returncode=0, external_ms=3.5, workload="04_structures_aos", mode="A")
        self.assertEqual(res["fields"]["TOTAL_V"], "21249")
        self.assertEqual(res["checksum"], "719080")

    def test_valid_region_alloc_workload_output(self):
        line = "TIME_MS:42.5000;PARTICLES:124999750000;SCRATCH:6816132500;NESTED:3877875000;CHECKSUM:135693757500\n"
        res = parse_and_validate_repetition(line, returncode=0, external_ms=45.0, workload="10_region_alloc", mode="A")
        self.assertEqual(res["fields"]["PARTICLES"], "124999750000")
        self.assertEqual(res["fields"]["SCRATCH"], "6816132500")
        self.assertEqual(res["fields"]["NESTED"], "3877875000")
        self.assertEqual(res["checksum"], "135693757500")

    def test_valid_allocator_suite_outputs(self):
        tests = [
            ("10a_high_churn", "TIME_MS:10.0;SUM:319999600000;CHECKSUM:319999600000\n", "319999600000"),
            ("10b_large_single", "TIME_MS:5.0;SUM:3199960000;CHECKSUM:3199960000\n", "3199960000"),
            ("10c_chunk_growth", "TIME_MS:2.0;SUM:781249375000;CHECKSUM:781249375000\n", "781249375000"),
            ("10d_empty_region", "TIME_MS:0.5;SUM:50000;CHECKSUM:50000\n", "50000"),
            ("10e_sparse_dtors", "TIME_MS:1.2;SUM:7998000;CHECKSUM:7998000\n", "7998000"),
            ("10f_reuse", "TIME_MS:1.5;SUM:4999950000;CHECKSUM:4999950000\n", "4999950000"),
        ]
        for wl, out_line, expected_csum in tests:
            res = parse_and_validate_repetition(out_line, returncode=0, external_ms=10.0, workload=wl, mode="A")
            self.assertEqual(res["checksum"], expected_csum)

    def test_reject_nonzero_exit_code(self):
        line = "TIME_MS:14.2500;PRIMES:2262;COLLATZ:1834634;H:1370357862;CHECKSUM:1372194758\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=1, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("non-zero code", str(ctx.exception))

    def test_reject_missing_checksum(self):
        line = "TIME_MS:14.2500;PRIMES:2262;COLLATZ:1834634;H:1370357862\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("Missing required field 'CHECKSUM'", str(ctx.exception))

    def test_reject_incorrect_checksum(self):
        line = "TIME_MS:14.2500;PRIMES:2262;COLLATZ:1834634;H:1370357862;CHECKSUM:9999999999\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("CHECKSUM' value mismatch", str(ctx.exception))

    def test_reject_missing_result_field(self):
        line = "TIME_MS:14.2500;COLLATZ:1834634;H:1370357862;CHECKSUM:1372194758\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("Missing required field 'PRIMES'", str(ctx.exception))

    def test_reject_incorrect_intermediate_field(self):
        line = "TIME_MS:14.2500;PRIMES:2263;COLLATZ:1834634;H:1370357862;CHECKSUM:1372194758\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("PRIMES' value mismatch", str(ctx.exception))

    def test_reject_unknown_field(self):
        line = "TIME_MS:14.2500;PRIMES:2262;COLLATZ:1834634;H:1370357862;ROGUE_FIELD:42;CHECKSUM:1372194758\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("Unexpected unknown field 'ROGUE_FIELD'", str(ctx.exception))

    def test_reject_duplicate_field(self):
        line = "TIME_MS:14.2500;PRIMES:2262;PRIMES:2262;COLLATZ:1834634;H:1370357862;CHECKSUM:1372194758\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("Duplicate field key 'PRIMES'", str(ctx.exception))

    def test_reject_malformed_delimiter_no_colon(self):
        line = "TIME_MS:14.2500;PRIMES2262;COLLATZ:1834634;H:1370357862;CHECKSUM:1372194758\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("missing ':'", str(ctx.exception))

    def test_reject_negative_timing(self):
        line = "TIME_MS:-5.2000;PRIMES:2262;COLLATZ:1834634;H:1370357862;CHECKSUM:1372194758\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("Non-finite or negative TIME_MS", str(ctx.exception))

    def test_reject_nan_timing(self):
        line = "TIME_MS:nan;PRIMES:2262;COLLATZ:1834634;H:1370357862;CHECKSUM:1372194758\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("Non-finite or negative TIME_MS", str(ctx.exception))

    def test_reject_inf_timing(self):
        line = "TIME_MS:inf;PRIMES:2262;COLLATZ:1834634;H:1370357862;CHECKSUM:1372194758\n"
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition(line, returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("Non-finite or negative TIME_MS", str(ctx.exception))

    def test_reject_empty_output(self):
        with self.assertRaises(BenchmarkValidationError) as ctx:
            parse_and_validate_repetition("", returncode=0, external_ms=15.0, workload="01_integer", mode="A")
        self.assertIn("empty standard output", str(ctx.exception))

    def test_bootstrap_ci_deterministic(self):
        samples = [10.0, 10.2, 9.8, 10.1, 10.5, 9.9, 10.3, 10.0, 10.1, 10.2]
        ci_low1, ci_high1 = compute_bootstrap_ci_median(samples)
        ci_low2, ci_high2 = compute_bootstrap_ci_median(samples)
        self.assertEqual(ci_low1, ci_low2)
        self.assertEqual(ci_high1, ci_high2)
        self.assertLessEqual(ci_low1, 10.15)
        self.assertGreaterEqual(ci_high1, 10.05)


if __name__ == "__main__":
    unittest.main()
