// 04_structures_soa.c - SoA (Structure of Arrays) Benchmark

#include <stdio.h>
#include <stdint.h>
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

    // Initialize outside timed section
    for (int i = 0; i < 5000; i++) {
        p.x[i] = (float)(i & 63);
        p.y[i] = (float)((i + 1) & 63);
        p.z[i] = (float)((i + 2) & 63);
        p.vx[i] = 0.5f;
        p.vy[i] = -0.5f;
        p.vz[i] = 0.25f;
    }

    BenchmarkTimer timer = timer_start();

    // Timed simulation steps
    for (int step = 0; step < 200; step++) {
        for (int i = 0; i < 5000; i++) {
            p.vx[i] = p.vx[i] + 0.01f;
            p.vy[i] = p.vy[i] + 0.02f;
            p.vz[i] = p.vz[i] - 0.01f;

            p.x[i] = p.x[i] + p.vx[i] * 0.1f;
            p.y[i] = p.y[i] + p.vy[i] * 0.1f;
            p.z[i] = p.z[i] + p.vz[i] * 0.1f;
        }
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    float total_x = 0.0f;
    float total_y = 0.0f;
    float total_z = 0.0f;
    float total_v = 0.0f;
    for (int i = 0; i < 5000; i++) {
        total_x = total_x + p.x[i];
        total_y = total_y + p.y[i];
        total_z = total_z + p.z[i];
        total_v = total_v + p.vx[i] + p.vy[i] + p.vz[i];
    }

    int64_t checksum = (int64_t)total_x + (int64_t)total_y + (int64_t)total_z + (int64_t)total_v;
    printf("TIME_MS:%.4f;TOTAL_X:%lld;TOTAL_Y:%lld;TOTAL_Z:%lld;TOTAL_V:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)total_x, (long long)total_y, (long long)total_z, (long long)total_v, (long long)checksum);
    return 0;
}
