use std::time::Instant;
#[cfg(target_arch = "x86_64")]
use std::arch::x86_64::*;

fn main() {
    unsafe {
        run();
    }
}

#[target_feature(enable = "avx2")]
unsafe fn run() {
    let t0 = Instant::now();

    let num_threads = std::thread::available_parallelism().map(|n| n.get()).unwrap_or(8);

    let (total_sum, total_dot_all) = std::thread::scope(|s| {
        let chunks_per_thread = (100 + num_threads - 1) / num_threads;
        let mut handles = Vec::with_capacity(num_threads);

        for t in 0..num_threads {
            let start = (t * chunks_per_thread).min(100);
            let end = ((t + 1) * chunks_per_thread).min(100);
            if start >= end {
                continue;
            }

            handles.push(s.spawn(move || {
                let mut local_sum = 0i32;
                let mut local_dot = 0i32;

                for _ in start..end {
                    let mut sum_vec = _mm256_setzero_ps();
                    let step = _mm256_setr_ps(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0);
                    let mut total_dot = 0.0f32;
                    let p1 = _mm_setr_ps(1.0, 2.0, 3.0, 4.0);
                    let p2 = _mm_setr_ps(0.5, 0.5, 0.5, 0.5);

                    for _ in 0..1_000_000 {
                        sum_vec = _mm256_add_ps(sum_vec, step);
                        let prod = _mm_mul_ps(p1, p2);
                        let shuf = _mm_shuffle_ps(prod, prod, 0b10_11_00_01);
                        let sums = _mm_add_ps(prod, shuf);
                        let shuf2 = _mm_movehl_ps(shuf, sums);
                        let sums2 = _mm_add_ss(sums, shuf2);
                        total_dot += _mm_cvtss_f32(sums2);
                    }

                    let vlow = _mm256_castps256_ps128(sum_vec);
                    let vhigh = _mm256_extractf128_ps(sum_vec, 1);
                    let vsum = _mm_add_ps(vlow, vhigh);
                    let shuf = _mm_shuffle_ps(vsum, vsum, 0b10_11_00_01);
                    let sums = _mm_add_ps(vsum, shuf);
                    let shuf2 = _mm_movehl_ps(shuf, sums);
                    let sums2 = _mm_add_ss(sums, shuf2);
                    let final_sum = _mm_cvtss_f32(sums2);

                    local_sum += final_sum as i32;
                    local_dot += total_dot as i32;
                }

                (local_sum, local_dot)
            }));
        }

        let mut sum = 0i32;
        let mut dot = 0i32;
        for h in handles {
            let (s, d) = h.join().unwrap();
            sum += s;
            dot += d;
        }
        (sum, dot)
    });

    let elapsed = t0.elapsed().as_secs_f64() * 1000.0;
    println!("TIME_MS={};SUM={};DOT={};", elapsed as i32, total_sum, total_dot_all);
}
