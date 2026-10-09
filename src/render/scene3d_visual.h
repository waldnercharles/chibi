#ifndef CHIBI_RENDER_SCENE3D_VISUAL_H
#define CHIBI_RENDER_SCENE3D_VISUAL_H

#include "ecs/ecs.h"
#include "render/scene3d_parts.h"
#include "runtime/visual_transform.h"

typedef struct
{
    scene3d_part part;
    CF_M4x4 frame; /* Local project coordinates to world, before part dimensions. */
} scene3d_visual;

/* Returns parts, zero for an invisible/missing visual, or -1 with an authoring error. */
int render_scene3d_visuals(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    scene3d_visual visuals[SCENE3D_PART_LIMIT],
    char *error,
    size_t size
);
CF_M4x4 render_scene3d_visual_model(const scene3d_visual *visual);
/* Conservative clip test of the built-in mesh bounds, including part transforms. */
bool render_scene3d_visual_visible(const scene3d_visual *visual, CF_M4x4 view_projection);

#endif
