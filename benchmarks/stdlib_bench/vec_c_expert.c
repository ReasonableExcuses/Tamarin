#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

typedef struct {
    int* __restrict__ data;
    size_t length;
    size_t capacity;
} IntVec;

static inline void vec_init_with_capacity(IntVec* v, size_t cap) {
    v->capacity = cap;
    v->length = 0;
    v->data = (int*)malloc(cap * sizeof(int));
}

static inline void vec_push(IntVec* __restrict__ v, int val) {
    v->data[v->length++] = val;
}

static inline int vec_pop(IntVec* __restrict__ v) {
    return v->data[--v->length];
}

static inline void vec_free(IntVec* v) {
    if (v->data) {
        free(v->data);
        v->data = NULL;
    }
}

int main() {
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    const int N = 10000000;
    IntVec v;
    vec_init_with_capacity(&v, N);

    // 1. Push
    for (int i = 0; i < N; ++i) {
        vec_push(&v, i * 3 + 1);
    }

    // 2. Sum
    long long sum = 0;
    int* __restrict__ ptr = v.data;
    size_t len = v.length;
    for (size_t i = 0; i < len; ++i) {
        sum += ptr[i];
    }

    // 3. Mutate
    for (size_t i = 0; i < len; ++i) {
        ptr[i] += 7;
    }

    // 4. Pop
    long long popSum = 0;
    for (int i = 0; i < 1000000; ++i) {
        popSum += vec_pop(&v);
    }

    vec_free(&v);

    QueryPerformanceCounter(&t1);
    double elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    printf("[C Expert]    N=%d | sum=%lld | popSum=%lld | time=%.2f ms\n", N, sum, popSum, elapsed_ms);
    return 0;
}
