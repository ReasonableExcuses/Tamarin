#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct {
    char* __restrict__ data;
    size_t length;
    size_t capacity;
} StrBuf;

static inline void str_init_with_capacity(StrBuf* s, size_t cap) {
    s->capacity = cap;
    s->length = 0;
    s->data = (char*)malloc(cap);
    s->data[0] = 0;
}

static inline void str_append_fast(StrBuf* __restrict__ s, const char* __restrict__ text, size_t tlen) {
    memcpy(s->data + s->length, text, tlen);
    s->length += tlen;
}

static inline void str_free(StrBuf* s) {
    if (s->data) {
        free(s->data);
        s->data = NULL;
    }
}

static inline int str_to_int_fast(const char* __restrict__ s) {
    int res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        ++s;
    }
    return res;
}

int main() {
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    const int N = 500000;
    StrBuf s;
    str_init_with_capacity(&s, N * 8 + 16);

    // 1. Fast Append
    for (int i = 0; i < N; ++i) {
        str_append_fast(&s, "tamarin_", 8);
    }
    s.data[s.length] = 0;

    // 2. Starts with & Ends with
    int sw = (memcmp(s.data, "tamarin_", 8) == 0);
    int ew = (memcmp(s.data + s.length - 8, "tamarin_", 8) == 0);

    // 3. String to int conversions
    long long numSum = 0;
    const char* numStr = "12345678";
    for (int i = 0; i < 1000000; ++i) {
        numSum += str_to_int_fast(numStr);
    }

    size_t totalLen = s.length;
    str_free(&s);

    QueryPerformanceCounter(&t1);
    double elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    printf("[C Expert String] N=%d | len=%zu | sw=%d | ew=%d | numSum=%lld | time=%.2f ms\n",
           N, totalLen, sw, ew, numSum, elapsed_ms);
    return 0;
}
