// 10f_reuse_expert.c - Expert Pattern F: Sequential region reuse under bounded cache

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"
#include "reusable_arena.h"

int main(void) {
    BenchmarkTimer timer = timer_start();

    int64_t sum = 0;
    for (int iter = 0; iter < 1000; iter++) {
        ReusableArena r;
        arena_pool_init(&r, 4096);
        int64_t* data = (int64_t*)arena_pool_alloc(&r, sizeof(int64_t) * 100, 8);
        for (int i = 0; i < 100; i++) {
            data[i] = (int64_t)(iter * 100 + i);
            sum += data[i];
        }
        arena_pool_destroy(&r);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    printf("TIME_MS:%.4f;SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)sum, (long long)sum);
    return 0;
}

