/* SPDX-License-Identifier: GPL-3.0-only */
/* The foreign-call corpus consumes these symbols. No Chibi runtime is linked. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* A single-threaded test observation hook, reset before each observed call. */
static uint64_t trace;
_Static_assert(sizeof(int64_t) == 8, "the slice requires 64-bit integers");
_Static_assert(INT64_MIN == -INT64_MAX - 1, "the slice requires two's complement");

int64_t chibi_slice_echo(int64_t value) { return value; }
bool chibi_slice_not(bool value) { return !value; }
int64_t chibi_slice_clear(void) { trace = 0; return 0; }
int64_t chibi_slice_mark(int64_t value) {
    trace = trace * 10 + (uint64_t)value;
    return value;
}
int64_t chibi_slice_trace(void) {
    int64_t result;
    memcpy(&result, &trace, sizeof(result));
    return result;
}
int64_t chibi_slice_weighted8(int64_t a, int64_t b, int64_t c, int64_t d,
                       int64_t e, int64_t f, int64_t g, int64_t h) {
    uint64_t bits = (uint64_t)a + 2 * (uint64_t)b + 3 * (uint64_t)c + 4 * (uint64_t)d +
                    5 * (uint64_t)e + 6 * (uint64_t)f + 7 * (uint64_t)g + 8 * (uint64_t)h;
    int64_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

#ifdef CHIBI_SLICE_PROBE_SELFTEST
int main(void) {
    if (chibi_slice_echo(INT64_MIN) != INT64_MIN || chibi_slice_echo(INT64_MAX) != INT64_MAX) return 1;
    if (chibi_slice_not(true) || !chibi_slice_not(false)) return 2;
    if (chibi_slice_weighted8(1, 2, 3, 4, 5, 6, 7, 8) != 204) return 3;
    if (chibi_slice_weighted8(INT64_MAX, 1, 0, 0, 0, 0, 0, 0) != INT64_MIN + 1) return 4;
    chibi_slice_clear();
    int64_t first = chibi_slice_mark(1);
    int64_t second = chibi_slice_mark(2);
    if (chibi_slice_weighted8(first, second, 0, 0, 0, 0, 0, 0) != 5 || chibi_slice_trace() != 12) return 5;
    chibi_slice_clear();
    return chibi_slice_trace() != 0;
}
#endif
