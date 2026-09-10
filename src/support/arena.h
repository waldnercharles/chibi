#ifndef CHIBI_ARENA_H
#define CHIBI_ARENA_H

#include <stddef.h>

typedef struct arena_block arena_block;
typedef struct arena
{
    arena_block *first;
    arena_block *current;
} arena;

void arena_init(arena *a, size_t cap);
/* Invalidates allocations and retains blocks for reuse. */
void arena_reset(arena *a);
/* Aligned allocations remain at stable addresses until reset or free. */
void *arena_alloc(arena *a, size_t size);
void arena_free(arena *a);

#endif
