#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} StrBuf;

static void str_init(StrBuf* s) {
    s->capacity = 16;
    s->length = 0;
    s->data = (char*)malloc(16);
    s->data[0] = 0;
}

static void str_append(StrBuf* s, const char* text) {
    size_t tlen = strlen(text);
    if (s->length + tlen + 1 > s->capacity) {
        s->capacity = (s->length + tlen + 1) * 2;
        s->data = (char*)realloc(s->data, s->capacity);
    }
    memcpy(s->data + s->length, text, tlen);
    s->length += tlen;
    s->data[s->length] = 0;
}

static void str_free(StrBuf* s) {
    if (s->data) {
        free(s->data);
        s->data = NULL;
    }
}

static int str_to_int(const char* s) {
    int res = 0;
    int i = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        res = res * 10 + (s[i] - '0');
        ++i;
    }
    return res;
}

int main() {
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    const int N = 500000;
    StrBuf s;
    str_init(&s);

    // 1. Append
    for (int i = 0; i < N; ++i) {
        str_append(&s, "tamarin_");
    }

    // 2. Starts with & Ends with
    int sw = (strncmp(s.data, "tamarin_", 8) == 0);
    int ew = (strcmp(s.data + s.length - 8, "tamarin_") == 0);

    // 3. String to int conversions
    long long numSum = 0;
    const char* numStr = "12345678";
    for (int i = 0; i < 1000000; ++i) {
        numSum += str_to_int(numStr);
    }

    size_t totalLen = s.length;
    str_free(&s);

    QueryPerformanceCounter(&t1);
    double elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    printf("[C String] N=%d | len=%zu | sw=%d | ew=%d | numSum=%lld | time=%.2f ms\n",
           N, totalLen, sw, ew, numSum, elapsed_ms);
    return 0;
}
