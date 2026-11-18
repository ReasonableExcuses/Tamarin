#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static void radix_sort_u32(unsigned int* arr, unsigned int* temp, int n) {
    // 4 passes of 8-bit radix
    for (int shift = 0; shift < 32; shift += 8) {
        int count[256] = {0};
        for (int i = 0; i < n; ++i) {
            unsigned char b = (arr[i] >> shift) & 0xFF;
            count[b]++;
        }

        int offset[256];
        offset[0] = 0;
        for (int i = 1; i < 256; ++i) {
            offset[i] = offset[i - 1] + count[i - 1];
        }

        for (int i = 0; i < n; ++i) {
            unsigned char b = (arr[i] >> shift) & 0xFF;
            temp[offset[b]++] = arr[i];
        }

        // Swap arr and temp pointers
        unsigned int* t = arr; arr = temp; temp = t;
    }
}

int main() {
    const int N = 2000000;
    unsigned int* data = (unsigned int*)malloc(N * sizeof(unsigned int));
    unsigned int* temp = (unsigned int*)malloc(N * sizeof(unsigned int));
    unsigned int seed = 123456789;
    for (int i = 0; i < N; ++i) {
        seed = seed * 1664525 + 1013904223;
        data[i] = (seed & 0x7FFFFFFF);
    }

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    radix_sort_u32(data, temp, N);

    QueryPerformanceCounter(&t1);
    double elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    int ok = 1;
    for (int i = 0; i < N - 1; ++i) {
        if (data[i] > data[i + 1]) { ok = 0; break; }
    }

    printf("[Radix Sort] N=%d | sorted=%s | min=%u | max=%u | time=%.2f ms\n",
           N, ok ? "YES" : "NO", data[0], data[N - 1], elapsed_ms);

    free(data);
    free(temp);
    return 0;
}
