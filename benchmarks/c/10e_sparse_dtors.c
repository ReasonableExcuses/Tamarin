// 10e_sparse_dtors.c - Pattern E: Regions with sparse destructor sidecars

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../common/timer.h"

typedef struct DtorEntry {
    void (*dtor)(void*);
    void* obj;
} DtorEntry;

typedef struct Chunk {
    struct Chunk* next;
    size_t capacity;
    size_t offset;
    char* data;
} Chunk;

typedef struct Arena {
    Chunk* head;
    Chunk* current;
    DtorEntry* dtors;
    size_t dtor_count;
    size_t dtor_cap;
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
    a->dtors = NULL;
    a->dtor_count = 0;
    a->dtor_cap = 0;
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

static void arena_register_dtor(Arena* a, void (*dtor)(void*), void* obj) {
    if (a->dtor_count >= a->dtor_cap) {
        size_t new_cap = a->dtor_cap == 0 ? 4 : a->dtor_cap * 2;
        DtorEntry* new_dtors = (DtorEntry*)realloc(a->dtors, new_cap * sizeof(DtorEntry));
        if (!new_dtors) abort();
        a->dtors = new_dtors;
        a->dtor_cap = new_cap;
    }
    a->dtors[a->dtor_count].dtor = dtor;
    a->dtors[a->dtor_count].obj = obj;
    a->dtor_count++;
}

static void arena_destroy(Arena* a) {
    for (size_t i = a->dtor_count; i > 0; i--) {
        a->dtors[i - 1].dtor(a->dtors[i - 1].obj);
    }
    if (a->dtors) free(a->dtors);

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
        Arena r;
        arena_init(&r, 65536);
        for (int i = 0; i < 4; i++) {
            Token* t = (Token*)arena_alloc(&r, sizeof(Token), 8);
            t->id = (int64_t)(iter * 4 + i);
            arena_register_dtor(&r, token_dtor, t);
            sum += t->id;
        }
        arena_destroy(&r);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    printf("TIME_MS:%.4f;SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)sum, (long long)sum);
    return 0;
}

