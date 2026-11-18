// 01_integer.c - Integer Arithmetic Benchmark
// Tests prime sieve, Collatz sequences, and 64-bit unsigned modular hashing

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

static int collatz_steps(uint64_t n) {
    int steps = 0;
    while (n > 1) {
        if (n % 2 == 0) {
            n = n / 2;
        } else {
            n = 3 * n + 1;
        }
        steps++;
    }
    return steps;
}

int main(void) {
    int is_prime[20000];
    BenchmarkTimer timer = timer_start();

    // 1. Sieve of Eratosthenes (20,000 elements x 50 iterations)
    int prime_count = 0;
    for (int iter = 0; iter < 50; iter++) {
        for (int i = 0; i < 20000; i++) {
            is_prime[i] = 1;
        }
        is_prime[0] = 0;
        is_prime[1] = 0;
        for (int p = 2; p * p < 20000; p++) {
            if (is_prime[p] == 1) {
                for (int m = p * p; m < 20000; m += p) {
                    is_prime[m] = 0;
                }
            }
        }
        if (iter == 49) {
            for (int i = 0; i < 20000; i++) {
                if (is_prime[i] == 1) {
                    prime_count++;
                }
            }
        }
    }

    // 2. Collatz sequence steps (1 to 20,000)
    uint64_t total_collatz = 0;
    for (uint64_t i = 1; i <= 20000; i++) {
        total_collatz += (uint64_t)collatz_steps(i);
    }

    // 3. 64-bit unsigned modular integer hashing (2,000,000 iterations)
    uint64_t h = 1469598103934665603ULL;
    for (uint64_t i = 0; i < 2000000; i++) {
        h = (h * 1099511628211ULL + i) ^ (h >> 17);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    uint64_t checksum = (uint64_t)prime_count + total_collatz + (h & 2147483647ULL);
    printf("TIME_MS:%.4f;PRIMES:%d;COLLATZ:%llu;H:%llu;CHECKSUM:%llu\n",
           elapsed_ms, prime_count, (unsigned long long)total_collatz,
           (unsigned long long)(h & 2147483647ULL), (unsigned long long)checksum);
    return 0;
}
