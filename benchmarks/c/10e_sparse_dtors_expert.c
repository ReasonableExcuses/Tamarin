// 10e_sparse_dtors_expert.c - Expert Pattern E: Regions with sparse destructor sidecars

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"
#include "reusable_arena.h"

typedef struct Token {
    int64_t id;
} Token;

static void token_dtor(void* obj) {
    Token* t = (Token*)obj;
    *(volatile int64_t*)&t->id = 0 - t->id;
}

int main(void) {
    BenchmarkTimer timer = timer_start();

    int64_t sum = 0;
    for (int iter = 0; iter < 1000; iter++) {
        ReusableArena r;
        arena_pool_init(&r, 4096);

        Token* t0 = (Token*)arena_pool_alloc(&r, sizeof(Token), 8);
        Token* t1 = (Token*)arena_pool_alloc(&r, sizeof(Token), 8);
        Token* t2 = (Token*)arena_pool_alloc(&r, sizeof(Token), 8);
        Token* t3 = (Token*)arena_pool_alloc(&r, sizeof(Token), 8);

        t0->id = (int64_t)(iter * 4 + 0);
        t1->id = (int64_t)(iter * 4 + 1);
        t2->id = (int64_t)(iter * 4 + 2);
        t3->id = (int64_t)(iter * 4 + 3);

        sum += t0->id + t1->id + t2->id + t3->id;

        token_dtor(t3);
        token_dtor(t2);
        token_dtor(t1);
        token_dtor(t0);

        arena_pool_destroy(&r);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    printf("TIME_MS:%.4f;SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)sum, (long long)sum);
    return 0;
}

