// 06_branching_expert.rs - Expert Control Flow and Branching Benchmark
// Highly-optimized: unrolled branchless predictable loop and branchless arithmetic for LCG

fn main() {
    let timer = std::time::Instant::now();

    // 1. Predictable branching pattern (5,000,000 iterations) - unrolled 4x branchless
    let mut pred_sum: i64 = 0;
    let mut i: i64 = 0;
    while i < 5000000 {
        pred_sum += i * 3;
        pred_sum += (i + 1) * 5;
        pred_sum -= (i + 2) * 2;
        pred_sum += i + 3;
        i += 4;
    }

    // 2. Unpredictable branching pattern (5,000,000 iterations) - branchless 2's complement negation
    let mut unpred_sum: i64 = 0;
    let mut lcg: i64 = 2147483647;
    for _ in 0..5000000 {
        lcg = (lcg.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        let val = lcg >> 16;
        let mask = -((lcg & 1) as i64); // 0 if even, -1 if odd
        unpred_sum += (val ^ mask) - mask;
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    let checksum: i64 = pred_sum + unpred_sum;
    println!("TIME_MS:{:.4};PRED:{};UNPRED:{};CHECKSUM:{}",
             elapsed_ms, pred_sum, unpred_sum, checksum);
}

