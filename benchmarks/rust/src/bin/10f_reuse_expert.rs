// 10f_reuse_expert.rs - Expert Pattern F: Sequential region reuse under bounded cache

use tamarin_benchmarks_rust::PooledArena;

fn main() {
    let timer = std::time::Instant::now();

    let mut sum: i64 = 0;
    for iter in 0..1000 {
        let r = PooledArena::new(4096);
        let data = r.alloc::<i64>(100, 8);
        for i in 0..100 {
            data[i] = (iter * 100 + i) as i64;
            sum += data[i];
        }
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    println!("TIME_MS:{:.4};SUM:{};CHECKSUM:{}",
             elapsed_ms, sum, sum);
}

