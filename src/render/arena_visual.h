#ifndef CHIBI_RENDER_ARENA_VISUAL_H
#define CHIBI_RENDER_ARENA_VISUAL_H

#include "ecs/ecs.h"
#include "render/arena_parts.h"
#include "runtime/visual_transform.h"

typedef struct
{
    arena_part part;
    CF_M4x4 frame; /* Local project coordinates to world, before part dimensions. */
} arena_visual;

/* Returns parts, zero for an invisible/missing visual, or -1 with an authoring error. */
int render_arena_visuals(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    arena_visual visuals[ARENA_PART_LIMIT],
    char *error,
    size_t size
);
CF_M4x4 render_arena_visual_model(const arena_visual *visual);
/* Conservative clip test of the built-in mesh bounds, including part transforms. */
bool render_arena_visual_visible(const arena_visual *visual, CF_M4x4 view_projection);

#endif
