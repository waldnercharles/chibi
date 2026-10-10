#ifndef CHIBI_RENDER_SCENE3D_VISUAL_H
#define CHIBI_RENDER_SCENE3D_VISUAL_H

#include "ecs/ecs.h"
#include "render/scene3d_parts.h"
#include "render/skip.h"
#include "runtime/geometry/mesh_asset.h"
#include "runtime/visual_transform.h"

typedef struct
{
    scene3d_part part;
    CF_M4x4 frame; /* Local project coordinates to world, before part dimensions. */
} scene3d_visual;

/* Where an entity with a Transform draws: at the drawn `position` with its
 * propagated angle, or with a NULL position at its exact world transform. */
GameGlobalTransform2D render_scene3d_placement(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position
);
/* Appends the entities the 3D view draws to `entities`: those with a
 * Geometry3D and a Transform, then those with a Geometry3D and a Transform3D.
 * An entity with both transforms comes once, with the second. */
void render_scene3d_entities(ecs_world *world, render_skip skip, ecs_entity **entities);
/* Returns parts, zero for an invisible/missing visual, or -1 with an authoring error.
 * With a `transform`, a global 3D transform, the visual's center sits there
 * and its Geometry3D's elevation is ignored. Without one it draws at
 * `position`, as render_scene3d_placement takes it. An entity with a Mesh
 * Asset has one Box part sized by its Geometry3D, whose model places the
 * returned `mesh`; `mesh` is NULL for other entities. */
int render_scene3d_visuals(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    const GameGlobalTransform3D *transform,
    scene3d_visual visuals[SCENE3D_PART_LIMIT],
    const render_mesh_asset **mesh,
    char *error,
    size_t size
);
CF_M4x4 render_scene3d_visual_model(const scene3d_visual *visual);
/* The determinant of a frame's linear part: negative when it mirrors, and
 * zero when it flattens, which hides a visual. */
float render_scene3d_frame_determinant(CF_M4x4 frame);
/* Conservative clip test of a box, a transform of the unit cube from -0.5 to
 * 0.5. */
bool render_scene3d_box_visible(CF_M4x4 box, CF_M4x4 view_projection);
/* Conservative clip test of the built-in mesh bounds, including part transforms. */
bool render_scene3d_visual_visible(const scene3d_visual *visual, CF_M4x4 view_projection);
/* The box around a mesh's bounds through `model`, as a transform of the unit
 * cube from -0.5 to 0.5. A flat mesh's box keeps a sliver of thickness. */
CF_M4x4 render_scene3d_mesh_box(const render_mesh_asset *mesh, CF_M4x4 model);

#endif
