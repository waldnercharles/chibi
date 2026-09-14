#include "support/xalloc.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Atomic uint64_t allocation_calls;
static _Atomic uint64_t requested_bytes;

static void record_allocation(size_t bytes)
{
    atomic_fetch_add_explicit(&allocation_calls, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&requested_bytes, (uint64_t)bytes, memory_order_relaxed);
}

static void xalloc_oom(void)
{
    fputs("out of memory\n", stderr);
    abort();
}

void *xmalloc(size_t n)
{
    size_t bytes = n ? n : 1;
    void *p = malloc(bytes);
    if (!p) {
        xalloc_oom();
    }
    record_allocation(bytes);
    return p;
}

void *xcalloc(size_t count, size_t size)
{
    size_t actual_count = count ? count : 1;
    size_t actual_size = size ? size : 1;
    void *p = calloc(actual_count, actual_size);
    if (!p) {
        xalloc_oom();
    }
    record_allocation(actual_count * actual_size);
    return p;
}

void *xrealloc(void *p, size_t n)
{
    size_t bytes = n ? n : 1;
    void *q = realloc(p, bytes);
    if (!q) {
        xalloc_oom();
    }
    record_allocation(bytes);
    return q;
}

char *xstrdup(const char *s)
{
    size_t len = strlen(s);
    char *p = (char *)xmalloc(len + 1);
    memcpy(p, s, len + 1);
    return p;
}

void xalloc_stats_get(xalloc_stats *stats)
{
    if (!stats) {
        return;
    }
    stats->calls = atomic_load_explicit(&allocation_calls, memory_order_relaxed);
    stats->requested_bytes = atomic_load_explicit(&requested_bytes, memory_order_relaxed);
}
