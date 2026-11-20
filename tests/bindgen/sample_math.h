#ifndef SAMPLE_MATH_H
#define SAMPLE_MATH_H

#define MAX_DIMENSIONS 4
#define PI 3.141592653589793
#define MATRIX_NAME "SampleMatrix"

typedef enum {
    MODE_FAST = 0,
    MODE_PRECISE = 1,
    MODE_FALLBACK = 2
} MathMode;

typedef struct {
    float x;
    float y;
    float z;
} Vec3;

typedef struct {
    Vec3 origin;
    Vec3 direction;
    float length;
} Ray;

typedef int (*LogCallback)(const char* message, int code);

typedef struct {
    int type;
    int trait;
    int fn;
    int match;
} KeywordTest;

Vec3 vec3_add(Vec3 a, Vec3 b);
float vec3_dot(const Vec3* a, const Vec3* b);
int compute_ray_intersection(const Ray* ray, Vec3* out_hit, LogCallback logger);

#endif // SAMPLE_MATH_H
