#ifndef REUSABLE_ARENA_H
#define REUSABLE_ARENA_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct Chunk {
    struct Chunk* next;
    size_t capacity;
    size_t offset;
    char* data;
} Chunk;

typedef struct ReusableArena {
    Chunk* head;
    Chunk* current;
} ReusableArena;

#define ARENA_POOL_CAP 131072

#if defined(_MSC_VER) && !defined(__clang__)
static __declspec(thread) Chunk* tls_pooled_chunk = NULL;
#else
static __thread Chunk* tls_pooled_chunk = NULL;
#endif

static inline void arena_pool_init(ReusableArena* a, size_t cap) {
    if (cap == 0) cap = 65536;
    if (tls_pooled_chunk != NULL && tls_pooled_chunk->capacity >= cap) {
        a->head = tls_pooled_chunk;
        a->current = tls_pooled_chunk;
        tls_pooled_chunk = NULL;
        a->head->offset = 0;
        return;
    }
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

static inline void* arena_pool_alloc(ReusableArena* a, size_t size, size_t align) {
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
    memset(ptr, 0, size);
    c->offset = off + size;
    return ptr;
}

static inline void arena_pool_destroy(ReusableArena* a) {
    if (!a->head) return;
    Chunk* first = a->head;
    Chunk* extra = first->next;
    while (extra != NULL) {
        Chunk* next = extra->next;
        free(extra->data);
        free(extra);
        extra = next;
    }
    first->next = NULL;
    first->offset = 0;

    if (tls_pooled_chunk == NULL && first->capacity <= ARENA_POOL_CAP) {
        tls_pooled_chunk = first;
    } else {
        free(first->data);
        free(first);
    }
    a->head = NULL;
    a->current = NULL;
}

#endif

