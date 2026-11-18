// 10b_large_single_expert.rs - Expert Pattern B: Large single region with many allocations

use tamarin_benchmarks_rust::PooledArena;

fn main() {
    let timer = std::time::Instant::now();

    let mut sum: i64 = 0;
    let r = PooledArena::new(1048576);

    for alloc_idx in 0..10000 {
        let buf = r.alloc::<i64>(8, 8);
        for j in 0..8 {
            buf[j] = (alloc_idx * 8 + j) as i64;
            sum += buf[j];
        }
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    println!("TIME_MS:{:.4};SUM:{};CHECKSUM:{}",
             elapsed_ms, sum, sum);
}

