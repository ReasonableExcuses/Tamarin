// 03_arrays.rs - Arrays and Bounds Checking Benchmark
// Tests sequential scan, random lookup, copy, and transform

fn main() {
    let mut arr = [0i32; 10000];
    let mut buf = [0i32; 10000];

    // Initialize outside timed section
    for i in 0..10000 {
        arr[i] = (i as i32) & 255;
    }

    let start = std::time::Instant::now();

    // 1. Sequential scan and accumulation (500 passes)
    let mut scan_sum: i64 = 0;
    for _ in 0..500 {
        for i in 0..10000 {
            scan_sum += arr[i] as i64;
        }
    }

    // 2. Pseudo-random access and in-place updates (1,000,000 updates)
    let mut lcg: i64 = 123456789;
    for step in 0..1000000 {
        lcg = (lcg.wrapping_mul(1103515245).wrapping_add(12345)) & 2147483647;
        let idx = (lcg % 10000) as usize;
        arr[idx] = (arr[idx] + (step & 255)) & 65535;
    }

    // 3. Array copy, transform, and element sum
    let mut transform_sum: i64 = 0;
    for _ in 0..200 {
        for i in 0..10000 {
            buf[i] = arr[i] * 2 + 1;
            transform_sum += (buf[i] & 255) as i64;
        }
    }

    let mut final_arr_sum: i64 = 0;
    for i in 0..10000 {
        final_arr_sum += arr[i] as i64;
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    let checksum = scan_sum + transform_sum + final_arr_sum;
    println!("TIME_MS:{:.4};SCAN:{};TRANSFORM:{};FINAL:{};CHECKSUM:{}",
             elapsed_ms, scan_sum, transform_sum, final_arr_sum, checksum);
}
