#include "sdl3_mini.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

static const int WIDTH = 1280;
static const int HEIGHT = 720;
static const int SAMPLES_PER_PIXEL = 16;
static const int MAX_DEPTH = 8;
static const int NUM_SPHERES = 124;

// ----------------------------------------------------------------------------
// Deterministic Math & PRNG
// ----------------------------------------------------------------------------
static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline float rand_float(uint32_t* state) {
    uint32_t u = xorshift32(state);
    return (float)(u & 16777215u) * (1.0f / 16777216.0f);
}

struct Vec3 {
    float x, y, z;
    constexpr Vec3() : x(0), y(0), z(0) {}
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(const Vec3& o) const { return Vec3(x * o.x, y * o.y, z * o.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
};

static inline Vec3 operator*(float s, const Vec3& v) { return Vec3(v.x * s, v.y * s, v.z * s); }
static inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline Vec3 normalize(const Vec3& v) {
    float len = sqrtf(dot(v, v));
    return (len > 0.0f) ? (v * (1.0f / len)) : Vec3(0, 0, 0);
}
static inline Vec3 random_unit_vector(uint32_t* rng) {
    float u1 = rand_float(rng);
    float u2 = rand_float(rng);
    float z = 1.0f - 2.0f * u1;
    float r = sqrtf(fmaxf(0.0f, 1.0f - z * z));
    float phi = 6.283185307179586f * u2;
    return Vec3(r * cosf(phi), r * sinf(phi), z);
}
static inline Vec3 reflect(const Vec3& v, const Vec3& n) {
    return v - 2.0f * dot(v, n) * n;
}
static inline Vec3 refract(const Vec3& uv, const Vec3& n, float etai_over_etat) {
    float cos_theta = fminf(dot(-uv, n), 1.0f);
    Vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    Vec3 r_out_parallel = -sqrtf(fabsf(1.0f - dot(r_out_perp, r_out_perp))) * n;
    return r_out_perp + r_out_parallel;
}

struct Ray {
    Vec3 origin;
    Vec3 dir;
    Ray() {}
    Ray(const Vec3& o, const Vec3& d) : origin(o), dir(d) {}
};

struct HitRecord {
    Vec3 p;
    Vec3 normal;
    float t;
    bool front_face;
    int mat_type; // 0 = diffuse, 1 = metal, 2 = dielectric
    Vec3 albedo;
    float fuzz;
    float ior;
};

// ----------------------------------------------------------------------------
// Scene Representations: AoS vs SoA
// ----------------------------------------------------------------------------
struct SphereAoS {
    Vec3 center;
    float radius;
    float inv_radius;
    Vec3 albedo;
    float fuzz;
    float ior;
    int mat_type;
};

struct SphereSceneSoA {
    float cx[NUM_SPHERES];
    float cy[NUM_SPHERES];
    float cz[NUM_SPHERES];
    float radius[NUM_SPHERES];
    float inv_radius[NUM_SPHERES];
    float albedo_r[NUM_SPHERES];
    float albedo_g[NUM_SPHERES];
    float albedo_b[NUM_SPHERES];
    float fuzz[NUM_SPHERES];
    float ior[NUM_SPHERES];
    int mat_type[NUM_SPHERES];
};

static SphereAoS g_scene_aos[NUM_SPHERES];
static SphereSceneSoA g_scene_soa;
static uint32_t g_framebuffer[HEIGHT][WIDTH];

static void init_scene() {
    // 0: Ground
    g_scene_soa.cx[0] = 0.0f; g_scene_soa.cy[0] = -1000.0f; g_scene_soa.cz[0] = 0.0f;
    g_scene_soa.radius[0] = 1000.0f; g_scene_soa.inv_radius[0] = 0.001f;
    g_scene_soa.mat_type[0] = 0;
    g_scene_soa.albedo_r[0] = 0.5f; g_scene_soa.albedo_g[0] = 0.5f; g_scene_soa.albedo_b[0] = 0.5f;
    g_scene_soa.fuzz[0] = 0.0f; g_scene_soa.ior[0] = 1.0f;

    // 1: Hero Glass
    g_scene_soa.cx[1] = 0.0f; g_scene_soa.cy[1] = 1.0f; g_scene_soa.cz[1] = 0.0f;
    g_scene_soa.radius[1] = 1.0f; g_scene_soa.inv_radius[1] = 1.0f;
    g_scene_soa.mat_type[1] = 2;
    g_scene_soa.albedo_r[1] = 1.0f; g_scene_soa.albedo_g[1] = 1.0f; g_scene_soa.albedo_b[1] = 1.0f;
    g_scene_soa.fuzz[1] = 0.0f; g_scene_soa.ior[1] = 1.5f;

    // 2: Hero Diffuse
    g_scene_soa.cx[2] = -4.0f; g_scene_soa.cy[2] = 1.0f; g_scene_soa.cz[2] = 0.0f;
    g_scene_soa.radius[2] = 1.0f; g_scene_soa.inv_radius[2] = 1.0f;
    g_scene_soa.mat_type[2] = 0;
    g_scene_soa.albedo_r[2] = 0.4f; g_scene_soa.albedo_g[2] = 0.2f; g_scene_soa.albedo_b[2] = 0.1f;
    g_scene_soa.fuzz[2] = 0.0f; g_scene_soa.ior[2] = 1.0f;

    // 3: Hero Metal
    g_scene_soa.cx[3] = 4.0f; g_scene_soa.cy[3] = 1.0f; g_scene_soa.cz[3] = 0.0f;
    g_scene_soa.radius[3] = 1.0f; g_scene_soa.inv_radius[3] = 1.0f;
    g_scene_soa.mat_type[3] = 1;
    g_scene_soa.albedo_r[3] = 0.7f; g_scene_soa.albedo_g[3] = 0.6f; g_scene_soa.albedo_b[3] = 0.5f;
    g_scene_soa.fuzz[3] = 0.0f; g_scene_soa.ior[3] = 1.0f;

    // 4..123: 120 procedural spheres
    int idx = 4;
    for (int a = -7; a <= 7; a++) {
        for (int b = -4; b <= 3; b++) {
            uint32_t rng_state = (uint32_t)((a + 10) * 100 + (b + 10));
            float rx = rand_float(&rng_state);
            float rz = rand_float(&rng_state);
            float center_x = (float)a * 1.6f + 0.9f * rx;
            float center_z = (float)b * 1.6f + 0.9f * rz;
            float center_y = 0.2f;
            float rad = 0.2f;

            float d1 = (center_x - 0.0f)*(center_x - 0.0f) + (center_z - 0.0f)*(center_z - 0.0f);
            float d2 = (center_x + 4.0f)*(center_x + 4.0f) + (center_z - 0.0f)*(center_z - 0.0f);
            float d3 = (center_x - 4.0f)*(center_x - 4.0f) + (center_z - 0.0f)*(center_z - 0.0f);
            if (d1 < 1.44f || d2 < 1.44f || d3 < 1.44f) {
                center_z += 1.5f;
            }

            g_scene_soa.cx[idx] = center_x;
            g_scene_soa.cy[idx] = center_y;
            g_scene_soa.cz[idx] = center_z;
            g_scene_soa.radius[idx] = rad;
            g_scene_soa.inv_radius[idx] = 1.0f / rad;

            float choose_mat = rand_float(&rng_state);
            if (choose_mat < 0.70f) {
                g_scene_soa.mat_type[idx] = 0;
                g_scene_soa.albedo_r[idx] = rand_float(&rng_state) * rand_float(&rng_state);
                g_scene_soa.albedo_g[idx] = rand_float(&rng_state) * rand_float(&rng_state);
                g_scene_soa.albedo_b[idx] = rand_float(&rng_state) * rand_float(&rng_state);
                g_scene_soa.fuzz[idx] = 0.0f;
                g_scene_soa.ior[idx] = 1.0f;
            } else if (choose_mat < 0.90f) {
                g_scene_soa.mat_type[idx] = 1;
                g_scene_soa.albedo_r[idx] = 0.5f * (1.0f + rand_float(&rng_state));
                g_scene_soa.albedo_g[idx] = 0.5f * (1.0f + rand_float(&rng_state));
                g_scene_soa.albedo_b[idx] = 0.5f * (1.0f + rand_float(&rng_state));
                g_scene_soa.fuzz[idx] = 0.5f * rand_float(&rng_state);
                g_scene_soa.ior[idx] = 1.0f;
            } else {
                g_scene_soa.mat_type[idx] = 2;
                g_scene_soa.albedo_r[idx] = 1.0f;
                g_scene_soa.albedo_g[idx] = 1.0f;
                g_scene_soa.albedo_b[idx] = 1.0f;
                g_scene_soa.fuzz[idx] = 0.0f;
                g_scene_soa.ior[idx] = 1.5f;
            }
            idx++;
        }
    }

    // Populate AoS copy for transparent baseline comparison
    for (int i = 0; i < NUM_SPHERES; i++) {
        g_scene_aos[i].center = Vec3(g_scene_soa.cx[i], g_scene_soa.cy[i], g_scene_soa.cz[i]);
        g_scene_aos[i].radius = g_scene_soa.radius[i];
        g_scene_aos[i].inv_radius = g_scene_soa.inv_radius[i];
        g_scene_aos[i].albedo = Vec3(g_scene_soa.albedo_r[i], g_scene_soa.albedo_g[i], g_scene_soa.albedo_b[i]);
        g_scene_aos[i].fuzz = g_scene_soa.fuzz[i];
        g_scene_aos[i].ior = g_scene_soa.ior[i];
        g_scene_aos[i].mat_type = g_scene_soa.mat_type[i];
    }
}

// ----------------------------------------------------------------------------
// Intersection Kernels (SoA & AoS)
// ----------------------------------------------------------------------------
static inline bool hit_scene_soa(const Ray& r, float t_min, float t_max, HitRecord* rec, const SphereSceneSoA* s) {
    bool hit_anything = false;
    float closest_so_far = t_max;
    int closest_idx = -1;

    for (int i = 0; i < NUM_SPHERES; i++) {
        float oc_x = r.origin.x - s->cx[i];
        float oc_y = r.origin.y - s->cy[i];
        float oc_z = r.origin.z - s->cz[i];
        float half_b = oc_x * r.dir.x + oc_y * r.dir.y + oc_z * r.dir.z;
        float c = oc_x * oc_x + oc_y * oc_y + oc_z * oc_z - s->radius[i] * s->radius[i];
        float discriminant = half_b * half_b - c;

        if (discriminant > 0.0f) {
            float sqrtd = sqrtf(discriminant);
            float root = -half_b - sqrtd;
            if (root <= t_min || closest_so_far <= root) {
                root = -half_b + sqrtd;
            }
            if (root > t_min && root < closest_so_far) {
                closest_so_far = root;
                closest_idx = i;
                hit_anything = true;
            }
        }
    }

    if (hit_anything) {
        rec->t = closest_so_far;
        rec->p = r.origin + r.dir * closest_so_far;
        Vec3 outward_normal = (rec->p - Vec3(s->cx[closest_idx], s->cy[closest_idx], s->cz[closest_idx])) * s->inv_radius[closest_idx];
        rec->front_face = dot(r.dir, outward_normal) < 0.0f;
        rec->normal = rec->front_face ? outward_normal : -outward_normal;
        rec->mat_type = s->mat_type[closest_idx];
        rec->albedo = Vec3(s->albedo_r[closest_idx], s->albedo_g[closest_idx], s->albedo_b[closest_idx]);
        rec->fuzz = s->fuzz[closest_idx];
        rec->ior = s->ior[closest_idx];
    }
    return hit_anything;
}

static inline bool hit_scene_aos(const Ray& r, float t_min, float t_max, HitRecord* rec, const SphereAoS* s) {
    bool hit_anything = false;
    float closest_so_far = t_max;

    for (int i = 0; i < NUM_SPHERES; i++) {
        Vec3 oc = r.origin - s[i].center;
        float half_b = dot(oc, r.dir);
        float c = dot(oc, oc) - s[i].radius * s[i].radius;
        float discriminant = half_b * half_b - c;

        if (discriminant > 0.0f) {
            float sqrtd = sqrtf(discriminant);
            float root = -half_b - sqrtd;
            if (root <= t_min || closest_so_far <= root) {
                root = -half_b + sqrtd;
            }
            if (root > t_min && root < closest_so_far) {
                closest_so_far = root;
                hit_anything = true;
                rec->t = root;
                rec->p = r.origin + r.dir * root;
                Vec3 outward_normal = (rec->p - s[i].center) * s[i].inv_radius;
                rec->front_face = dot(r.dir, outward_normal) < 0.0f;
                rec->normal = rec->front_face ? outward_normal : -outward_normal;
                rec->mat_type = s[i].mat_type;
                rec->albedo = s[i].albedo;
                rec->fuzz = s[i].fuzz;
                rec->ior = s[i].ior;
            }
        }
    }
    return hit_anything;
}

// ----------------------------------------------------------------------------
// Ray Tracing Kernel
// ----------------------------------------------------------------------------
struct Camera {
    Vec3 origin;
    Vec3 lower_left_corner;
    Vec3 horizontal;
    Vec3 vertical;
};

static Camera init_camera() {
    Camera cam;
    cam.origin = Vec3(13.0f, 2.0f, 3.0f);
    Vec3 lookat(0.0f, 0.0f, 0.0f);
    Vec3 vup(0.0f, 1.0f, 0.0f);
    float vfov = 20.0f;
    float aspect_ratio = (float)WIDTH / (float)HEIGHT;

    float theta = vfov * (3.141592653589793f / 180.0f);
    float h = tanf(theta / 2.0f);
    float viewport_height = 2.0f * h;
    float viewport_width = aspect_ratio * viewport_height;

    Vec3 w = normalize(cam.origin - lookat);
    Vec3 u = normalize(cross(vup, w));
    Vec3 v = cross(w, u);

    cam.horizontal = viewport_width * u;
    cam.vertical = viewport_height * v;
    cam.lower_left_corner = cam.origin - cam.horizontal / 2.0f - cam.vertical / 2.0f - w;
    return cam;
}

static inline Vec3 ray_color(const Ray& r_in, uint32_t* rng, bool use_soa) {
    Vec3 cur_attenuation(1.0f, 1.0f, 1.0f);
    Ray cur_ray = r_in;
    Vec3 final_color(0.0f, 0.0f, 0.0f);

    for (int depth = 0; depth < MAX_DEPTH; depth++) {
        HitRecord rec;
        bool hit = use_soa ? hit_scene_soa(cur_ray, 0.001f, 1e20f, &rec, &g_scene_soa)
                           : hit_scene_aos(cur_ray, 0.001f, 1e20f, &rec, g_scene_aos);
        if (hit) {
            if (rec.mat_type == 0) {
                // Lambertian
                Vec3 scatter_dir = rec.normal + random_unit_vector(rng);
                if (fabsf(scatter_dir.x) < 1e-6f && fabsf(scatter_dir.y) < 1e-6f && fabsf(scatter_dir.z) < 1e-6f) {
                    scatter_dir = rec.normal;
                }
                cur_ray = Ray(rec.p, normalize(scatter_dir));
                cur_attenuation = cur_attenuation * rec.albedo;
            } else if (rec.mat_type == 1) {
                // Metal
                Vec3 reflected = reflect(normalize(cur_ray.dir), rec.normal);
                Vec3 scatter_dir = reflected + rec.fuzz * random_unit_vector(rng);
                cur_ray = Ray(rec.p, normalize(scatter_dir));
                cur_attenuation = cur_attenuation * rec.albedo;
                if (dot(cur_ray.dir, rec.normal) <= 0.0f) {
                    cur_attenuation = Vec3(0, 0, 0);
                    break;
                }
            } else {
                // Dielectric (Glass)
                float refraction_ratio = rec.front_face ? (1.0f / rec.ior) : rec.ior;
                Vec3 unit_dir = normalize(cur_ray.dir);
                float cos_theta = fminf(dot(-unit_dir, rec.normal), 1.0f);
                float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
                bool cannot_refract = (refraction_ratio * sin_theta) > 1.0f;

                float r0 = (1.0f - refraction_ratio) / (1.0f + refraction_ratio);
                r0 = r0 * r0;
                float p5 = 1.0f - cos_theta;
                float reflectance = r0 + (1.0f - r0) * (p5 * p5 * p5 * p5 * p5);

                Vec3 direction;
                if (cannot_refract || reflectance > rand_float(rng)) {
                    direction = reflect(unit_dir, rec.normal);
                } else {
                    direction = refract(unit_dir, rec.normal, refraction_ratio);
                }
                cur_ray = Ray(rec.p, normalize(direction));
            }
        } else {
            // Background Sky
            Vec3 unit_dir = normalize(cur_ray.dir);
            float t = 0.5f * (unit_dir.y + 1.0f);
            Vec3 sky = (1.0f - t) * Vec3(1.0f, 1.0f, 1.0f) + t * Vec3(0.5f, 0.7f, 1.0f);
            final_color = cur_attenuation * sky;
            break;
        }
    }
    return final_color;
}

static void render_scanline(int y, int frame_idx, const Camera& cam, bool use_soa) {
    int j = HEIGHT - 1 - y;
    for (int x = 0; x < WIDTH; x++) {
        uint32_t pixel_seed = ((uint32_t)y * (uint32_t)WIDTH + (uint32_t)x) * 1973u + (uint32_t)frame_idx * 9277u + 1u;
        xorshift32(&pixel_seed);
        xorshift32(&pixel_seed);

        float tot_r = 0.0f, tot_g = 0.0f, tot_b = 0.0f;
        for (int s = 0; s < SAMPLES_PER_PIXEL; s++) {
            float u_jitter = rand_float(&pixel_seed);
            float v_jitter = rand_float(&pixel_seed);
            float u_coord = ((float)x + u_jitter) * (1.0f / (float)WIDTH);
            float v_coord = ((float)j + v_jitter) * (1.0f / (float)HEIGHT);

            Vec3 ray_dir = cam.lower_left_corner + u_coord * cam.horizontal + v_coord * cam.vertical - cam.origin;
            Ray r(cam.origin, normalize(ray_dir));
            Vec3 c = ray_color(r, &pixel_seed, use_soa);
            tot_r += c.x;
            tot_g += c.y;
            tot_b += c.z;
        }

        float scale = 1.0f / (float)SAMPLES_PER_PIXEL;
        float r = tot_r * scale;
        float g = tot_g * scale;
        float b = tot_b * scale;

        r = sqrtf(r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r));
        g = sqrtf(g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g));
        b = sqrtf(b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b));

