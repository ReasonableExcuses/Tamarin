// 10d_empty_region.c - Pattern D: Pure creation and destruction of empty regions

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../common/timer.h"

typedef struct Chunk {
    struct Chunk* next;
    size_t capacity;
    size_t offset;
    char* data;
} Chunk;

typedef struct Arena {
    Chunk* head;
    Chunk* current;
} Arena;

static void arena_init(Arena* a, size_t cap) {
    if (cap == 0) cap = 65536;
    Chunk* c = (Chunk*)malloc(sizeof(Chunk));
    if (!c) abort();
    c->data = (char*)malloc(cap);
    if (!c->data) { free(c); abort(); }
    c->next = NULL;
    c->capacity = cap;
    c->offset = 0;
    a->head = c;
    a->current = c;
}

static void arena_destroy(Arena* a) {
    Chunk* c = a->head;
    while (c != NULL) {
        Chunk* next = c->next;
        free(c->data);
        free(c);
        c = next;
    }
    a->head = NULL;
    a->current = NULL;
}

int main(void) {
    BenchmarkTimer timer = timer_start();

    int64_t sum = 0;
    for (int iter = 0; iter < 50000; iter++) {
        Arena r;
        arena_init(&r, 65536);
        arena_destroy(&r);
        sum += 1;
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    printf("TIME_MS:%.4f;SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)sum, (long long)sum);
    return 0;
}

