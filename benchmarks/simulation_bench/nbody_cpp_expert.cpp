// 13_nbody_simulation - C++ Expert (Structure-of-Arrays SoA)
// 4,096 bodies, 20 time steps = 335,544,320 pairwise interactions

#include <iostream>
#include <vector>
#include <cmath>
#include <cstdint>
#include <windows.h>

constexpr int N = 4096;

static LARGE_INTEGER g_start_time;
static LARGE_INTEGER g_freq;

static void benchmark_start() {
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_start_time);
}

static double benchmark_elapsed_ms() {
    LARGE_INTEGER end_time;
    QueryPerformanceCounter(&end_time);
    return static_cast<double>(end_time.QuadPart - g_start_time.QuadPart) * 1000.0 / static_cast<double>(g_freq.QuadPart);
}

alignas(64) static float pos_x[N];
alignas(64) static float pos_y[N];
alignas(64) static float pos_z[N];
alignas(64) static float vel_x[N];
alignas(64) static float vel_y[N];
alignas(64) static float vel_z[N];
alignas(64) static float mass[N];

int main() {
    int64_t seed = 123456789;

    // Deterministic LCG initialization outside timed kernel
    for (int i = 0; i < N; i++) {
        seed = (seed * 1664525 + 1013904223) & 2147483647;
        pos_x[i] = (static_cast<float>(seed & 65535) / 65535.0f) * 20.0f - 10.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        pos_y[i] = (static_cast<float>(seed & 65535) / 65535.0f) * 20.0f - 10.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        pos_z[i] = (static_cast<float>(seed & 65535) / 65535.0f) * 20.0f - 10.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        vel_x[i] = (static_cast<float>(seed & 65535) / 65535.0f) * 2.0f - 1.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        vel_y[i] = (static_cast<float>(seed & 65535) / 65535.0f) * 2.0f - 1.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        vel_z[i] = (static_cast<float>(seed & 65535) / 65535.0f) * 2.0f - 1.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        mass[i] = (static_cast<float>(seed & 65535) / 65535.0f) * 9.9f + 0.1f;
    }

    constexpr float dt = 0.01f;
    constexpr float softening = 0.01f;

    benchmark_start();

    // 20 simulation time steps
    for (int step = 0; step < 20; step++) {
        for (int i = 0; i < N; i++) {
            const float px = pos_x[i];
            const float py = pos_y[i];
            const float pz = pos_z[i];
            float ax = 0.0f;
            float ay = 0.0f;
            float az = 0.0f;

            #pragma clang loop vectorize(enable)
            #pragma clang loop unroll(enable)
            for (int j = 0; j < N; j++) {
                float dx = pos_x[j] - px;
                float dy = pos_y[j] - py;
                float dz = pos_z[j] - pz;
                float distSqr = dx * dx + dy * dy + dz * dz + softening;
                float invDist = 1.0f / std::sqrt(distSqr);
                float invDist3 = invDist * invDist * invDist;
                float s = mass[j] * invDist3;
                ax += dx * s;
                ay += dy * s;
                az += dz * s;
            }

            vel_x[i] += ax * dt;
            vel_y[i] += ay * dt;
            vel_z[i] += az * dt;

            pos_x[i] = px + vel_x[i] * dt;
            pos_y[i] = py + vel_y[i] * dt;
            pos_z[i] = pz + vel_z[i] * dt;
        }
    }

    double elapsed_ms = benchmark_elapsed_ms();

    float total_x = 0.0f;
    float total_y = 0.0f;
    float total_z = 0.0f;
    float total_v = 0.0f;
    float total_ek = 0.0f;

    for (int i = 0; i < N; i++) {
        total_x += pos_x[i];
        total_y += pos_y[i];
        total_z += pos_z[i];
        total_v += vel_x[i] + vel_y[i] + vel_z[i];
        float vsq = vel_x[i] * vel_x[i] + vel_y[i] * vel_y[i] + vel_z[i] * vel_z[i];
        total_ek += 0.5f * mass[i] * vsq;
    }

    int64_t checksum = static_cast<int64_t>(total_x) + static_cast<int64_t>(total_y) +
                       static_cast<int64_t>(total_z) + static_cast<int64_t>(total_v) +
                       static_cast<int64_t>(total_ek);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "TIME_MS:%.4f;TOTAL_X:%lld;TOTAL_Y:%lld;TOTAL_Z:%lld;TOTAL_V:%lld;TOTAL_EK:%lld;CHECKSUM:%lld\n",
             elapsed_ms, static_cast<long long>(static_cast<int64_t>(total_x)),
             static_cast<long long>(static_cast<int64_t>(total_y)),
             static_cast<long long>(static_cast<int64_t>(total_z)),
             static_cast<long long>(static_cast<int64_t>(total_v)),
             static_cast<long long>(static_cast<int64_t>(total_ek)),
             static_cast<long long>(checksum));
    std::cout << buf;

    return 0;
}
