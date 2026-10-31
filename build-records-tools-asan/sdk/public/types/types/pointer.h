#ifndef CHIBI_POINTER_H
#define CHIBI_POINTER_H

#include "types/value.h"

#include <assert.h>
#include <string.h>

typedef struct pin_pointer
{
    pin_value value;
    struct pin_pointer *base;
    const void *path;
    arena *storage;
    size_t offset;
    struct ecs_component_cell *component;
} pin_pointer;

/* Resolve an array-valued pointer against its current logical root cell.
 * Field projections carry cumulative byte offsets; they never target array
 * entries, so reallocation cannot invalidate the path. */
static inline int pin_pointer_read_value(const pin_pointer *pointer, pin_value *out)
{
    if (!pointer || !out) {
        return -1;
    }
    const pin_pointer *root = pointer;
    size_t offset = 0;
    while (root->base) {
        if (root->offset > SIZE_MAX - offset) {
            return -1;
        }
        offset += root->offset;
        root = root->base;
    }
    if (pointer == root) {
        *out = root->value;
        return 0;
    }
    if (pointer->value.container != PIN_CONTAINER_ARRAY ||
        root->value.container != PIN_CONTAINER_SINGLE ||
        root->value.type != PIN_STRUCT || !root->value.ptr) {
        return -1;
    }
    *out = pointer->value;
    memcpy(&out->ptr, (const char *)root->value.ptr + offset, sizeof out->ptr);
    return 0;
}

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
