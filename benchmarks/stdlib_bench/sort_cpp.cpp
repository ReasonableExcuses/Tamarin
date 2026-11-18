#include <iostream>
#include <vector>
#include <algorithm>
#include <windows.h>

int main() {
    const int N = 2000000;
    std::vector<int> data(N);
    unsigned int seed = 123456789;
    for (int i = 0; i < N; ++i) {
        seed = seed * 1664525 + 1013904223;
        data[i] = (int)(seed & 0x7FFFFFFF);
    }

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    std::sort(data.begin(), data.end());

    QueryPerformanceCounter(&t1);
    double elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    bool ok = std::is_sorted(data.begin(), data.end());

    std::cout << "[C++ std::sort] N=" << N << " | sorted=" << (ok ? "YES" : "NO")
              << " | min=" << data[0] << " | max=" << data[N - 1]
              << " | time=" << elapsed_ms << " ms\n";
    return 0;
}
