#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static void quicksort_c(int* arr, int low, int high) {
    if (low >= high) return;

    if (high - low <= 10) {
        for (int i = low + 1; i <= high; ++i) {
            int key = arr[i];
            int j = i - 1;
            while (j >= low && arr[j] > key) {
                arr[j + 1] = arr[j];
                j = j - 1;
            }
            arr[j + 1] = key;
        }
        return;
    }

    int mid = low + (high - low) / 2;
    if (arr[low] > arr[mid]) { int t = arr[low]; arr[low] = arr[mid]; arr[mid] = t; }
    if (arr[low] > arr[high]) { int t = arr[low]; arr[low] = arr[high]; arr[high] = t; }
    if (arr[mid] > arr[high]) { int t = arr[mid]; arr[mid] = arr[high]; arr[high] = t; }

    int pivot = arr[mid];
    int i = low;
    int j = high;

    while (i <= j) {
        while (arr[i] < pivot) ++i;
        while (arr[j] > pivot) --j;
        if (i <= j) {
            int tmp = arr[i];
            arr[i] = arr[j];
            arr[j] = tmp;
            ++i;
            --j;
        }
    }

    if (low < j) quicksort_c(arr, low, j);
    if (i < high) quicksort_c(arr, i, high);
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

    quicksort_c(data, 0, N - 1);

    QueryPerformanceCounter(&t1);
    double elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    int ok = 1;
    for (int i = 0; i < N - 1; ++i) {
        if (data[i] > data[i + 1]) { ok = 0; break; }
    }

    printf("[C Expert QSort] N=%d | sorted=%s | min=%d | max=%d | time=%.2f ms\n",
           N, ok ? "YES" : "NO", data[0], data[N - 1], elapsed_ms);

    free(data);
    return 0;
}
