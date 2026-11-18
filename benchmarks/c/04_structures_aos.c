// 04_structures_aos.c - AoS (Array of Structures) Benchmark

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

    // Initialize outside timed section
    for (int i = 0; i < 5000; i++) {
        p[i].x = (float)(i & 63);
        p[i].y = (float)((i + 1) & 63);
        p[i].z = (float)((i + 2) & 63);
        p[i].vx = 0.5f;
        p[i].vy = -0.5f;
        p[i].vz = 0.25f;
    }

    BenchmarkTimer timer = timer_start();

    // Timed simulation steps
    for (int step = 0; step < 200; step++) {
        for (int i = 0; i < 5000; i++) {
            p[i].vx = p[i].vx + 0.01f;
            p[i].vy = p[i].vy + 0.02f;
            p[i].vz = p[i].vz - 0.01f;

            p[i].x = p[i].x + p[i].vx * 0.1f;
            p[i].y = p[i].y + p[i].vy * 0.1f;
            p[i].z = p[i].z + p[i].vz * 0.1f;
        }
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    float total_x = 0.0f;
    float total_y = 0.0f;
    float total_z = 0.0f;
    float total_v = 0.0f;
    for (int i = 0; i < 5000; i++) {
        total_x = total_x + p[i].x;
        total_y = total_y + p[i].y;
        total_z = total_z + p[i].z;
        total_v = total_v + p[i].vx + p[i].vy + p[i].vz;
    }

    int64_t checksum = (int64_t)total_x + (int64_t)total_y + (int64_t)total_z + (int64_t)total_v;
    printf("TIME_MS:%.4f;TOTAL_X:%lld;TOTAL_Y:%lld;TOTAL_Z:%lld;TOTAL_V:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)total_x, (long long)total_y, (long long)total_z, (long long)total_v, (long long)checksum);
    return 0;
}
