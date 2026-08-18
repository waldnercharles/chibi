#include "arena.h"

#include <stddef.h>
#include <stdlib.h>

void arena_init(arena *a, size_t cap)
{
    a->buf = (unsigned char *)malloc(cap ? cap : 256);
    a->cap = a->buf ? (cap ? cap : 256) : 0;
    a->used = 0;
}

void arena_reset(arena *a) { a->used = 0; }

void *arena_alloc(arena *a, size_t size)
{
    size_t align = _Alignof(max_align_t);
    size_t off = (a->used + align - 1) & ~(align - 1);
    size_t need = off + size;
    if (need > a->cap) {
        size_t ncap = a->cap ? a->cap : 256;
        while (ncap < need) ncap *= 2;
        unsigned char *nb = (unsigned char *)realloc(a->buf, ncap);
        if (!nb) return NULL;
        a->buf = nb;
        a->cap = ncap;
    }
    void *p = a->buf + off;
    a->used = need;
    return p;
}

void arena_free(arena *a)
{
    free(a->buf);
    a->buf = NULL;
    a->cap = 0;
    a->used = 0;
}
