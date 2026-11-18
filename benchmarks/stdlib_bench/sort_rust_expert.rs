use std::time::Instant;

fn main() {
    let n = 2_000_000;
    let mut data = Vec::with_capacity(n);
    let mut seed: u64 = 123456789;
    for _ in 0..n {
        seed = (seed.wrapping_mul(1664525)).wrapping_add(1013904223);
        data.push((seed & 0x7FFFFFFF) as i32);
    }

    let t0 = Instant::now();

    data.sort_unstable();

    let elapsed = t0.elapsed();
    let elapsed_ms = elapsed.as_secs_f64() * 1000.0;

    let mut ok = true;
    for i in 0..n - 1 {
        if data[i] > data[i + 1] {
            ok = false;
            break;
        }
    }

    println!("[Rust sort_unstable] N={} | sorted={} | min={} | max={} | time={:.2} ms",
             n, if ok { "YES" } else { "NO" }, data[0], data[n - 1], elapsed_ms);
}
