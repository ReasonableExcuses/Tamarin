#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static int cmp_int(const void* a, const void* b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return (ia > ib) - (ia < ib);
}

int main() {
    const int N = 2000000;
    int* data = (int*)malloc(N * sizeof(int));
    unsigned int seed = 123456789;
    for (int i = 0; i < N; ++i) {
        seed = seed * 1664525 + 1013904223;
        data[i] = (int)(seed & 0x7FFFFFFF);
    }

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    qsort(data, N, sizeof(int), cmp_int);

    QueryPerformanceCounter(&t1);
    double elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    // Verify sort
    int ok = 1;
    for (int i = 0; i < N - 1; ++i) {
        if (data[i] > data[i + 1]) { ok = 0; break; }
    }

    printf("[C qsort]      N=%d | sorted=%s | min=%d | max=%d | time=%.2f ms\n",
           N, ok ? "YES" : "NO", data[0], data[N - 1], elapsed_ms);

    free(data);
    return 0;
}
