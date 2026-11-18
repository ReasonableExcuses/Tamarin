// 02_float.c - Floating-Point Arithmetic Benchmark
// Tests Mandelbrot escape counts and dense matrix multiplication

#if defined(__clang__) || defined(__GNUC__)
#pragma STDC FP_CONTRACT OFF
#endif

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

static double run_mandelbrot(int width, int height, int max_iter) {
    double total_iters = 0.0;
    double x_min = -2.0;
    double x_max = 0.5;
    double y_min = -1.25;
    double y_max = 1.25;

    double dx = (x_max - x_min) / (double)width;
    double dy = (y_max - y_min) / (double)height;

    for (int py = 0; py < height; py++) {
        double cy = y_min + (double)py * dy;
        for (int px = 0; px < width; px++) {
            double cx = x_min + (double)px * dx;
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
    return total_iters;
}

static double run_matmul(int n, int iters) {
    // 64 x 64 = 4096 elements
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
                for (int j = 0; j < n; j++) {
                    c[row_a + j] += aik * b[row_b + j];
                }
            }
        }
    }

    double total = 0.0;
    for (int i = 0; i < 4096; i++) {
        total += c[i];
    }
    return total;
}

int main(void) {
    BenchmarkTimer timer = timer_start();

    double m_sum = run_mandelbrot(200, 200, 300);
    double mat_sum = run_matmul(64, 5);

    double elapsed_ms = timer_elapsed_ms(timer);

    double checksum = m_sum + mat_sum;
    uint64_t m_bits = double_to_bits(m_sum);
    uint64_t mat_bits = double_to_bits(mat_sum);
    uint64_t chk_bits = double_to_bits(checksum);
    printf("TIME_MS:%.4f;MANDEL:%llu;MATMUL:%llu;CHECKSUM:%llu\n",
           elapsed_ms, (unsigned long long)m_bits, (unsigned long long)mat_bits, (unsigned long long)chk_bits);
    return 0;
}
