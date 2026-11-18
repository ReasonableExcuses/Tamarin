// 10_region_alloc.c - Memory Regions and Monotonic Arena Benchmark
// Tests monotonic bump allocation, nested regions, and bulk reclamation

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../common/timer.h"

typedef struct Particle {
    int64_t id;
    int64_t x;
    int64_t y;
    int64_t z;
} Particle;

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

static void* arena_alloc(Arena* a, size_t size, size_t align) {
    if (size == 0) return NULL;
    if (align == 0 || (align & (align - 1)) != 0) abort();
    Chunk* c = a->current;
    size_t mask = align - 1;
    if (c->offset + mask < c->offset) abort();
    size_t off = (c->offset + mask) & ~mask;
    if (off + size < off) abort();
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
    memset(ptr, 0, size);
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

    int64_t particle_sum = 0;
    int64_t scratch_sum = 0;
    int64_t nested_sum = 0;

    for (int iter = 0; iter < 5000; iter++) {
        Arena frame;
        arena_init(&frame, 65536);

        Particle* particles = (Particle*)arena_alloc(&frame, sizeof(Particle) * 100, 8);
        int32_t* scratch = (int32_t*)arena_alloc(&frame, sizeof(int32_t) * 200, 4);
        int32_t* growth = (int32_t*)arena_alloc(&frame, sizeof(int32_t) * 18000, 4);

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

        Arena subframe;
        arena_init(&subframe, 16384);
        int32_t* temp = (int32_t*)arena_alloc(&subframe, sizeof(int32_t) * 50, 4);
        for (int m = 0; m < 50; m++) {
            temp[m] = scratch[m] + (int32_t)particles[m].z;
            nested_sum += (int64_t)temp[m];
        }
        arena_destroy(&subframe);

        arena_destroy(&frame);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = particle_sum + scratch_sum + nested_sum;

    printf("TIME_MS:%.4f;PARTICLES:%lld;SCRATCH:%lld;NESTED:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)particle_sum, (long long)scratch_sum, (long long)nested_sum, (long long)checksum);
    return 0;
}

