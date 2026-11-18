// 10b_large_single_expert.c - Expert Pattern B: Large single region with many allocations

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"
#include "reusable_arena.h"

int main(void) {
    BenchmarkTimer timer = timer_start();

    int64_t sum = 0;
    ReusableArena r;
    arena_pool_init(&r, 1048576);

    for (int alloc_idx = 0; alloc_idx < 10000; alloc_idx++) {
        int64_t* buf = (int64_t*)arena_pool_alloc(&r, sizeof(int64_t) * 8, 8);
        for (int j = 0; j < 8; j++) {
            buf[j] = (int64_t)(alloc_idx * 8 + j);
            sum += buf[j];
        }
    }

    arena_pool_destroy(&r);

    double elapsed_ms = timer_elapsed_ms(timer);

    printf("TIME_MS:%.4f;SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)sum, (long long)sum);
    return 0;
}

