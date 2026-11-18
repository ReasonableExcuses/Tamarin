// 10a_high_churn_expert.c - Expert Pattern A: High-churn short-lived region (50,000 small iterations)
// Genuine reusable thread-local arena pool: 0 malloc/free in steady state

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"
#include "reusable_arena.h"

int main(void) {
    BenchmarkTimer timer = timer_start();

    int64_t sum = 0;
    for (int iter = 0; iter < 50000; iter++) {
        ReusableArena r;
        arena_pool_init(&r, 4096);
        int64_t* values = (int64_t*)arena_pool_alloc(&r, sizeof(int64_t) * 16, 8);
        for (int i = 0; i < 16; i++) {
            values[i] = (int64_t)(iter * 16 + i);
            sum += values[i];
        }
        arena_pool_destroy(&r);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    printf("TIME_MS:%.4f;SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)sum, (long long)sum);
    return 0;
}

