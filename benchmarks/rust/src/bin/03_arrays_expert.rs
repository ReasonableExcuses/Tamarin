// 03_arrays_expert.rs - Expert Arrays and Bounds Checking Benchmark
// Highly-optimized: unrolled sequential scan, pipelined LCG updates, and vectorized transforms

fn main() {
    let mut arr = [0i32; 10000];
    let mut buf = [0i32; 10000];

    for i in 0..10000 {
        arr[i] = (i & 255) as i32;
    }

    let timer = std::time::Instant::now();

    // 1. Sequential scan and accumulation (500 passes) - unrolled 8x
    let mut scan_sum: i64 = 0;
    for _ in 0..500 {
        let mut pass_acc0: i64 = 0;
        let mut pass_acc1: i64 = 0;
        let mut i = 0;
        while i < 10000 {
            pass_acc0 += arr[i] as i64 + arr[i + 1] as i64 + arr[i + 2] as i64 + arr[i + 3] as i64;
            pass_acc1 += arr[i + 4] as i64 + arr[i + 5] as i64 + arr[i + 6] as i64 + arr[i + 7] as i64;
            i += 8;
        }
        scan_sum += pass_acc0 + pass_acc1;
    }

    // 2. Pseudo-random access and in-place updates (1,000,000 updates) - pipelined 4x
    let mut lcg: i64 = 123456789;
    let mut step = 0;
    while step < 1000000 {
        // Step 0
        lcg = (lcg.wrapping_mul(1103515245).wrapping_add(12345)) & 2147483647;
        let idx0 = (lcg % 10000) as usize;
        arr[idx0] = (arr[idx0] + (step & 255)) & 65535;

        // Step 1
        lcg = (lcg.wrapping_mul(1103515245).wrapping_add(12345)) & 2147483647;
        let idx1 = (lcg % 10000) as usize;
        arr[idx1] = (arr[idx1] + ((step + 1) & 255)) & 65535;

        // Step 2
        lcg = (lcg.wrapping_mul(1103515245).wrapping_add(12345)) & 2147483647;
        let idx2 = (lcg % 10000) as usize;
        arr[idx2] = (arr[idx2] + ((step + 2) & 255)) & 65535;

        // Step 3
        lcg = (lcg.wrapping_mul(1103515245).wrapping_add(12345)) & 2147483647;
        let idx3 = (lcg % 10000) as usize;
        arr[idx3] = (arr[idx3] + ((step + 3) & 255)) & 65535;

        step += 4;
    }

    // 3. Array copy, transform, and element sum (200 passes) - unrolled 4x
    let mut transform_sum: i64 = 0;
    for _ in 0..200 {
        let mut tacc: i64 = 0;
        let mut i = 0;
        while i < 10000 {
            let b0 = arr[i] * 2 + 1;
            let b1 = arr[i + 1] * 2 + 1;
            let b2 = arr[i + 2] * 2 + 1;
            let b3 = arr[i + 3] * 2 + 1;
            buf[i] = b0;
            buf[i + 1] = b1;
            buf[i + 2] = b2;
            buf[i + 3] = b3;
            tacc += ((b0 & 255) as i64) + ((b1 & 255) as i64) + ((b2 & 255) as i64) + ((b3 & 255) as i64);
            i += 4;
        }
        transform_sum += tacc;
    }

    let mut final_arr_sum: i64 = 0;
    let mut i = 0;
    while i < 10000 {
        final_arr_sum += arr[i] as i64 + arr[i + 1] as i64 + arr[i + 2] as i64 + arr[i + 3] as i64;
        i += 4;
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    let checksum: i64 = scan_sum + transform_sum + final_arr_sum;
    println!("TIME_MS:{:.4};SCAN:{};TRANSFORM:{};FINAL:{};CHECKSUM:{}",
             elapsed_ms, scan_sum, transform_sum, final_arr_sum, checksum);
}

