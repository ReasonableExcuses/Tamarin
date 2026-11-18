// 10d_empty_region_expert.rs - Expert Pattern D: Pure creation and destruction of empty regions

use tamarin_benchmarks_rust::PooledArena;

fn main() {
    let timer = std::time::Instant::now();

    let mut sum: i64 = 0;
    for _ in 0..50000 {
        let _r = PooledArena::new(65536);
        sum += 1;
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    println!("TIME_MS:{:.4};SUM:{};CHECKSUM:{}",
             elapsed_ms, sum, sum);
}

