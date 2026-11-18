// 08_memory_ops.c - Memory and String Operations Benchmark
// Tests byte buffer operations, character transformations, and pattern matching

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

int main(void) {
    char buf[16384];

    // Initialize buffer outside timed section
    for (int i = 0; i < 16384; i++) {
        int rem = i % 26;
        if ((i & 1) == 0) {
            buf[i] = (char)(65 + rem); // 'A' .. 'Z'
        } else {
            buf[i] = (char)(97 + rem); // 'a' .. 'z'
        }
    }

    BenchmarkTimer timer = timer_start();

    // 1. In-place case inversion (200 passes)
    int64_t transform_count = 0;
    for (int pass = 0; pass < 200; pass++) {
        for (int i = 0; i < 16384; i++) {
            char c = buf[i];
            if (c >= (char)65 && c <= (char)90) {
                buf[i] = (char)((int)c + 32);
                transform_count++;
            } else if (c >= (char)97 && c <= (char)122) {
                buf[i] = (char)((int)c - 32);
                transform_count++;
            }
        }
#if defined(__clang__) || defined(__GNUC__)
        __asm__ __volatile__("" : "+m" (*(char (*)[16384])buf) : : "memory");
#elif defined(_MSC_VER)
        _ReadWriteBarrier();
#endif
    }

    // 2. Linear scan pattern search (100 passes)
    int64_t matches = 0;
    for (int pass = 0; pass < 100; pass++) {
        for (int i = 0; i < 16380; i++) {
            if (buf[i] == (char)65 && buf[i + 1] == (char)98 && buf[i + 2] == (char)67) {
                matches++;
            }
        }
    }

    int64_t byte_sum = 0;
    for (int i = 0; i < 16384; i++) {
        byte_sum += (int64_t)((int)buf[i]);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = transform_count + matches + byte_sum;
    printf("TIME_MS:%.4f;TRANSFORMS:%lld;MATCHES:%lld;BYTE_SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)transform_count, (long long)matches, (long long)byte_sum, (long long)checksum);
    return 0;
}
