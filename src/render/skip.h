#ifndef CHIBI_RENDER_SKIP_H
#define CHIBI_RENDER_SKIP_H

#include "ecs/ecs.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* Entities a draw or a pick leaves out, as ascending ecs_entity_to_u64
 * values, such as the objects an editor hides. The caller owns them. */
typedef struct render_skip
{
    const uint64_t *entities;
    int count;
} render_skip;

static inline int render_skip_compare(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static inline bool render_skip_has(render_skip skip, ecs_entity entity)
{
    uint64_t id = ecs_entity_to_u64(entity);
    return skip.count &&
           bsearch(&id, skip.entities, (size_t)skip.count, sizeof id, render_skip_compare);
}

#endif
