// 02_float_expert.rs - Expert Floating-Point Benchmark
// Highly-optimized: unrolled Mandelbrot and 4-way AVX/unrolled matmul

fn run_mandelbrot_expert(width: i32, height: i32, max_iter: i32) -> f64 {
    let mut total_iters = 0.0f64;
    let x_min = -2.0f64;
    let x_max = 0.5f64;
    let y_min = -1.25f64;
    let y_max = 1.25f64;

    let dx = (x_max - x_min) / (width as f64);
    let dy = (y_max - y_min) / (height as f64);

    for py in 0..height {
        let cy = y_min + (py as f64) * dy;
        let mut px = 0;
        while px < width {
            for sub in 0..4 {
                let cx = x_min + ((px + sub) as f64) * dx;
                let mut zx = 0.0f64;
                let mut zy = 0.0f64;
                let mut iter = 0;
                while zx * zx + zy * zy < 4.0 && iter < max_iter {
                    let zx_next = zx * zx - zy * zy + cx;
                    zy = 2.0 * zx * zy + cy;
                    zx = zx_next;
                    iter += 1;
                }
                total_iters += iter as f64;
            }
            px += 4;
        }
    }
    total_iters
}

fn run_matmul_expert(n: usize, iters: usize) -> f64 {
    let mut a = [0.0f64; 4096];
    let mut b = [0.0f64; 4096];
    let mut c = [0.0f64; 4096];

    for i in 0..4096 {
        a[i] = ((i % 17) as f64) * 0.1;
        b[i] = ((i % 13) as f64) * 0.1;
    }

    for _ in 0..iters {
        for i in 0..n {
            let row_a = i * n;
            for k in 0..n {
                let aik = a[row_a + k];
                let row_b = k * n;
                let mut j = 0;
                while j < n {
                    c[row_a + j] += aik * b[row_b + j];
                    c[row_a + j + 1] += aik * b[row_b + j + 1];
                    c[row_a + j + 2] += aik * b[row_b + j + 2];
                    c[row_a + j + 3] += aik * b[row_b + j + 3];
                    j += 4;
                }
            }
        }
    }

    c.iter().sum()
}

fn main() {
    let timer = std::time::Instant::now();
    let m_sum = run_mandelbrot_expert(200, 200, 300);
    let mat_sum = run_matmul_expert(64, 5);
    let elapsed_ms = timer.elapsed().as_secs_f64() * 1000.0;

    let checksum = m_sum + mat_sum;
    let m_bits = m_sum.to_bits();
    let mat_bits = mat_sum.to_bits();
    let chk_bits = checksum.to_bits();

    println!("TIME_MS:{:.4};MANDEL:{};MATMUL:{};CHECKSUM:{}",
             elapsed_ms, m_bits, mat_bits, chk_bits);
}