        uint32_t ir = (uint32_t)(255.999f * r);
        uint32_t ig = (uint32_t)(255.999f * g);
        uint32_t ib = (uint32_t)(255.999f * b);
        uint32_t ia = 255u;

        // RGBA32 format: R byte 0, G byte 1, B byte 2, A byte 3
        g_framebuffer[y][x] = ir | (ig << 8) | (ib << 16) | (ia << 24);
    }
}

static double render_frame(int frame_idx, const Camera& cam, bool use_soa) {
    auto t0 = std::chrono::steady_clock::now();

    unsigned num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 8;

    std::atomic<int> next_y(0);
    auto worker = [&]() {
        while (true) {
            int y = next_y.fetch_add(1, std::memory_order_relaxed);
            if (y >= HEIGHT) break;
            render_scanline(y, frame_idx, cam, use_soa);
        }
    };

    std::vector<std::jthread> threads;
    threads.reserve(num_threads);
    for (unsigned i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }
    threads.clear(); // wait for all threads

    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

static uint64_t compute_checksum() {
    uint64_t hash = 14695981039346656037ull;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            uint32_t p = g_framebuffer[y][x];
            uint8_t b0 = (uint8_t)(p & 0xFF);
            uint8_t b1 = (uint8_t)((p >> 8) & 0xFF);
            uint8_t b2 = (uint8_t)((p >> 16) & 0xFF);
            uint8_t b3 = (uint8_t)((p >> 24) & 0xFF);
            hash = (hash ^ b0) * 1099511628211ull;
            hash = (hash ^ b1) * 1099511628211ull;
            hash = (hash ^ b2) * 1099511628211ull;
            hash = (hash ^ b3) * 1099511628211ull;
        }
    }
    return hash;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// BMP Screenshot Exporter
