// 05_function_calls.rs - Function Calls and Inlining Benchmark

fn fib(n: i32) -> i32 {
    if n <= 1 {
        return n;
    }
    fib(n - 1) + fib(n - 2)
}

fn transform(x: u64, y: u64) -> u64 {
    (x.wrapping_mul(3).wrapping_add(y.wrapping_mul(7)).wrapping_add(1)) ^ (x >> 3)
}

fn main() {
    let start = std::time::Instant::now();

    let f = fib(34);

    let mut acc: u64 = 12345;
    for i in 0..5000000 {
        acc = transform(acc, i as u64);
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    let acc_masked = acc & 2147483647;
    let checksum = (f as i64) + (acc_masked as i64);
    println!("TIME_MS:{:.4};FIB:{};ACC:{};CHECKSUM:{}",
             elapsed_ms, f, acc_masked, checksum);
}
