#include "support/arena.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

struct arena_block
{
    arena_block *next;
    size_t cap;
    size_t used;
    _Alignas(max_align_t) unsigned char data[];
};
static _Thread_local uint64_t block_allocations;

uint64_t arena_block_allocations()
{
    return block_allocations;
}

static arena_block *new_block(size_t cap)
{
    assert(cap <= SIZE_MAX - sizeof(arena_block));
    arena_block *block = xmalloc(sizeof *block + cap);
    block_allocations++;
    block->next = NULL;
    block->cap = cap;
    block->used = 0;
    return block;
}

void arena_init(arena *a, size_t cap)
{
    a->first = new_block(cap ? cap : 256);
    a->current = a->first;
}

void arena_reset(arena *a)
{
    for (arena_block *block = a->first; block; block = block->next) {
        block->used = 0;
    }
    a->current = a->first;
}

void *arena_alloc(arena *a, size_t size)
{
    if (!a->first) {
        arena_init(a, size > 256 ? size : 256);
    }
    for (;;) {
        arena_block *block = a->current;
        size_t align = _Alignof(max_align_t);
        size_t padding = (align - block->used % align) % align;
        size_t remaining = block->cap - block->used;
        if (padding <= remaining && size <= remaining - padding) {
            void *result = block->data + block->used + padding;
            block->used += padding + size;
            return result;
        }
        if (!block->next) {
            size_t cap = block->cap <= SIZE_MAX / 2 ? block->cap * 2 : size;
            block->next = new_block(cap > size ? cap : size);
        }
        a->current = block->next;
    }
}

void arena_free(arena *a)
{
    arena_block *block = a->first;
    while (block) {
        arena_block *next = block->next;
        free(block);
        block = next;
    }
    *a = (arena){ 0 };
}
