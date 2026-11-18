// 07_virtual_dispatch.c - OOP Direct vs Virtual Dispatch Benchmark
// Measures vtable dynamic dispatch overhead compared to direct method calls

#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

struct Shape;

typedef struct ShapeVTable {
    int64_t (*area)(struct Shape* self);
} ShapeVTable;

typedef struct Shape {
    const ShapeVTable* vptr;
} Shape;

typedef struct Rectangle {
    Shape base;
    int64_t w;
    int64_t h;
} Rectangle;

typedef struct Triangle {
    Shape base;
    int64_t b;
    int64_t h;
} Triangle;

static int64_t Rect_area(Shape* self) {
    Rectangle* r = (Rectangle*)self;
    return r->w * r->h;
}

static int64_t Rect_directArea(Rectangle* r) {
    return r->w * r->h;
}

static int64_t Tri_area(Shape* self) {
    Triangle* t = (Triangle*)self;
    return (t->b * t->h) / 2;
}

static int64_t Tri_directArea(Triangle* t) {
    return (t->b * t->h) / 2;
}

static const ShapeVTable Rect_vtable = { Rect_area };
static const ShapeVTable Tri_vtable = { Tri_area };

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static int64_t callVirtual(Shape* s) {
    return s->vptr->area(s);
}

int main(void) {
    Rectangle rect;
    rect.base.vptr = &Rect_vtable;
    rect.w = 10;
    rect.h = 20;

    Triangle tri;
    tri.base.vptr = &Tri_vtable;
    tri.b = 15;
    tri.h = 30;

    BenchmarkTimer timer = timer_start();

    // 1. Direct dispatch loop (2,000,000 iterations)
    int64_t direct_sum = 0;
    for (int i = 0; i < 2000000; i++) {
        direct_sum += Rect_directArea(&rect);
        direct_sum += Tri_directArea(&tri);
    }

    // 2. Virtual dispatch loop (2,000,000 iterations)
    int64_t virtual_sum = 0;
    for (int i = 0; i < 2000000; i++) {
        virtual_sum += callVirtual((Shape*)&rect);
        virtual_sum += callVirtual((Shape*)&tri);
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = direct_sum + virtual_sum;
    printf("TIME_MS:%.4f;DIRECT:%lld;VIRTUAL:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)direct_sum, (long long)virtual_sum, (long long)checksum);
    return 0;
}
