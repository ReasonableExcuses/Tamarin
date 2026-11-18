use std::time::Instant;

fn str_to_int(s: &str) -> i32 {
    let mut res = 0;
    for &b in s.as_bytes() {
        if b >= b'0' && b <= b'9' {
            res = res * 10 + (b - b'0') as i32;
        } else {
            break;
        }
    }
    res
}

fn main() {
    let t0 = Instant::now();
    let n = 500_000;
    let mut s = String::new();

    // 1. Append
    for _ in 0..n {
        s.push_str("tamarin_");
    }

    // 2. Starts with & Ends with
    let sw = s.starts_with("tamarin_");
    let ew = s.ends_with("tamarin_");

    // 3. String to int
    let mut num_sum: i64 = 0;
    let num_str = "12345678";
    for _ in 0..1_000_000 {
        num_sum += str_to_int(num_str) as i64;
    }

    let elapsed = t0.elapsed();
    let elapsed_ms = elapsed.as_secs_f64() * 1000.0;

    println!("[Rust String] N={} | len={} | sw={} | ew={} | numSum={} | time={:.2} ms",
             n, s.len(), sw, ew, num_sum, elapsed_ms);
}
