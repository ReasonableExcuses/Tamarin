// 10c_chunk_growth_expert.rs - Expert Pattern C: Chunk growth under sustained allocation pressure

use tamarin_benchmarks_rust::PooledArena;

fn main() {
    let timer = std::time::Instant::now();

    let mut sum: i64 = 0;
    for iter in 0..50 {
        let r = PooledArena::new(65536);
        let data = r.alloc::<i64>(25000, 8);
        for j in 0..25000 {
            data[j] = (iter * 25000 + j) as i64;
            sum += data[j];
        }
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    println!("TIME_MS:{:.4};SUM:{};CHECKSUM:{}",
             elapsed_ms, sum, sum);
}

