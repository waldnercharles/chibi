#ifndef CBLUEPRINT_ARENA_H
#define CBLUEPRINT_ARENA_H

#include <stddef.h>

typedef struct arena
{
    unsigned char *buf;
    size_t cap;
    size_t used;
} arena;

void arena_init(arena *a, size_t cap);
void arena_reset(arena *a); /* used = 0, keeps the backing buffer */
void *arena_alloc(arena *a, size_t size);
void arena_free(arena *a);

#endif
