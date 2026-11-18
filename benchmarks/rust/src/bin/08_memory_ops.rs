// 08_memory_ops.rs - Memory and String Operations Benchmark

fn main() {
    let mut buf = [0u8; 16384];

    // Initialize buffer outside timed section
    for i in 0..16384 {
        let rem = (i % 26) as u8;
        if (i & 1) == 0 {
            buf[i] = 65 + rem; // 'A' .. 'Z'
        } else {
            buf[i] = 97 + rem; // 'a' .. 'z'
        }
    }

    let start = std::time::Instant::now();

    // 1. In-place case inversion (200 passes)
    let mut transform_count: i64 = 0;
    for _ in 0..200 {
        for i in 0..16384 {
            let c = buf[i];
            if c >= 65 && c <= 90 {
                buf[i] = c + 32;
                transform_count += 1;
            } else if c >= 97 && c <= 122 {
                buf[i] = c - 32;
                transform_count += 1;
            }
        }
        std::hint::black_box(&mut buf);
    }

    // 2. Linear scan pattern search (100 passes)
    let mut matches: i64 = 0;
    for _ in 0..100 {
        for i in 0..16380 {
            if buf[i] == 65 && buf[i + 1] == 98 && buf[i + 2] == 67 {
                matches += 1;
            }
        }
    }

    let mut byte_sum: i64 = 0;
    for i in 0..16384 {
        byte_sum += (buf[i] as i8) as i64; // cast as signed char to match C/Tamarin signed char
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    let checksum = transform_count + matches + byte_sum;
    println!("TIME_MS:{:.4};TRANSFORMS:{};MATCHES:{};BYTE_SUM:{};CHECKSUM:{}",
             elapsed_ms, transform_count, matches, byte_sum, checksum);
}
