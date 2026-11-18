// 10b_large_single.rs - Pattern B: Large single region with many small allocations (10,000 allocations)

use tamarin_benchmarks_rust::Arena;

fn main() {
    let start = std::time::Instant::now();

    let mut sum: i64 = 0;
    let r = Arena::new(1048576);
    for alloc in 0..10000 {
        let buf = r.alloc::<i64>(8, 8);
        for j in 0..8 {
            buf[j] = (alloc * 8 + j) as i64;
            sum += buf[j];
        }
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    println!("TIME_MS:{:.4};SUM:{};CHECKSUM:{}",
             elapsed_ms, sum, sum);
}
