#ifndef CHIBI_POINTER_H
#define CHIBI_POINTER_H

#include "types/value.h"

#include <assert.h>

typedef struct pin_pointer
{
    pin_value value;
    struct pin_pointer *base;
    const void *path;
    arena *storage;
    size_t offset;
    struct ecs_component_cell *component;
} pin_pointer;

/* Derived descriptors identify locations, so repeated projections of the same
 * field compare equal even when their descriptor allocations differ. */
static inline bool pin_pointer_equal(const pin_pointer *a, const pin_pointer *b)
{
    if (!a || !b) {
        return a == b;
    }
    size_t left = 0, right = 0;
    while (a->base) {
        assert(a->offset <= SIZE_MAX - left);
        left += a->offset;
        a = a->base;
    }
    while (b->base) {
        assert(b->offset <= SIZE_MAX - right);
        right += b->offset;
        b = b->base;
    }
    return (a == b || (a->component && a->component == b->component)) && left == right;
}

#endif
