// 12_adt_matching.c - Milestone v0.7: Algebraic Data Types, Pattern Matching & Error Handling
#include <stdio.h>
#include <stdint.h>
#include "../common/timer.h"

typedef struct {
    int id;
} Entity;

typedef enum {
    EVENT_QUIT = 0,
    EVENT_MOVE = 1,
    EVENT_DAMAGE = 2,
    EVENT_TELEMETRY = 3
} EventTag;

typedef struct {
    EventTag tag;
    union {
        struct { int dx, dy; } move;
        struct { int target_id, amount; } damage;
        struct { int code, length, flags; } telemetry;
    } payload;
} Event;

typedef struct {
    int is_ok;
    int value;
} Result;

static Result stage1(int val) {
    if ((val & 7) == 0) return (Result){0, 1};
    return (Result){1, val + 10};
}

static Result stage2(int val) {
    if ((val & 32) != 0) return (Result){0, 2};
    return (Result){1, val * 3};
}

static Result stage3(int val) {
    if ((val & 256) != 0) return (Result){0, 3};
    return (Result){1, val ^ 23130};
}

static Result run_pipeline(int val) {
    Result r1 = stage1(val);
    if (!r1.is_ok) return r1;
    Result r2 = stage2(r1.value);
    if (!r2.is_ok) return r2;
    Result r3 = stage3(r2.value);
    if (!r3.is_ok) return r3;
    return r3;
}

int main(void) {
    BenchmarkTimer timer = timer_start();

    int64_t lcg = 2147483647LL;

    // ── Part 1: ADT Pattern Matching (2,000,000 iterations) ──
    int64_t x = 0;
    int64_t y = 0;
    int64_t damage_sum = 0;
    int64_t telemetry_sum = 0;
    int64_t quit_count = 0;

    for (int i = 0; i < 2000000; i++) {
        lcg = (lcg * 1664525LL + 1013904223LL) & 2147483647LL;
        int ev_tag = (int)(lcg % 4);

        Event ev;
        ev.tag = (EventTag)ev_tag;
        if (ev_tag == 0) {
            // Quit has no payload
        } else if (ev_tag == 1) {
            ev.payload.move.dx = (int)((lcg >> 16) & 255);
            ev.payload.move.dy = (int)((lcg >> 8) & 255);
        } else if (ev_tag == 2) {
            ev.payload.damage.target_id = (int)((lcg >> 16) & 1023);
            ev.payload.damage.amount = (int)(lcg & 255);
        } else {
            ev.payload.telemetry.code = (int)((lcg >> 16) & 255);
            ev.payload.telemetry.length = (int)((lcg >> 8) & 127);
            ev.payload.telemetry.flags = (int)(lcg & 31);
        }

        switch (ev.tag) {
            case EVENT_QUIT:
                quit_count += 1;
                break;
            case EVENT_MOVE:
                x += (int64_t)ev.payload.move.dx;
                y += (int64_t)ev.payload.move.dy;
                break;
            case EVENT_DAMAGE:
                damage_sum += (int64_t)(ev.payload.damage.target_id * ev.payload.damage.amount);
                break;
            case EVENT_TELEMETRY:
                telemetry_sum += (int64_t)((ev.payload.telemetry.code ^ ev.payload.telemetry.length) + ev.payload.telemetry.flags);
                break;
        }
    }

    int64_t adt_sum = x + y + damage_sum + telemetry_sum + quit_count;

    // ── Part 2: Safe Error Handling with Result and ? (1,000,000 iterations) ──
    int64_t ok_sum = 0;
    int64_t err_sum = 0;

    for (int j = 0; j < 1000000; j++) {
        lcg = (lcg * 1664525LL + 1013904223LL) & 2147483647LL;
        int val = (int)(lcg & 65535);

        Result res = run_pipeline(val);
        if (res.is_ok) {
            ok_sum += (int64_t)res.value;
        } else {
            err_sum += (int64_t)res.value;
        }
    }

    int64_t try_sum = ok_sum - err_sum;

    // ── Part 3: Niche-Optimized Option Handle (500,000 iterations) ──
    Entity entities[1024];
    uint64_t handles[1024];
    for (int k = 0; k < 1024; k++) {
        entities[k].id = k + 1;
        handles[k] = ((uint64_t)1 << 32) | (uint64_t)k; // odd gen 1, slot k
    }

    int64_t niche_sum = 0;
    for (int m = 0; m < 500000; m++) {
        lcg = (lcg * 1664525LL + 1013904223LL) & 2147483647LL;
        uint64_t opt = 0; // None (niche 0)
        if ((lcg & 3) != 0) {
            int idx = (int)(lcg & 1023);
            opt = handles[idx]; // Some(handle)
        }

        if (opt != 0) {
            uint32_t slot = (uint32_t)(opt & 1023);
            niche_sum += (int64_t)entities[slot].id;
        } else {
            niche_sum -= 42;
        }
    }

    double elapsed_ms = timer_elapsed_ms(timer);

    int64_t checksum = adt_sum + try_sum + niche_sum;

    printf("TIME_MS:%.4f;ADT_SUM:%lld;TRY_SUM:%lld;NICHE_SUM:%lld;CHECKSUM:%lld\n",
           elapsed_ms, (long long)adt_sum, (long long)try_sum, (long long)niche_sum, (long long)checksum);
    return 0;
}
