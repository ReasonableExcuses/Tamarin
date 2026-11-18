// 04_structures_soa_expert.c - Expert SoA Benchmark
// Highly-optimized: AVX2 8-way vectorized floating point simulation

#include <stdio.h>
#include <stdint.h>
#include <immintrin.h>
#include "../common/timer.h"

typedef struct {
    float x[5000];
    float y[5000];
    float z[5000];
    float vx[5000];
    float vy[5000];
    float vz[5000];
} ParticleSoA;

int main(void) {
    ParticleSoA p;

    for (int i = 0; i < 5000; i++) {
        p.x[i] = (float)(i & 63);
        p.y[i] = (float)((i + 1) & 63);
        p.z[i] = (float)((i + 2) & 63);
        p.vx[i] = 0.5f;
        p.vy[i] = -0.5f;
        p.vz[i] = 0.25f;
    }

    BenchmarkTimer timer = timer_start();

#if defined(__AVX__)
    __m256 v_dvx = _mm256_set1_ps(0.01f);
    __m256 v_dvy = _mm256_set1_ps(0.02f);
    __m256 v_dvz = _mm256_set1_ps(-0.01f);
    __m256 v_dt  = _mm256_set1_ps(0.1f);

    for (int step = 0; step < 200; step++) {
        for (int i = 0; i < 5000; i += 8) {
            __m256 vx = _mm256_loadu_ps(&p.vx[i]);
            __m256 vy = _mm256_loadu_ps(&p.vy[i]);
            __m256 vz = _mm256_loadu_ps(&p.vz[i]);
            __m256 x  = _mm256_loadu_ps(&p.x[i]);
            __m256 y  = _mm256_loadu_ps(&p.y[i]);
            __m256 z  = _mm256_loadu_ps(&p.z[i]);

            vx = _mm256_add_ps(vx, v_dvx);
            vy = _mm256_add_ps(vy, v_dvy);
            vz = _mm256_add_ps(vz, v_dvz);

            x = _mm256_add_ps(x, _mm256_mul_ps(vx, v_dt));
            y = _mm256_add_ps(y, _mm256_mul_ps(vy, v_dt));
            z = _mm256_add_ps(z, _mm256_mul_ps(vz, v_dt));

            _mm256_storeu_ps(&p.vx[i], vx);
            _mm256_storeu_ps(&p.vy[i], vy);
            _mm256_storeu_ps(&p.vz[i], vz);
            _mm256_storeu_ps(&p.x[i], x);
            _mm256_storeu_ps(&p.y[i], y);
            _mm256_storeu_ps(&p.z[i], z);
        }
    }
#else
    for (int step = 0; step < 200; step++) {
        for (int i = 0; i < 5000; i++) {
            p.vx[i] += 0.01f;
            p.vy[i] += 0.02f;
            p.vz[i] -= 0.01f;
            p.x[i] += p.vx[i] * 0.1f;
            p.y[i] += p.vy[i] * 0.1f;
            p.z[i] += p.vz[i] * 0.1f;
        }
    }
#endif

    double elapsed_ms = timer_elapsed_ms(timer);

    float total_x = 0.0f;
    float total_y = 0.0f;
    float total_z = 0.0f;
    float total_v = 0.0f;
    for (int i = 0; i < 5000; i++) {
        total_x += p.x[i];
        total_y += p.y[i];
        total_z += p.z[i];
        total_v += p.vx[i] + p.vy[i] + p.vz[i];
    }

    long checksum = (long)total_x + (long)total_y + (long)total_z + (long)total_v;
    printf("TIME_MS:%.4f;TOTAL_X:%ld;TOTAL_Y:%ld;TOTAL_Z:%ld;TOTAL_V:%ld;CHECKSUM:%ld\n",
           elapsed_ms, (long)total_x, (long)total_y, (long)total_z, (long)total_v, checksum);
    return 0;
}

