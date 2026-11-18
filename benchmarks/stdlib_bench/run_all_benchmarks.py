import subprocess
import re
import statistics

def run_cmd(cmd):
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return res.stdout + res.stderr

def extract_metric(output, key):
    m = re.search(rf"{key}=([\d\.]+)", output)
    return float(m.group(1)) if m else None

def benchmark(cmd, name, runs=5):
    times = []
    output_sample = ""
    for _ in range(runs):
        out = run_cmd(cmd)
        output_sample = out.strip()
        t = extract_metric(out, "time")
        if t is not None:
            times.append(t)
    avg = statistics.mean(times) if times else 0.0
    stdev = statistics.stdev(times) if len(times) > 1 else 0.0
    return name, avg, stdev, output_sample

def main():
    print("=" * 80)
    print("TAMARIN STANDARD LIBRARY vs C vs C++ vs RUST BENCHMARK SUITE")
    print("=" * 80)

    # 1. Vector Benchmark (10M elements)
    print("\n--- Benchmark 1: Dynamic Vector (10,000,000 integers) ---")
    vec_benches = [
        (r".\benchmarks\stdlib_bench\vec_c.exe", "C (Idiomatic malloc/realloc)"),
        (r".\benchmarks\stdlib_bench\vec_c_expert.exe", "C Expert (pre-capacity, restrict)"),
        (r".\benchmarks\stdlib_bench\vec_rust.exe", "Rust (Idiomatic Vec::new())"),
        (r".\benchmarks\stdlib_bench\vec_rust_expert.exe", "Rust Expert (Vec::with_capacity)"),
        (r".\benchmarks\stdlib_bench\vec_tamarin.exe", "Tamarin (std.collections.vec::Vec)"),
        (r".\benchmarks\stdlib_bench\vec_tamarin_expert.exe", "Tamarin Expert (pre-capacity, slice)"),
    ]
    for cmd, name in vec_benches:
        _, avg, std, out = benchmark(cmd, name, 3)
        print(f"  {name:38} : {avg:6.2f} ms (±{std:4.2f} ms) | {out}")

    # 2. Sort Benchmark (2M elements)
    print("\n--- Benchmark 2: Sort (2,000,000 pseudo-random integers) ---")
    sort_benches = [
        (r".\benchmarks\stdlib_bench\sort_c.exe", "C (Standard Library qsort)"),
        (r".\benchmarks\stdlib_bench\sort_c_expert.exe", "C Expert (Quicksort median-of-3)"),
        (r".\benchmarks\stdlib_bench\sort_cpp.exe", "C++ (std::sort clang++ -O3)"),
        (r".\benchmarks\stdlib_bench\sort_rust.exe", "Rust (slice::sort)"),
        (r".\benchmarks\stdlib_bench\sort_rust_expert.exe", "Rust Expert (slice::sort_unstable)"),
        (r".\benchmarks\stdlib_bench\sort_tamarin.exe", "Tamarin (std.algorithms::sort)"),
        (r".\benchmarks\stdlib_bench\sort_tamarin_expert.exe", "Tamarin Expert (std.algorithms::radix_sort)"),
    ]
    for cmd, name in sort_benches:
        _, avg, std, out = benchmark(cmd, name, 3)
        print(f"  {name:38} : {avg:6.2f} ms (±{std:4.2f} ms) | {out}")

    # 3. String Benchmark (500k appends + 1M to_int)
    print("\n--- Benchmark 3: String (500,000 appends + 1,000,000 to_int) ---")
    str_benches = [
        (r".\benchmarks\stdlib_bench\str_c.exe", "C (Dynamic char* buffer)"),
        (r".\benchmarks\stdlib_bench\str_c_expert.exe", "C Expert (Pre-allocated char*)"),
        (r".\benchmarks\stdlib_bench\str_rust.exe", "Rust (Idiomatic String)"),
        (r".\benchmarks\stdlib_bench\str_rust_expert.exe", "Rust Expert (String::with_capacity)"),
        (r".\benchmarks\stdlib_bench\str_tamarin.exe", "Tamarin (std.string::String)"),
        (r".\benchmarks\stdlib_bench\str_tamarin_expert.exe", "Tamarin Expert (reserve + -O3 native)"),
    ]
    for cmd, name in str_benches:
        _, avg, std, out = benchmark(cmd, name, 3)
        print(f"  {name:38} : {avg:6.2f} ms (±{std:4.2f} ms) | {out}")

if __name__ == "__main__":
    main()
