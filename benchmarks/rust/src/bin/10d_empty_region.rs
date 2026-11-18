// 10d_empty_region.rs - Pattern D: Pure creation and destruction of empty regions

use tamarin_benchmarks_rust::Arena;

fn main() {
    let start = std::time::Instant::now();

    let mut sum: i64 = 0;
    for _iter in 0..50000 {
        let _r = Arena::new(65536);
        sum += 1;
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    println!("TIME_MS:{:.4};SUM:{};CHECKSUM:{}",
             elapsed_ms, sum, sum);
}
