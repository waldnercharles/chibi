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
GlobalTransform2D render_scene3d_placement(
    const ecs_world *world,
    ecs_entity entity,
    const Transform *position
);
/* Appends the entities the 3D view draws to `entities`: those with a
 * Geometry3D and a Transform, then those with a Geometry3D and a Transform3D.
 * An entity with both transforms comes once, with the second. */
void render_scene3d_entities(ecs_world *world, render_skip skip, ecs_entity **entities);
typedef enum
{
    SCENE3D_FRAME_INVALID, /* the frame is not finite, or the scale is negative */
    SCENE3D_FRAME_FLAT, /* flattened onto a plane, a line or a point: hidden */
    SCENE3D_FRAME_WHOLE,
} scene3d_frame_state;
/* Where an entity's visual stands, before its parts' own placement. With a
 * `transform`, the global 3D transform of the entity's Transform3D, its
 * center sits there and its Geometry3D's elevation is ignored. Without one
 * it stands at `position`, as render_scene3d_placement takes it, raised to
 * that elevation. Its Visual Transform turns and scales it there. The frame
 * is flat when its Visual Transform or a Transform3D placing it scales an
 * axis to zero, however the turns between them round, or when float cannot
 * keep its volume at all. A thin frame that keeps some is whole. */
scene3d_frame_state render_scene3d_visual_frame(
    const ecs_world *world,
    ecs_entity entity,
    const Transform *position,
    const GlobalTransform3D *transform,
    CF_M4x4 *frame
);
/* Returns parts, zero for an invisible/missing visual, or -1 with an authoring error.
 * The parts stand where render_scene3d_visual_frame places them, and a part
 * whose model render_scene3d_visual_state finds flat is left out. An entity
 * with a Mesh Asset has one Box part sized by its Geometry3D, whose model
 * places the returned `mesh`; `mesh` is NULL for other entities and when
 * no part is left. */
int render_scene3d_visuals(
    const ecs_world *world,
    ecs_entity entity,
    const Transform *position,
    const GlobalTransform3D *transform,
    scene3d_visual visuals[SCENE3D_PART_LIMIT],
    const render_mesh_asset **mesh,
    char *error,
    size_t size
);
CF_M4x4 render_scene3d_visual_model(const scene3d_visual *visual);
/* How a visual's model, its frame scaled by its part's dimensions, stands:
 * invalid when it or the frame passes float range, which a part's position
 * or dimensions can carry a finite frame past; flat when a dimension
 * underflows one of a whole frame's axes to zero; whole otherwise. */
scene3d_frame_state render_scene3d_visual_state(const scene3d_visual *visual);
/* Whether a whole frame mirrors space, which turns its faces inside out. It
 * reads its axes' directions alone, so it holds at any scale. */
bool render_scene3d_frame_mirrors(CF_M4x4 frame);
/* Conservative clip test of a box, a transform of the unit cube from -0.5 to
 * 0.5. */
bool render_scene3d_box_visible(CF_M4x4 box, CF_M4x4 view_projection);
/* Conservative clip test of the built-in mesh bounds, including part transforms. */
bool render_scene3d_visual_visible(const scene3d_visual *visual, CF_M4x4 view_projection);
/* The box around a mesh's bounds in its own coordinates, as a transform of
 * the cube from -1 to 1. A flat mesh's box keeps a sliver of thickness. Its
 * axes are half the bounds' size, so any finite bounds fit in a float. */
CF_M4x4 render_scene3d_mesh_extent(const render_mesh_asset *mesh);
/* The box around a mesh's bounds through `model`, as a transform of the unit
 * cube from -0.5 to 0.5, sized as render_scene3d_mesh_extent. */
CF_M4x4 render_scene3d_mesh_box(const render_mesh_asset *mesh, CF_M4x4 model);

#endif
