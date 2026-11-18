// 10_region_alloc_expert.c - Expert Memory Regions and Monotonic Arena Benchmark
// Uses genuine reusable thread-local arena pool

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"
#include "reusable_arena.h"

typedef struct Particle {
    int64_t id;
    int64_t x;
    int64_t y;
    int64_t z;
} Particle;

int main(void) {
    BenchmarkTimer timer = timer_start();

    int64_t particle_sum = 0;
    int64_t scratch_sum = 0;
    int64_t nested_sum = 0;

    for (int iter = 0; iter < 5000; iter++) {
        ReusableArena frame;
        arena_pool_init(&frame, 65536);

        Particle* particles = (Particle*)arena_pool_alloc(&frame, sizeof(Particle) * 100, 8);
        int32_t* scratch = (int32_t*)arena_pool_alloc(&frame, sizeof(int32_t) * 200, 4);
        int32_t* growth = (int32_t*)arena_pool_alloc(&frame, sizeof(int32_t) * 18000, 4);

        for (int i = 0; i < 100; i++) {
            particles[i].id = (int64_t)(iter * 100 + i);
            particles[i].x = (int64_t)(i * 3 + iter);
            particles[i].y = (int64_t)(i * 7 + iter * 2);
            particles[i].z = (int64_t)(i * 11 + iter * 3);
        }

        for (int j = 0; j < 100; j++) {
            scratch[j] = (int32_t)(particles[j].x + particles[j].y);
            scratch[j + 100] = (int32_t)(particles[j].z - particles[j].x);
        }

        growth[0] = scratch[0] + (int32_t)particles[0].x;
        growth[17999] = scratch[199] + (int32_t)particles[99].z;

        for (int k = 0; k < 200; k++) {
            scratch_sum += (int64_t)scratch[k];
        }
        scratch_sum += (int64_t)(growth[0] + growth[17999]);

        for (int p = 0; p < 100; p++) {
            particle_sum += particles[p].id;
        }

        ReusableArena subframe;
        arena_pool_init(&subframe, 16384);
        int32_t* temp = (int32_t*)arena_pool_alloc(&subframe, sizeof(int32_t) * 50, 4);
        for (int m = 0; m < 50; m++) {
            temp[m] = scratch[m] + (int32_t)particles[m].z;
            nested_sum += (int64_t)temp[m];
        }
        arena_pool_destroy(&subframe);

        arena_pool_destroy(&frame);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = particle_sum + scratch_sum + nested_sum;
    printf("TIME_MS:%.4f;PARTICLES:%lld;SCRATCH:%lld;NESTED:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)particle_sum, (long long)scratch_sum,
           (long long)nested_sum, (long long)checksum);
    return 0;
}

