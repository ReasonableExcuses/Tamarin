// 06_branching.c - Control Flow and Branching Benchmark
// Tests predictable vs unpredictable branch prediction performance

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

int main(void) {
    BenchmarkTimer timer = timer_start();

    // 1. Predictable branching pattern (5,000,000 iterations)
    int64_t pred_sum = 0;
    for (int i = 0; i < 5000000; i++) {
        int rem = i % 4;
        if (rem == 0) {
            pred_sum += (int64_t)(i * 3);
        } else if (rem == 1) {
            pred_sum += (int64_t)(i * 5);
        } else if (rem == 2) {
            pred_sum -= (int64_t)(i * 2);
        } else {
            pred_sum += (int64_t)i;
        }
    }

    // 2. Unpredictable branching pattern (5,000,000 iterations via LCG)
    int64_t unpred_sum = 0;
    int64_t lcg = 2147483647LL;
    for (int i = 0; i < 5000000; i++) {
        lcg = (lcg * 1664525LL + 1013904223LL) & 2147483647LL;
        if ((lcg & 1) == 0) {
            unpred_sum += (lcg >> 16);
        } else {
            unpred_sum -= (lcg >> 16);
        }
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = pred_sum + unpred_sum;
    printf("TIME_MS:%.4f;PRED:%lld;UNPRED:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)pred_sum, (long long)unpred_sum, (long long)checksum);
    return 0;
}