// ----------------------------------------------------------------------------
static bool save_bmp(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return false;

    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    // File size: 54 + 1280 * 720 * 3 = 2764854
    header[2] = 0x36; header[3] = 0x30; header[4] = 0x2A; header[5] = 0x00;
    header[10] = 54; // offset
    header[14] = 40; // biSize
    header[18] = 0x00; header[19] = 0x05; // 1280
    header[22] = 0x30; header[23] = 0xFD; header[24] = 0xFF; header[25] = 0xFF; // -720
    header[26] = 1; // planes
    header[28] = 24; // bpp
    header[34] = 0x00; header[35] = 0x30; header[36] = 0x2A; // img size
    header[38] = 0x13; header[39] = 0x0B; // xppm
    header[42] = 0x13; header[43] = 0x0B; // yppm

    fwrite(header, 1, 54, fp);

    uint8_t row[WIDTH * 3];
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            uint32_t px = g_framebuffer[y][x];
            row[x * 3 + 0] = (uint8_t)((px >> 16) & 0xFF); // B
            row[x * 3 + 1] = (uint8_t)((px >> 8) & 0xFF);  // G
            row[x * 3 + 2] = (uint8_t)(px & 0xFF);         // R
        }
        fwrite(row, 1, WIDTH * 3, fp);
    }
    fclose(fp);
    return true;
}

