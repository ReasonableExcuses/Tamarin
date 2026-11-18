// 10c_chunk_growth_expert.c - Expert Pattern C: Chunk growth under sustained allocation pressure

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"
#include "reusable_arena.h"

int main(void) {
    BenchmarkTimer timer = timer_start();

    int64_t sum = 0;
    for (int iter = 0; iter < 50; iter++) {
        ReusableArena r;
        arena_pool_init(&r, 65536);
        int64_t* data = (int64_t*)arena_pool_alloc(&r, sizeof(int64_t) * 25000, 8);
        for (int j = 0; j < 25000; j++) {
            data[j] = (int64_t)(iter * 25000 + j);
            sum += data[j];
        }
        arena_pool_destroy(&r);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    printf("TIME_MS:%.4f;SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)sum, (long long)sum);
    return 0;
}

