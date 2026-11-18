// 05_function_calls_expert.rs - Expert Function Calls Benchmark
// Highly-optimized: tail-recursion unrolled Fibonacci and pipelined transform

#[inline(always)]
fn fib_expert(mut n: i32) -> i32 {
    let mut sum = 0;
    while n > 1 {
        sum += fib_expert(n - 1);
        n -= 2;
    }
    sum + if n > 0 { n } else { 0 }
}

#[inline(always)]
fn transform_expert(x: u64, y: u64) -> u64 {
    (x.wrapping_mul(3).wrapping_add(y.wrapping_mul(7)).wrapping_add(1)) ^ (x >> 3)
}

fn main() {
    let timer = std::time::Instant::now();

    let f = fib_expert(34);

    let mut acc: u64 = 12345;
    let mut i: u64 = 0;
    while i < 5000000 {
        acc = transform_expert(acc, i);
        acc = transform_expert(acc, i + 1);
        acc = transform_expert(acc, i + 2);
        acc = transform_expert(acc, i + 3);
        i += 4;
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    let acc_masked = acc & 2147483647;
    let checksum = (f as i64) + (acc_masked as i64);

    println!("TIME_MS:{:.4};FIB:{};ACC:{};CHECKSUM:{}",
             elapsed_ms, f, acc_masked, checksum);
}