// ----------------------------------------------------------------------------
// Main Application
// ----------------------------------------------------------------------------
int main(int argc, char** argv) {
    bool headless = false;
    int headless_frames = 1;
    bool use_soa = true;
    bool take_screenshot = false;
    int gui_max_frames = 300;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0 && i + 1 < argc) {
            headless = true;
            headless_frames = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--gui-frames") == 0 && i + 1 < argc) {
            gui_max_frames = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--screenshot") == 0) {
            take_screenshot = true;
        } else if (strcmp(argv[i], "--aos") == 0) {
            use_soa = false;
        } else if (strcmp(argv[i], "--soa") == 0) {
            use_soa = true;
        }
    }

    init_scene();
    Camera cam = init_camera();

    if (headless) {
        printf("Running in HEADLESS mode (%d frames, layout=%s)...\n", headless_frames, use_soa ? "SoA" : "AoS");
        double total_ms = 0.0;
        uint64_t last_checksum = 0;

        for (int f = 0; f < headless_frames; f++) {
            double frame_ms = render_frame(f, cam, use_soa);
            total_ms += frame_ms;
            last_checksum = compute_checksum();
            double mrays = ((double)WIDTH * HEIGHT * SAMPLES_PER_PIXEL) / (frame_ms * 1000.0);

            if (f == 0 && take_screenshot) {
                save_bmp("benchmarks/results/raytracer_cpp.bmp");
                printf("SCREENSHOT_SAVED: benchmarks/results/raytracer_cpp.bmp\n");
            }

            printf("FRAME %d: TIME_MS=%.2f MRAYS_PER_SEC=%.2f CHECKSUM=0x%016llX\n", f, frame_ms, mrays, (unsigned long long)last_checksum);
            fflush(stdout);
        }

        double avg_ms = total_ms / headless_frames;
        double avg_mrays = ((double)WIDTH * HEIGHT * SAMPLES_PER_PIXEL) / (avg_ms * 1000.0);
        printf("BENCHMARK_COMPLETE: AVG_TIME_MS=%.2f AVG_MRAYS_PER_SEC=%.2f FINAL_CHECKSUM=0x%016llX\n", avg_ms, avg_mrays, (unsigned long long)last_checksum);
        return 0;
    }

    // Interactive SDL3 Window & Streaming Texture Mode
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Failed to initialize SDL3: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Tamarin vs C++ vs Rust Raytracer (SDL3)", WIDTH, HEIGHT, 0, &window, &renderer)) {
        fprintf(stderr, "Failed to create window and renderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderVSync(renderer, 0); // Unthrottled

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    if (!texture) {
        fprintf(stderr, "Failed to create streaming texture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    printf("Starting unthrottled live raytracer (1280x720, 16 SPP, 124 spheres). Press ESC to exit, 'S' for screenshot.\n");

    int frame_idx = 0;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                running = false;
            }
        }

        // 1. Strict isolated raytrace timing
        double trace_ms = render_frame(frame_idx, cam, use_soa);

        if (frame_idx == 0) {
            save_bmp("benchmarks/results/raytracer_cpp.bmp");
            printf("Saved screenshot to benchmarks/results/raytracer_cpp.bmp\n");
        }

        // 2. Zero-copy SDL3 texture streaming
        void* pixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(texture, nullptr, &pixels, &pitch)) {
            for (int y = 0; y < HEIGHT; y++) {
                memcpy((uint8_t*)pixels + y * pitch, &g_framebuffer[y][0], WIDTH * sizeof(uint32_t));
            }
            SDL_UnlockTexture(texture);
        }

        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        double mrays = ((double)WIDTH * HEIGHT * SAMPLES_PER_PIXEL) / (trace_ms * 1000.0);
        printf("\rFrame %d | Trace: %.2f ms (%.2f Mrays/s) | Present: OK", frame_idx, trace_ms, mrays);
        fflush(stdout);

        frame_idx++;
        if (frame_idx >= gui_max_frames) break;
    }
    printf("\n");

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
