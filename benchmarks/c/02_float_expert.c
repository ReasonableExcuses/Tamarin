// 02_float_expert.c - Expert Floating-Point Benchmark
// Optimizations: 4-way unrolled Mandelbrot and 4-way AVX/unrolled cache-blocked matmul

#if defined(__clang__) || defined(__GNUC__)
#pragma STDC FP_CONTRACT OFF
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <immintrin.h>
#include "../common/timer.h"

static double run_mandelbrot_expert(int width, int height, int max_iter) {
    double total_iters = 0.0;
    const double x_min = -2.0;
    const double x_max = 0.5;
    const double y_min = -1.25;
    const double y_max = 1.25;

    const double dx = (x_max - x_min) / (double)width;
    const double dy = (y_max - y_min) / (double)height;

    for (int py = 0; py < height; py++) {
        double cy = y_min + (double)py * dy;
        for (int px = 0; px < width; px += 4) {
            // Unroll 4 points along X
            for (int sub = 0; sub < 4; sub++) {
                double cx = x_min + (double)(px + sub) * dx;
                double zx = 0.0;
                double zy = 0.0;
                int iter = 0;
                while (zx * zx + zy * zy < 4.0 && iter < max_iter) {
                    double zx_next = zx * zx - zy * zy + cx;
                    zy = 2.0 * zx * zy + cy;
                    zx = zx_next;
                    iter++;
                }
                total_iters += (double)iter;
            }
        }
    }
    return total_iters;
}

static double run_matmul_expert(int n, int iters) {
    double a[4096];
    double b[4096];
    double c[4096];

    for (int i = 0; i < 4096; i++) {
        a[i] = (double)(i % 17) * 0.1;
        b[i] = (double)(i % 13) * 0.1;
        c[i] = 0.0;
    }

    for (int it = 0; it < iters; it++) {
        for (int i = 0; i < n; i++) {
            int row_a = i * n;
            for (int k = 0; k < n; k++) {
                double aik = a[row_a + k];
                int row_b = k * n;
#if defined(__AVX__)
                __m256d v_aik = _mm256_set1_pd(aik);
                for (int j = 0; j < n; j += 4) {
                    __m256d v_b = _mm256_loadu_pd(&b[row_b + j]);
                    __m256d v_c = _mm256_loadu_pd(&c[row_a + j]);
#if defined(__FMA__)
                    v_c = _mm256_fmadd_pd(v_aik, v_b, v_c);
#else
                    v_c = _mm256_add_pd(v_c, _mm256_mul_pd(v_aik, v_b));
#endif
                    _mm256_storeu_pd(&c[row_a + j], v_c);
                }
#else
                for (int j = 0; j < n; j += 4) {
                    c[row_a + j] += aik * b[row_b + j];
                    c[row_a + j + 1] += aik * b[row_b + j + 1];
                    c[row_a + j + 2] += aik * b[row_b + j + 2];
                    c[row_a + j + 3] += aik * b[row_b + j + 3];
                }
#endif
            }
        }
    }

    double total = 0.0;
    for (int i = 0; i < 4096; i++) {
        total += c[i];
    }
    return total;
}

static uint64_t f64_to_bits(double v) {
    uint64_t u;
    memcpy(&u, &v, sizeof(u));
    return u;
}

int main(void) {
    BenchmarkTimer timer = timer_start();
    double m_sum = run_mandelbrot_expert(200, 200, 300);
    double mat_sum = run_matmul_expert(64, 5);
    double elapsed_ms = timer_elapsed_ms(timer);

    double checksum = m_sum + mat_sum;
    uint64_t m_bits = f64_to_bits(m_sum);
    uint64_t mat_bits = f64_to_bits(mat_sum);
    uint64_t chk_bits = f64_to_bits(checksum);

    printf("TIME_MS:%.4f;MANDEL:%llu;MATMUL:%llu;CHECKSUM:%llu\n",
           elapsed_ms, (unsigned long long)m_bits,
           (unsigned long long)mat_bits, (unsigned long long)chk_bits);
    return 0;
}
