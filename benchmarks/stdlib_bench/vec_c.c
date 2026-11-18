#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

typedef struct {
    int* data;
    size_t length;
    size_t capacity;
} IntVec;

static void vec_init(IntVec* v) {
    v->capacity = 4;
    v->length = 0;
    v->data = (int*)malloc(v->capacity * sizeof(int));
}

static void vec_push(IntVec* v, int val) {
    if (v->length >= v->capacity) {
        v->capacity = v->capacity * 2;
        v->data = (int*)realloc(v->data, v->capacity * sizeof(int));
    }
    v->data[v->length++] = val;
}

static int vec_pop(IntVec* v) {
    return v->data[--v->length];
}

static void vec_free(IntVec* v) {
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
    vec_init(&v);

    // 1. Push
    for (int i = 0; i < N; ++i) {
        vec_push(&v, i * 3 + 1);
    }

    // 2. Sum
    long long sum = 0;
    for (size_t i = 0; i < v.length; ++i) {
        sum += v.data[i];
    }

    // 3. Mutate
    for (size_t i = 0; i < v.length; ++i) {
        v.data[i] += 7;
    }

    // 4. Pop
    long long popSum = 0;
    for (int i = 0; i < 1000000; ++i) {
        popSum += vec_pop(&v);
    }

    vec_free(&v);

    QueryPerformanceCounter(&t1);
    double elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    printf("[C Idiomatic] N=%d | sum=%lld | popSum=%lld | time=%.2f ms\n", N, sum, popSum, elapsed_ms);
    return 0;
}
