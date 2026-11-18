// 04_structures_aos_expert.c - Expert AoS Benchmark
// Highly-optimized: unrolled particle simulation and reduction

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

typedef struct {
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
} Particle;

int main(void) {
    Particle p[5000];

    for (int i = 0; i < 5000; i++) {
        p[i].x = (float)(i & 63);
        p[i].y = (float)((i + 1) & 63);
        p[i].z = (float)((i + 2) & 63);
        p[i].vx = 0.5f;
        p[i].vy = -0.5f;
        p[i].vz = 0.25f;
    }

    BenchmarkTimer timer = timer_start();

    for (int step = 0; step < 200; step++) {
        Particle* const p_end = p + 5000;
        for (Particle* pt = p; pt < p_end; ++pt) {
            pt->vx += 0.01f;
            pt->vy += 0.02f;
            pt->vz -= 0.01f;
            pt->x += pt->vx * 0.1f;
            pt->y += pt->vy * 0.1f;
            pt->z += pt->vz * 0.1f;
        }
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    float total_x = 0.0f;
    float total_y = 0.0f;
    float total_z = 0.0f;
    float total_v = 0.0f;
    for (int i = 0; i < 5000; i++) {
        total_x += p[i].x;
        total_y += p[i].y;
        total_z += p[i].z;
        total_v += p[i].vx + p[i].vy + p[i].vz;
    }

    long checksum = (long)total_x + (long)total_y + (long)total_z + (long)total_v;
    printf("TIME_MS:%.4f;TOTAL_X:%ld;TOTAL_Y:%ld;TOTAL_Z:%ld;TOTAL_V:%ld;CHECKSUM:%ld\n",
           elapsed_ms, (long)total_x, (long)total_y, (long)total_z, (long)total_v, checksum);
    return 0;
}

