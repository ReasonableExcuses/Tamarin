// 10a_high_churn.c - Pattern A: High-churn short-lived region (50,000 small iterations)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
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
    if (cap == 0) cap = 4096;
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

static void* arena_alloc(Arena* a, size_t size, size_t align) {
    if (size == 0) return NULL;
    Chunk* c = a->current;
    size_t mask = align - 1;
    size_t off = (c->offset + mask) & ~mask;
    if (off + size > c->capacity) {
        size_t new_cap = c->capacity * 2;
        if (new_cap < size + align) new_cap = size + align + 64;
        Chunk* next = (Chunk*)malloc(sizeof(Chunk));
        if (!next) abort();
        next->data = (char*)malloc(new_cap);
        if (!next->data) { free(next); abort(); }
        next->next = NULL;
        next->capacity = new_cap;
        next->offset = 0;
        c->next = next;
        a->current = next;
        c = next;
        off = 0;
    }
    void* ptr = c->data + off;
    c->offset = off + size;
    return ptr;
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
        arena_init(&r, 4096);
        int64_t* values = (int64_t*)arena_alloc(&r, sizeof(int64_t) * 16, 8);
        for (int i = 0; i < 16; i++) {
            values[i] = (int64_t)(iter * 16 + i);
            sum += values[i];
        }
        arena_destroy(&r);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    printf("TIME_MS:%.4f;SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)sum, (long long)sum);
    return 0;
}

