// 13_nbody_simulation - C++ Normal (std::vector AoS)
// 4,096 bodies, 20 time steps = 335,544,320 pairwise interactions

#include <iostream>
#include <vector>
#include <cmath>
#include <cstdint>
#include <windows.h>

struct Particle {
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
    float mass;
};

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

int main() {
    const int N = 4096;
    std::vector<Particle> p(N);
    int64_t seed = 123456789;

    // Deterministic LCG initialization outside timed kernel
    for (int i = 0; i < N; i++) {
        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].x = (static_cast<float>(seed & 65535) / 65535.0f) * 20.0f - 10.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].y = (static_cast<float>(seed & 65535) / 65535.0f) * 20.0f - 10.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].z = (static_cast<float>(seed & 65535) / 65535.0f) * 20.0f - 10.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].vx = (static_cast<float>(seed & 65535) / 65535.0f) * 2.0f - 1.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].vy = (static_cast<float>(seed & 65535) / 65535.0f) * 2.0f - 1.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].vz = (static_cast<float>(seed & 65535) / 65535.0f) * 2.0f - 1.0f;

        seed = (seed * 1664525 + 1013904223) & 2147483647;
        p[i].mass = (static_cast<float>(seed & 65535) / 65535.0f) * 9.9f + 0.1f;
    }

    const float dt = 0.01f;
    const float softening = 0.01f;

    benchmark_start();

    // 20 simulation time steps
    for (int step = 0; step < 20; step++) {
        for (int i = 0; i < N; i++) {
            float px = p[i].x;
            float py = p[i].y;
            float pz = p[i].z;
            float ax = 0.0f;
            float ay = 0.0f;
            float az = 0.0f;

            for (int j = 0; j < N; j++) {
                float dx = p[j].x - px;
                float dy = p[j].y - py;
                float dz = p[j].z - pz;
                float distSqr = dx * dx + dy * dy + dz * dz + softening;
                float invDist = 1.0f / std::sqrt(distSqr);
                float invDist3 = invDist * invDist * invDist;
                float s = p[j].mass * invDist3;
                ax += dx * s;
                ay += dy * s;
                az += dz * s;
            }

            p[i].vx += ax * dt;
            p[i].vy += ay * dt;
            p[i].vz += az * dt;

            p[i].x = px + p[i].vx * dt;
            p[i].y = py + p[i].vy * dt;
            p[i].z = pz + p[i].vz * dt;
        }
    }

    double elapsed_ms = benchmark_elapsed_ms();

    float total_x = 0.0f;
    float total_y = 0.0f;
    float total_z = 0.0f;
    float total_v = 0.0f;
    float total_ek = 0.0f;

    for (int i = 0; i < N; i++) {
        total_x += p[i].x;
        total_y += p[i].y;
        total_z += p[i].z;
        total_v += p[i].vx + p[i].vy + p[i].vz;
        float vsq = p[i].vx * p[i].vx + p[i].vy * p[i].vy + p[i].vz * p[i].vz;
        total_ek += 0.5f * p[i].mass * vsq;
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
