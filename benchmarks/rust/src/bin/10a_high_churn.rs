// 10a_high_churn.rs - Pattern A: High-churn short-lived region (50,000 small iterations)

use tamarin_benchmarks_rust::Arena;

fn main() {
    let start = std::time::Instant::now();

    let mut sum: i64 = 0;
    for iter in 0..50000 {
        let r = Arena::new(4096);
        let values = r.alloc::<i64>(16, 8);
        for i in 0..16 {
            values[i] = (iter * 16 + i) as i64;
            sum += values[i];
        }
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    println!("TIME_MS:{:.4};SUM:{};CHECKSUM:{}",
             elapsed_ms, sum, sum);
}
