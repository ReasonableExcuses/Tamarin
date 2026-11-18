// 03_arrays_expert.c - Expert Arrays and Bounds Checking Benchmark
// Highly-optimized: unrolled sequential scan, pipelined LCG updates, and vectorized transforms

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

int main(void) {
    int arr[10000];
    int buf[10000];

    for (int i = 0; i < 10000; i++) {
        arr[i] = i & 255;
    }

    BenchmarkTimer timer = timer_start();

    // 1. Sequential scan and accumulation (500 passes) - unrolled 8x
    int64_t scan_sum = 0;
    for (int pass = 0; pass < 500; pass++) {
        int64_t pass_acc0 = 0, pass_acc1 = 0;
        for (int i = 0; i < 10000; i += 8) {
            pass_acc0 += (int64_t)arr[i] + (int64_t)arr[i + 1] + (int64_t)arr[i + 2] + (int64_t)arr[i + 3];
            pass_acc1 += (int64_t)arr[i + 4] + (int64_t)arr[i + 5] + (int64_t)arr[i + 6] + (int64_t)arr[i + 7];
        }
        scan_sum += pass_acc0 + pass_acc1;
    }

    // 2. Pseudo-random access and in-place updates (1,000,000 updates) - pipelined 4x
    int64_t lcg = 123456789;
    for (int step = 0; step < 1000000; step += 4) {
        // Step 0
        lcg = (lcg * 1103515245 + 12345) & 2147483647;
        int idx0 = (int)(lcg % 10000);
        arr[idx0] = (arr[idx0] + (step & 255)) & 65535;

        // Step 1
        lcg = (lcg * 1103515245 + 12345) & 2147483647;
        int idx1 = (int)(lcg % 10000);
        arr[idx1] = (arr[idx1] + ((step + 1) & 255)) & 65535;

        // Step 2
        lcg = (lcg * 1103515245 + 12345) & 2147483647;
        int idx2 = (int)(lcg % 10000);
        arr[idx2] = (arr[idx2] + ((step + 2) & 255)) & 65535;

        // Step 3
        lcg = (lcg * 1103515245 + 12345) & 2147483647;
        int idx3 = (int)(lcg % 10000);
        arr[idx3] = (arr[idx3] + ((step + 3) & 255)) & 65535;
    }

    // 3. Array copy, transform, and element sum (200 passes) - unrolled 4x
    int64_t transform_sum = 0;
    for (int pass = 0; pass < 200; pass++) {
        int64_t tacc = 0;
        for (int i = 0; i < 10000; i += 4) {
            int b0 = arr[i] * 2 + 1;
            int b1 = arr[i + 1] * 2 + 1;
            int b2 = arr[i + 2] * 2 + 1;
            int b3 = arr[i + 3] * 2 + 1;
            buf[i] = b0;
            buf[i + 1] = b1;
            buf[i + 2] = b2;
            buf[i + 3] = b3;
            tacc += (int64_t)(b0 & 255) + (int64_t)(b1 & 255) + (int64_t)(b2 & 255) + (int64_t)(b3 & 255);
        }
        transform_sum += tacc;
    }

    int64_t final_arr_sum = 0;
    for (int i = 0; i < 10000; i += 4) {
        final_arr_sum += (int64_t)arr[i] + (int64_t)arr[i + 1] + (int64_t)arr[i + 2] + (int64_t)arr[i + 3];
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = scan_sum + transform_sum + final_arr_sum;
    printf("TIME_MS:%.4f;SCAN:%lld;TRANSFORM:%lld;FINAL:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)scan_sum, (long long)transform_sum,
           (long long)final_arr_sum, (long long)checksum);
    return 0;
}

