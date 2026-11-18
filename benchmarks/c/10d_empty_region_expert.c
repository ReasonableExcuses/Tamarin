// 10d_empty_region_expert.c - Expert Pattern D: Pure creation and destruction of empty regions

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"
#include "reusable_arena.h"

int main(void) {
    BenchmarkTimer timer = timer_start();

    int64_t sum = 0;
    for (int iter = 0; iter < 50000; iter++) {
        ReusableArena r;
        arena_pool_init(&r, 65536);
        sum += 1;
        arena_pool_destroy(&r);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    printf("TIME_MS:%.4f;SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)sum, (long long)sum);
    return 0;
}

