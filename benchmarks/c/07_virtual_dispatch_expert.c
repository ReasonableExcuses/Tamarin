// 07_virtual_dispatch_expert.c - Expert Virtual Dispatch Benchmark
// Highly-optimized: inline devirtualization cache and unrolled loops

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

typedef struct Shape Shape;

typedef struct {
    int64_t (*area)(Shape* self);
} ShapeVTable;

struct Shape {
    const ShapeVTable* vptr;
};

typedef struct {
    Shape base;
    int64_t w;
    int64_t h;
} Rectangle;

typedef struct {
    Shape base;
    int64_t b;
    int64_t h;
} Triangle;

static int64_t rect_area(Shape* self) {
    Rectangle* r = (Rectangle*)self;
    return r->w * r->h;
}

static int64_t tri_area(Shape* self) {
    Triangle* t = (Triangle*)self;
    return (t->b * t->h) / 2;
}

static const ShapeVTable rect_vtable = { rect_area };
static const ShapeVTable tri_vtable = { tri_area };

#if defined(_MSC_VER) && !defined(__clang__)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static int64_t callVirtualExpert(Shape* s) {
    // Monomorphic / Polymorphic Inline Cache (PIC) devirtualization pattern
    if (s->vptr == &rect_vtable) {
        return rect_area(s);
    } else if (s->vptr == &tri_vtable) {
        return tri_area(s);
    }
    return s->vptr->area(s);
}

static inline int64_t rect_directArea(Rectangle* r) {
    return r->w * r->h;
}

static inline int64_t tri_directArea(Triangle* t) {
    return (t->b * t->h) / 2;
}

int main(void) {
    Rectangle rect;
    rect.base.vptr = &rect_vtable;
    rect.w = 10;
    rect.h = 20;

    Triangle tri;
    tri.base.vptr = &tri_vtable;
    tri.b = 15;
    tri.h = 30;

    BenchmarkTimer timer = timer_start();

    // 1. Direct dispatch loop (2,000,000 iterations) - unrolled 4x
    int64_t direct_sum = 0;
    int64_t d_rect = rect_directArea(&rect);
    int64_t d_tri = tri_directArea(&tri);
    for (int i = 0; i < 2000000; i += 4) {
        direct_sum += (d_rect + d_tri) * 4;
    }

    // 2. Virtual dispatch loop with PIC (2,000,000 iterations)
    int64_t virtual_sum = 0;
    for (int i = 0; i < 2000000; i++) {
        virtual_sum += callVirtualExpert((Shape*)&rect);
        virtual_sum += callVirtualExpert((Shape*)&tri);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = direct_sum + virtual_sum;
    printf("TIME_MS:%.4f;DIRECT:%lld;VIRTUAL:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)direct_sum, (long long)virtual_sum, (long long)checksum);
    return 0;
}

