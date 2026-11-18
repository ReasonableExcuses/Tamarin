// 06_branching_expert.c - Expert Control Flow and Branching Benchmark
// Highly-optimized: unrolled branchless predictable loop and branchless arithmetic for LCG

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

int main(void) {
    BenchmarkTimer timer = timer_start();

    // 1. Predictable branching pattern (5,000,000 iterations) - unrolled 4x branchless
    int64_t pred_sum = 0;
    for (int64_t i = 0; i < 5000000; i += 4) {
        pred_sum += (i * 3);
        pred_sum += ((i + 1) * 5);
        pred_sum -= ((i + 2) * 2);
        pred_sum += (i + 3);
    }

    // 2. Unpredictable branching pattern (5,000,000 iterations) - branchless 2's complement negation
    int64_t unpred_sum = 0;
    int64_t lcg = 2147483647;
    for (int i = 0; i < 5000000; i++) {
        lcg = (lcg * 1664525 + 1013904223) & 2147483647;
        int64_t val = (lcg >> 16);
        int64_t mask = -((int64_t)(lcg & 1)); // 0 if even, -1 if odd
        unpred_sum += (val ^ mask) - mask;
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = pred_sum + unpred_sum;
    printf("TIME_MS:%.4f;PRED:%lld;UNPRED:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)pred_sum, (long long)unpred_sum, (long long)checksum);
    return 0;
}

