// 05_function_calls_expert.c - Expert Function Calls Benchmark
// Highly-optimized: tail-recursion unrolled Fibonacci and pipelined transform

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

static inline int fib_expert(int n) {
    int sum = 0;
    while (n > 1) {
        sum += fib_expert(n - 1);
        n -= 2;
    }
    return sum + (n > 0 ? n : 0);
}

static inline uint64_t transform_expert(uint64_t x, uint64_t y) {
    return (x * 3 + y * 7 + 1) ^ (x >> 3);
}

int main(void) {
    BenchmarkTimer timer = timer_start();

    int f = fib_expert(34);

    uint64_t acc = 12345;
    for (uint64_t i = 0; i < 5000000; i += 4) {
        acc = transform_expert(acc, i);
        acc = transform_expert(acc, i + 1);
        acc = transform_expert(acc, i + 2);
        acc = transform_expert(acc, i + 3);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    uint64_t acc_masked = acc & 2147483647ULL;
    int64_t checksum = (int64_t)f + (int64_t)acc_masked;

    printf("TIME_MS:%.4f;FIB:%d;ACC:%llu;CHECKSUM:%lld\n",
           elapsed_ms, f, (unsigned long long)acc_masked, (long long)checksum);
    return 0;
}

