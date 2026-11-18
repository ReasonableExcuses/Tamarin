#pragma once
#ifndef BENCHMARK_TIMER_H
#define BENCHMARK_TIMER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>

typedef struct {
    LARGE_INTEGER start;
    LARGE_INTEGER freq;
} BenchmarkTimer;

static inline BenchmarkTimer timer_start(void) {
    BenchmarkTimer t;
    if (!QueryPerformanceFrequency(&t.freq) || t.freq.QuadPart <= 0) {
        exit(1);
    }
    if (!QueryPerformanceCounter(&t.start)) {
        exit(1);
    }
    return t;
}

static inline double timer_elapsed_ms(BenchmarkTimer t) {
    LARGE_INTEGER end;
    if (!QueryPerformanceCounter(&end)) {
        exit(1);
    }
    return (double)(end.QuadPart - t.start.QuadPart) * 1000.0 / (double)t.freq.QuadPart;
}

#else
#include <time.h>

typedef struct {
    struct timespec start;
} BenchmarkTimer;

static inline BenchmarkTimer timer_start(void) {
    BenchmarkTimer t;
    if (clock_gettime(CLOCK_MONOTONIC, &t.start) != 0) {
        exit(1);
    }
    return t;
}

static inline double timer_elapsed_ms(BenchmarkTimer t) {
    struct timespec end;
    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        exit(1);
    }
    return (double)(end.tv_sec - t.start.tv_sec) * 1000.0 +
           (double)(end.tv_nsec - t.start.tv_nsec) / 1000000.0;
}
#endif

// Reinterprets IEEE-754 double-precision float to 64-bit integer bits without precision loss
static inline uint64_t double_to_bits(double d) {
    uint64_t bits = 0;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

#endif // BENCHMARK_TIMER_H
