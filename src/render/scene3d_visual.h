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
/* Where an entity's visual stands, before its parts' own placement. With a
 * `transform`, a global 3D transform, its center sits there and its
 * Geometry3D's elevation is ignored. Without one it stands at `position`, as
 * render_scene3d_placement takes it, raised to that elevation. Its Visual
 * Transform turns and scales it there. False when a value is not finite or
 * the scale is negative. */
bool render_scene3d_visual_frame(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    const GameGlobalTransform3D *transform,
    CF_M4x4 *frame
);
/* Whether a frame flattens space onto a plane, a line or a point, which
 * hides a visual. It reads its axes' directions alone, so a frame of any
 * scale that keeps some volume is whole. */
bool render_scene3d_frame_flat(CF_M4x4 frame);
/* Returns parts, zero for an invisible/missing visual, or -1 with an authoring error.
 * The parts stand where render_scene3d_visual_frame places them. An entity
 * with a Mesh Asset has one Box part sized by its Geometry3D, whose model
 * places the returned `mesh`; `mesh` is NULL for other entities. */
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
/* Whether a whole frame mirrors space, which turns its faces inside out. It
 * reads its axes' directions alone, like render_scene3d_frame_flat. */
bool render_scene3d_frame_mirrors(CF_M4x4 frame);
/* Conservative clip test of a box, a transform of the unit cube from -0.5 to
 * 0.5. */
bool render_scene3d_box_visible(CF_M4x4 box, CF_M4x4 view_projection);
/* Conservative clip test of the built-in mesh bounds, including part transforms. */
bool render_scene3d_visual_visible(const scene3d_visual *visual, CF_M4x4 view_projection);
/* The box around a mesh's bounds through `model`, as a transform of the unit
 * cube from -0.5 to 0.5. A flat mesh's box keeps a sliver of thickness. */
CF_M4x4 render_scene3d_mesh_box(const render_mesh_asset *mesh, CF_M4x4 model);

#endif
