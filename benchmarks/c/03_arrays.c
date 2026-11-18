// 03_arrays.c - Arrays and Bounds Checking Benchmark
// Tests sequential scan, random lookup, copy, and transform

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

int main(void) {
    int arr[10000];
    int buf[10000];

    // Initialize outside timed section
    for (int i = 0; i < 10000; i++) {
        arr[i] = i & 255;
        buf[i] = 0;
    }

    BenchmarkTimer timer = timer_start();

    // 1. Sequential scan and accumulation (500 passes)
    int64_t scan_sum = 0;
    for (int pass = 0; pass < 500; pass++) {
        for (int i = 0; i < 10000; i++) {
            scan_sum += (int64_t)arr[i];
        }
    }

    // 2. Pseudo-random access and in-place updates (1,000,000 updates)
    int64_t lcg = 123456789LL;
    for (int step = 0; step < 1000000; step++) {
        lcg = (lcg * 1103515245LL + 12345LL) & 2147483647LL;
        int idx = (int)(lcg % 10000);
        arr[idx] = (arr[idx] + (step & 255)) & 65535;
    }

    // 3. Array copy, transform, and element sum
    int64_t transform_sum = 0;
    for (int pass = 0; pass < 200; pass++) {
        for (int i = 0; i < 10000; i++) {
            buf[i] = arr[i] * 2 + 1;
            transform_sum += (int64_t)(buf[i] & 255);
        }
    }

    int64_t final_arr_sum = 0;
    for (int i = 0; i < 10000; i++) {
        final_arr_sum += (int64_t)arr[i];
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = scan_sum + transform_sum + final_arr_sum;
    printf("TIME_MS:%.4f;SCAN:%lld;TRANSFORM:%lld;FINAL:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)scan_sum, (long long)transform_sum, (long long)final_arr_sum, (long long)checksum);
    return 0;
}
