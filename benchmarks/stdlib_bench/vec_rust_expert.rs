use std::time::Instant;

fn main() {
    let t0 = Instant::now();
    let n: usize = 10_000_000;
    let mut v: Vec<i32> = Vec::with_capacity(n);

    // 1. Push
    for i in 0..n {
        v.push((i * 3 + 1) as i32);
    }

    // 2. Sum
    let sum: i64 = v.iter().map(|&x| x as i64).sum();

    // 3. Mutate
    for x in v.iter_mut() {
        *x += 7;
    }

    // 4. Pop
    let mut pop_sum: i64 = 0;
    for _ in 0..1_000_000 {
        if let Some(val) = v.pop() {
            pop_sum += val as i64;
        }
    }

    let elapsed = t0.elapsed();
    let elapsed_ms = elapsed.as_secs_f64() * 1000.0;

    println!("[Rust Expert]    N={} | sum={} | popSum={} | time={:.2} ms", n, sum, pop_sum, elapsed_ms);
}
