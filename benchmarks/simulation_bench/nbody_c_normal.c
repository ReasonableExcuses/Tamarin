// 13_nbody_simulation - C Normal (AoS)
// 4,096 bodies, 20 time steps = 335,544,320 pairwise interactions

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <windows.h>

typedef struct {
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
    float mass;
} Particle;

static LARGE_INTEGER g_start_time;
static LARGE_INTEGER g_freq;

static void benchmark_start(void) {
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_start_time);
}

static double benchmark_elapsed_ms(void) {
    LARGE_INTEGER end_time;
    QueryPerformanceCounter(&end_time);
    return (double)(end_time.QuadPart - g_start_time.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
}

int main(void) {
    static Particle p[4096];
    int64_t seed = 123456789;

    // Deterministic LCG initialization outside timed kernel
    for (int i = 0; i < 4096; i++) {
        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].x = ((float)(seed & 65535) / 65535.0f) * 20.0f - 10.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].y = ((float)(seed & 65535) / 65535.0f) * 20.0f - 10.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].z = ((float)(seed & 65535) / 65535.0f) * 20.0f - 10.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].vx = ((float)(seed & 65535) / 65535.0f) * 2.0f - 1.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].vy = ((float)(seed & 65535) / 65535.0f) * 2.0f - 1.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].vz = ((float)(seed & 65535) / 65535.0f) * 2.0f - 1.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].mass = ((float)(seed & 65535) / 65535.0f) * 9.9f + 0.1f;
    }

    float dt = 0.01f;
    float softening = 0.01f;

    benchmark_start();

    // 20 simulation time steps
    for (int step = 0; step < 20; step++) {
        for (int i = 0; i < 4096; i++) {
            float px = p[i].x;
            float py = p[i].y;
            float pz = p[i].z;
            float ax = 0.0f;
            float ay = 0.0f;
            float az = 0.0f;

            for (int j = 0; j < 4096; j++) {
                float dx = p[j].x - px;
                float dy = p[j].y - py;
                float dz = p[j].z - pz;
                float distSqr = dx * dx + dy * dy + dz * dz + softening;
                float invDist = 1.0f / sqrtf(distSqr);
                float invDist3 = invDist * invDist * invDist;
                float s = p[j].mass * invDist3;
                ax = ax + dx * s;
                ay = ay + dy * s;
                az = az + dz * s;
            }

            p[i].vx = p[i].vx + ax * dt;
            p[i].vy = p[i].vy + ay * dt;
            p[i].vz = p[i].vz + az * dt;

            p[i].x = px + p[i].vx * dt;
            p[i].y = py + p[i].vy * dt;
            p[i].z = pz + p[i].vz * dt;
        }
    }

    double elapsed_ms = benchmark_elapsed_ms();

    float total_x = 0.0f;
    float total_y = 0.0f;
    float total_z = 0.0f;
    float total_v = 0.0f;
    float total_ek = 0.0f;

    for (int i = 0; i < 4096; i++) {
        total_x = total_x + p[i].x;
        total_y = total_y + p[i].y;
        total_z = total_z + p[i].z;
        total_v = total_v + p[i].vx + p[i].vy + p[i].vz;
        float vsq = p[i].vx * p[i].vx + p[i].vy * p[i].vy + p[i].vz * p[i].vz;
        total_ek = total_ek + 0.5f * p[i].mass * vsq;
    }

    int64_t checksum = (int64_t)total_x + (int64_t)total_y + (int64_t)total_z + (int64_t)total_v + (int64_t)total_ek;

    printf("TIME_MS:%.4f;TOTAL_X:%lld;TOTAL_Y:%lld;TOTAL_Z:%lld;TOTAL_V:%lld;TOTAL_EK:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)(int64_t)total_x, (long long)(int64_t)total_y,
           (long long)(int64_t)total_z, (long long)(int64_t)total_v,
           (long long)(int64_t)total_ek, (long long)checksum);

    return 0;
}
