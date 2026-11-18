// 10a_high_churn_expert.rs - Expert Pattern A: High-churn short-lived region (50,000 small iterations)
// Genuine reusable thread-local arena pool: 0 allocations in steady state

use tamarin_benchmarks_rust::PooledArena;

fn main() {
    let timer = std::time::Instant::now();

    let mut sum: i64 = 0;
    for iter in 0..50000 {
        let r = PooledArena::new(4096);
        let values = r.alloc::<i64>(16, 8);
        for i in 0..16 {
            values[i] = (iter * 16 + i) as i64;
            sum += values[i];
        }
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    println!("TIME_MS:{:.4};SUM:{};CHECKSUM:{}",
             elapsed_ms, sum, sum);
}

