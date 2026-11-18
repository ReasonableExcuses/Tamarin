// 02_float.rs - Floating-Point Arithmetic Benchmark
// Tests Mandelbrot escape counts and dense matrix multiplication

fn run_mandelbrot(width: i32, height: i32, max_iter: i32) -> f64 {
    let mut total_iters = 0.0f64;
    let x_min = -2.0f64;
    let x_max = 0.5f64;
    let y_min = -1.25f64;
    let y_max = 1.25f64;

    let dx = (x_max - x_min) / (width as f64);
    let dy = (y_max - y_min) / (height as f64);

    for py in 0..height {
        let cy = y_min + (py as f64) * dy;
        for px in 0..width {
            let cx = x_min + (px as f64) * dx;
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
    }
    total_iters
}

fn run_matmul(n: usize, iters: usize) -> f64 {
    let mut a = [0.0f64; 4096];
    let mut b = [0.0f64; 4096];
    let mut c = [0.0f64; 4096];

    for i in 0..4096 {
        a[i] = ((i % 17) as f64) * 0.1;
        b[i] = ((i % 13) as f64) * 0.1;
        c[i] = 0.0;
    }

    for _ in 0..iters {
        for i in 0..n {
            let row_a = i * n;
            for k in 0..n {
                let aik = a[row_a + k];
                let row_b = k * n;
                for j in 0..n {
                    c[row_a + j] += aik * b[row_b + j];
                }
            }
        }
    }

    let mut total = 0.0f64;
    for i in 0..4096 {
        total += c[i];
    }
    total
}

fn main() {
    let start = std::time::Instant::now();
    let m_sum = run_mandelbrot(200, 200, 300);
    let mat_sum = run_matmul(64, 5);
    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;

    let checksum = m_sum + mat_sum;
    let m_bits = m_sum.to_bits();
    let mat_bits = mat_sum.to_bits();
    let chk_bits = checksum.to_bits();
    println!("TIME_MS:{:.4};MANDEL:{};MATMUL:{};CHECKSUM:{}",
             elapsed_ms, m_bits, mat_bits, chk_bits);
}
