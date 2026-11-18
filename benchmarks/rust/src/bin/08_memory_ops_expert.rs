// 08_memory_ops_expert.rs - Expert Memory and String Operations Benchmark
// Highly-optimized: bitwise ASCII case inversion and unrolled pattern search

fn main() {
    let mut buf = [0u8; 16384];

    for i in 0..16384 {
        let rem = (i % 26) as u8;
        if (i & 1) == 0 {
            buf[i] = 65 + rem; // 'A' .. 'Z'
        } else {
            buf[i] = 97 + rem; // 'a' .. 'z'
        }
    }

    let timer = std::time::Instant::now();

    // 1. In-place case inversion (200 passes) - unrolled 8x bitwise XOR 0x20
    let mut transform_count: i64 = 0;
    for _ in 0..200 {
        let mut pcount: i64 = 0;
        let mut i = 0;
        while i < 16384 {
            for k in 0..8 {
                let c = buf[i + k];
                if (c >= b'A' && c <= b'Z') || (c >= b'a' && c <= b'z') {
                    buf[i + k] = c ^ 0x20;
                    pcount += 1;
                }
            }
            i += 8;
        }
        transform_count += pcount;
        std::hint::black_box(&mut buf);
    }

    // 2. Linear scan pattern search (100 passes) - unrolled 4x
    let mut matches: i64 = 0;
    for _ in 0..100 {
        let mut pmatches: i64 = 0;
        let mut i = 0;
        while i < 16380 {
            if buf[i] == b'A' && buf[i + 1] == b'b' && buf[i + 2] == b'C' { pmatches += 1; }
            if buf[i + 1] == b'A' && buf[i + 2] == b'b' && buf[i + 3] == b'C' { pmatches += 1; }
            if buf[i + 2] == b'A' && buf[i + 3] == b'b' && buf[i + 4] == b'C' { pmatches += 1; }
            if buf[i + 3] == b'A' && buf[i + 4] == b'b' && buf[i + 5] == b'C' { pmatches += 1; }
            i += 4;
        }
        matches += pmatches;
    }

    let mut byte_sum: i64 = 0;
    let mut i = 0;
    while i < 16384 {
        byte_sum += (buf[i] as i64) + (buf[i + 1] as i64) + (buf[i + 2] as i64) + (buf[i + 3] as i64)
                  + (buf[i + 4] as i64) + (buf[i + 5] as i64) + (buf[i + 6] as i64) + (buf[i + 7] as i64);
        i += 8;
    }

    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    let checksum: i64 = transform_count + matches + byte_sum;
    println!("TIME_MS:{:.4};TRANSFORMS:{};MATCHES:{};BYTE_SUM:{};CHECKSUM:{}",
             elapsed_ms, transform_count, matches, byte_sum, checksum);
}

