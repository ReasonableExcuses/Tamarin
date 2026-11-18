// 06_branching.rs - Control Flow and Branching Benchmark

fn main() {
    let start = std::time::Instant::now();

    // 1. Predictable branching pattern (5,000,000 iterations)
    let mut pred_sum: i64 = 0;
    for i in 0..5000000i64 {
        let rem = i % 4;
        if rem == 0 {
            pred_sum += i * 3;
        } else if rem == 1 {
            pred_sum += i * 5;
        } else if rem == 2 {
            pred_sum -= i * 2;
        } else {
            pred_sum += i;
        }
    }

    // 2. Unpredictable branching pattern (5,000,000 iterations via LCG)
    let mut unpred_sum: i64 = 0;
    let mut lcg: i64 = 2147483647;
    for _ in 0..5000000 {
        lcg = (lcg.wrapping_mul(1664525).wrapping_add(1013904223)) & 2147483647;
        if (lcg & 1) == 0 {
            unpred_sum += lcg >> 16;
        } else {
            unpred_sum -= lcg >> 16;
        }
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    let checksum = pred_sum + unpred_sum;
    println!("TIME_MS:{:.4};PRED:{};UNPRED:{};CHECKSUM:{}",
             elapsed_ms, pred_sum, unpred_sum, checksum);
}
