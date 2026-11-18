// 05_function_calls.c - Function Calls and Inlining Benchmark

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

static int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

static uint64_t transform(uint64_t x, uint64_t y) {
    return (x * 3ULL + y * 7ULL + 1ULL) ^ (x >> 3);
}

int main(void) {
    BenchmarkTimer timer = timer_start();

    int f = fib(34);

    uint64_t acc = 12345ULL;
    for (int i = 0; i < 5000000; i++) {
        acc = transform(acc, (uint64_t)i);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    uint64_t acc_masked = acc & 2147483647ULL;
    int64_t checksum = (int64_t)f + (int64_t)acc_masked;
    printf("TIME_MS:%.4f;FIB:%d;ACC:%llu;CHECKSUM:%lld\n",
           elapsed_ms, f, (unsigned long long)acc_masked, (long long)checksum);
    return 0;
}
