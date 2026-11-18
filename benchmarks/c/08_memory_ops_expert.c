// 08_memory_ops_expert.c - Expert Memory and String Operations Benchmark
// Highly-optimized: bitwise ASCII case inversion and unrolled pattern search

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

int main(void) {
    char buf[16384];

    for (int i = 0; i < 16384; i++) {
        int rem = i % 26;
        if ((i & 1) == 0) {
            buf[i] = (char)(65 + rem); // 'A' .. 'Z'
        } else {
            buf[i] = (char)(97 + rem); // 'a' .. 'z'
        }
    }

    BenchmarkTimer timer = timer_start();

    // 1. In-place case inversion (200 passes) - unrolled 8x bitwise XOR 0x20
    int64_t transform_count = 0;
    for (int pass = 0; pass < 200; pass++) {
        int64_t pcount = 0;
        for (int i = 0; i < 16384; i += 8) {
            for (int k = 0; k < 8; k++) {
                char c = buf[i + k];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                    buf[i + k] = (char)(c ^ 0x20);
                    pcount++;
                }
            }
        }
        transform_count += pcount;
#if defined(__clang__) || defined(__GNUC__)
        __asm__ __volatile__("" : "+m" (*(char (*)[16384])buf) : : "memory");
#elif defined(_MSC_VER)
        _ReadWriteBarrier();
#endif
    }

    // 2. Linear scan pattern search (100 passes) - unrolled 4x
    int64_t matches = 0;
    for (int pass = 0; pass < 100; pass++) {
        int64_t pmatches = 0;
        for (int i = 0; i < 16380; i += 4) {
            if (buf[i] == 'A' && buf[i + 1] == 'b' && buf[i + 2] == 'C') pmatches++;
            if (buf[i + 1] == 'A' && buf[i + 2] == 'b' && buf[i + 3] == 'C') pmatches++;
            if (buf[i + 2] == 'A' && buf[i + 3] == 'b' && buf[i + 4] == 'C') pmatches++;
            if (buf[i + 3] == 'A' && buf[i + 4] == 'b' && buf[i + 5] == 'C') pmatches++;
        }
        matches += pmatches;
    }

    int64_t byte_sum = 0;
    for (int i = 0; i < 16384; i += 8) {
        byte_sum += (int64_t)((unsigned char)buf[i]) + (int64_t)((unsigned char)buf[i + 1])
                  + (int64_t)((unsigned char)buf[i + 2]) + (int64_t)((unsigned char)buf[i + 3])
                  + (int64_t)((unsigned char)buf[i + 4]) + (int64_t)((unsigned char)buf[i + 5])
                  + (int64_t)((unsigned char)buf[i + 6]) + (int64_t)((unsigned char)buf[i + 7]);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = transform_count + matches + byte_sum;
    printf("TIME_MS:%.4f;TRANSFORMS:%lld;MATCHES:%lld;BYTE_SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)transform_count, (long long)matches,
           (long long)byte_sum, (long long)checksum);
    return 0;
}

