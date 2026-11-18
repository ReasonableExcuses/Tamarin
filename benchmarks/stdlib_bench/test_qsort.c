#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static inline void iswap(int* a, int* b) {
    int t = *a; *a = *b; *b = t;
}

static inline int med3(int* arr, int a, int b, int c) {
    return arr[a] < arr[b] ?
        (arr[b] < arr[c] ? b : (arr[a] < arr[c] ? c : a)) :
        (arr[b] > arr[c] ? b : (arr[a] > arr[c] ? c : a));
}

static void introsort_c(int* arr, int low, int high) {
    while (high - low > 24) {
        int mid = low + (high - low) / 2;
        int pIdx;
        int len = high - low;
        if (len > 1000) {
            int s = len / 8;
            int m1 = med3(arr, low, low + s, low + 2 * s);
            int m2 = med3(arr, mid - s, mid, mid + s);
            int m3 = med3(arr, high - 2 * s, high - s, high);
            pIdx = med3(arr, m1, m2, m3);
        } else {
            pIdx = med3(arr, low, mid, high);
        }

        int pivot = arr[pIdx];
        int i = low;
        int j = high;

        while (i <= j) {
            while (arr[i] < pivot) ++i;
            while (arr[j] > pivot) --j;
            if (i <= j) {
                iswap(&arr[i], &arr[j]);
                ++i;
                --j;
            }
        }

        if (j - low < high - i) {
            if (low < j) introsort_c(arr, low, j);
            low = i;
        } else {
            if (i < high) introsort_c(arr, i, high);
            high = j;
        }
    }
}

static void final_insertion_sort(int* arr, int n) {
    for (int i = 1; i < n; ++i) {
        int key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }
}

static void fast_sort(int* arr, int n) {
    if (n > 1) {
        introsort_c(arr, 0, n - 1);
        final_insertion_sort(arr, n);
    }
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

    fast_sort(data, N);

    QueryPerformanceCounter(&t1);
    double elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    int ok = 1;
    for (int i = 0; i < N - 1; ++i) {
        if (data[i] > data[i + 1]) { ok = 0; break; }
    }

    printf("[Fast Sort] N=%d | sorted=%s | time=%.2f ms\n", N, ok ? "YES" : "NO", elapsed_ms);
    free(data);
    return 0;
}
