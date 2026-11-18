#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define BLOCK_SIZE 64

static inline void iswap(int* a, int* b) {
    int t = *a; *a = *b; *b = t;
}

static inline int med3(int* arr, int a, int b, int c) {
    int va = arr[a], vb = arr[b], vc = arr[c];
    return va < vb ? (vb < vc ? b : (va < vc ? c : a)) : (vb > vc ? b : (va > vc ? c : a));
}

static void block_quicksort_c(int* arr, int low, int high) {
    while (high - low > 32) {
        int mid = low + (high - low) / 2;
        int pIdx = med3(arr, low, mid, high);
        int pivot = arr[pIdx];

        // Move pivot to end
        iswap(&arr[pIdx], &arr[high]);

        int l = low;
        int r = high - 1;

        int offsets_l[BLOCK_SIZE];
        int offsets_r[BLOCK_SIZE];

        while (r - l + 1 > 2 * BLOCK_SIZE) {
            int num_l = 0;
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                offsets_l[num_l] = i;
                num_l += (arr[l + i] >= pivot);
            }

            int num_r = 0;
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                offsets_r[num_r] = i;
                num_r += (arr[r - i] < pivot);
            }

            int num = num_l < num_r ? num_l : num_r;
            for (int i = 0; i < num; ++i) {
                iswap(&arr[l + offsets_l[i]], &arr[r - offsets_r[i]]);
            }

            if (num_l == num) {
                l += BLOCK_SIZE;
            }
            if (num_r == num) {
                r -= BLOCK_SIZE;
            }
        }

        // Remainder partition
        while (l <= r) {
            while (l <= r && arr[l] < pivot) ++l;
            while (l <= r && arr[r] >= pivot) --r;
            if (l < r) {
                iswap(&arr[l], &arr[r]);
                ++l;
                --r;
            }
        }

        // Put pivot back
        iswap(&arr[l], &arr[high]);

        if (l - 1 - low < high - (l + 1)) {
            if (low < l - 1) block_quicksort_c(arr, low, l - 1);
            low = l + 1;
        } else {
            if (l + 1 < high) block_quicksort_c(arr, l + 1, high);
            high = l - 1;
        }
    }

    // Insertion sort for remaining
    for (int i = low + 1; i <= high; ++i) {
        int key = arr[i];
        int j = i - 1;
        while (j >= low && arr[j] > key) {
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
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

    block_quicksort_c(data, 0, N - 1);

    QueryPerformanceCounter(&t1);
    double elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    int ok = 1;
    for (int i = 0; i < N - 1; ++i) {
        if (data[i] > data[i + 1]) { ok = 0; break; }
    }

    printf("[Block Quicksort] N=%d | sorted=%s | min=%d | max=%d | time=%.2f ms\n",
           N, ok ? "YES" : "NO", data[0], data[N - 1], elapsed_ms);

    free(data);
    return 0;
}
