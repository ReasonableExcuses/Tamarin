// 01_integer_expert.c - Expert Integer Arithmetic Benchmark
// Highly-optimized: bit-manipulation Collatz (ctz batch halving), unrolled sieve, unrolled hash

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
#include "../common/timer.h"

static inline int collatz_steps_expert(uint64_t n) {
    int steps = 0;
    while (n > 1) {
        if ((n & 1) == 0) {
#if defined(__GNUC__) || defined(__clang__)
            int tz = __builtin_ctzll(n);
#elif defined(_MSC_VER)
            unsigned long tz;
            _BitScanForward64(&tz, n);
#else
            int tz = 0;
            while ((n & 1) == 0) { n >>= 1; tz++; }
#endif
            steps += (int)tz;
            n >>= tz;
        } else {
            n = 3 * n + 1;
            steps++;
        }
    }
    return steps;
}

int main(void) {
    int is_prime[20000];
    BenchmarkTimer timer = timer_start();

    // 1. Sieve of Eratosthenes (20,000 elements x 50 iterations)
    int prime_count = 0;
    for (int iter = 0; iter < 50; iter++) {
        // Fast fill
        for (int i = 0; i < 20000; i += 4) {
            is_prime[i] = 1;
            is_prime[i + 1] = 1;
            is_prime[i + 2] = 1;
            is_prime[i + 3] = 1;
        }
        is_prime[0] = 0;
        is_prime[1] = 0;

        for (int p = 2; p * p < 20000; p++) {
            if (is_prime[p] == 1) {
                int m = p * p;
                int step = p * 2;
                // Unroll inner marking loop
                while (m + p < 20000) {
                    is_prime[m] = 0;
                    is_prime[m + p] = 0;
                    m += step;
                }
                if (m < 20000) {
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
        total_collatz += (uint64_t)collatz_steps_expert(i);
    }

    // 3. 64-bit unsigned modular integer hashing (2,000,000 iterations)
    uint64_t h = 1469598103934665603ULL;
    for (uint64_t i = 0; i < 2000000; i += 4) {
        h = (h * 1099511628211ULL + i) ^ (h >> 17);
        h = (h * 1099511628211ULL + (i + 1)) ^ (h >> 17);
        h = (h * 1099511628211ULL + (i + 2)) ^ (h >> 17);
        h = (h * 1099511628211ULL + (i + 3)) ^ (h >> 17);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    uint64_t checksum = (uint64_t)prime_count + total_collatz + (h & 2147483647ULL);
    printf("TIME_MS:%.4f;PRIMES:%d;COLLATZ:%llu;H:%llu;CHECKSUM:%llu\n",
           elapsed_ms, prime_count, (unsigned long long)total_collatz,
           (unsigned long long)(h & 2147483647ULL), (unsigned long long)checksum);
    return 0;
}

