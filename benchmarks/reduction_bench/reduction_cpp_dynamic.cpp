#include <immintrin.h>
#include <omp.h>
#include <chrono>
#include <cstdio>

int main() {
    auto t0 = std::chrono::high_resolution_clock::now();

    int total_sum = 0;
    int total_dot_all = 0;

    #pragma omp parallel for schedule(dynamic) reduction(+:total_sum, total_dot_all)
    for (int c = 0; c < 100; c++) {
        __m256 sum_vec = _mm256_setzero_ps();
        __m256 step = _mm256_setr_ps(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);
        float total_dot = 0.0f;
        __m128 p1 = _mm_setr_ps(1.0f, 2.0f, 3.0f, 4.0f);
        __m128 p2 = _mm_setr_ps(0.5f, 0.5f, 0.5f, 0.5f);

        for (int i = 0; i < 1000000; i++) {
            sum_vec = _mm256_add_ps(sum_vec, step);
            __m128 prod = _mm_mul_ps(p1, p2);
            __m128 shuf = _mm_shuffle_ps(prod, prod, _MM_SHUFFLE(2, 3, 0, 1));
            __m128 sums = _mm_add_ps(prod, shuf);
            shuf = _mm_movehl_ps(shuf, sums);
            sums = _mm_add_ss(sums, shuf);
            total_dot += _mm_cvtss_f32(sums);
        }

        __m128 vlow = _mm256_castps256_ps128(sum_vec);
        __m128 vhigh = _mm256_extractf128_ps(sum_vec, 1);
        __m128 vsum = _mm_add_ps(vlow, vhigh);
        __m128 shuf = _mm_shuffle_ps(vsum, vsum, _MM_SHUFFLE(2, 3, 0, 1));
        __m128 sums = _mm_add_ps(vsum, shuf);
        shuf = _mm_movehl_ps(shuf, sums);
        sums = _mm_add_ss(sums, shuf);
        float final_sum = _mm_cvtss_f32(sums);

        total_sum += (int)final_sum;
        total_dot_all += (int)total_dot;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();

    printf("TIME_MS=%d;SUM=%d;DOT=%d;\n", (int)elapsed, total_sum, total_dot_all);
    return 0;
}
