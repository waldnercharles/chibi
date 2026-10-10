#include "render/scene3d_visual.h"

#include "runtime/component_catalog.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

GameGlobalTransform2D render_scene3d_placement(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position
)
{
    /* Picking and bounds pass no position and may run before any
     * propagation, so they read the exact world placement. */
    if (!position) {
        return game_transform_world_2d(world, entity);
    }
    const GameGlobalTransform2D *global = GAME_GameGlobalTransform2D_get_const(world, entity);
    return (GameGlobalTransform2D){ *position, global ? global->radians : 0 };
}

void render_scene3d_entities(ecs_world *world, render_skip skip, ecs_entity **entities)
{
    ecs_component_id placements[] = { GAME_COMPONENT_GameTransform.id,
                                      GAME_COMPONENT_GameTransform3D.id };
    for (int i = 0; i < 2; i++) {
        ecs_component_id required[] = { placements[i],
                                        GAME_COMPONENT_GameGeometry3D.id };
        ecs_query query;
        if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
            continue;
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (!render_skip_has(skip, entity) &&
                (i || !GAME_GameTransform3D_has(world, entity))) {
                apush(*entities, entity);
            }
        }
        ecs_query_end(&query);
    }
}

static bool finite_frame(CF_M4x4 frame)
{
    for (int i = 0; i < 16; i++) {
        if (!isfinite(frame.elements[i])) {
            return false;
        }
    }
    return true;
}

/* The signed volume of the box a frame's unit axis directions span: one
 * when they are perpendicular, negative when the frame mirrors and zero
 * when an axis vanishes. Unlike the determinant, it keeps its sign and size
 * at any scale. */
static float direction_volume(CF_M4x4 frame)
{
    CF_V3 axes[3];
    for (int i = 0; i < 3; i++) {
        CF_V3 axis = { frame.elements[i * 4],
                       frame.elements[i * 4 + 1],
                       frame.elements[i * 4 + 2] };
        /* Dividing by the largest part first keeps tiny axes from
         * underflowing on their way to unit length. */
        float largest = fmaxf(fabsf(axis.x), fmaxf(fabsf(axis.y), fabsf(axis.z)));
        if (largest == 0) {
            return 0;
        }
        axes[i] = cf_norm_v3(cf_div_v3_f(axis, largest));
    }
    return cf_dot_v3(axes[0], cf_cross_v3(axes[1], axes[2]));
}

/* Whether the Transform3D of `entity` or of an ancestor placing it scales
 * an axis to zero. */
static bool scaled_to_nothing(const ecs_world *world, ecs_entity entity)
{
    for (ecs_entity at = entity; at.slot; at = game_transform_parent_3d(world, at)) {
        const GameTransform3D *local = GAME_GameTransform3D_get_const(world, at);
        assert(local);
        if (!local->scale.x || !local->scale.y || !local->scale.z) {
            return true;
        }
    }
    return false;
}

bool render_scene3d_frame_mirrors(CF_M4x4 frame)
{
    return direction_volume(frame) < 0;
}

scene3d_frame_state render_scene3d_visual_frame(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    const GameGlobalTransform3D *transform,
    CF_M4x4 *frame
)
{
    const GameVisualTransform *visual = GAME_GameVisualTransform_get_const(world, entity);
    float scale = visual ? visual->scale : 1;
    CF_V3 rotation = visual ? (CF_V3){ visual->pitch, visual->yaw, visual->roll }
                            : (CF_V3){ 0 };
    if (transform) {
        *frame = cf_mul_m4(
            game_transform_matrix_3d(transform),
            cf_m4_from_trs((CF_V3){ 0 }, game_visual_rotation(0, rotation), (CF_V3){ scale, scale, scale })
        );
    } else {
        float elevation = GAME_GameGeometry3D_get_const(world, entity)->elevation;
        GameGlobalTransform2D pose = render_scene3d_placement(world, entity, position);
        *frame = cf_m4_from_trs(
            (CF_V3){ pose.position.x, elevation, pose.position.y },
            game_visual_rotation(pose.radians, rotation),
            (CF_V3){ scale, scale, scale }
        );
    }
    /* A value that is not finite spreads to the frame, and so does a product
     * of finite ones that overflows. */
    if (!finite_frame(*frame) || scale < 0) {
        return SCENE3D_FRAME_INVALID;
    }
    bool flat = !direction_volume(*frame) ||
                (transform && scaled_to_nothing(world, entity));
    return flat ? SCENE3D_FRAME_FLAT : SCENE3D_FRAME_WHOLE;
}

int render_scene3d_visuals(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    const GameGlobalTransform3D *transform,
    scene3d_visual visuals[SCENE3D_PART_LIMIT],
    const render_mesh_asset **mesh,
    char *error,
    size_t size
)
{
    *mesh = NULL;
    const GameGeometry3D *shape = GAME_GameGeometry3D_get_const(world, entity);
    if (!shape ||
        (!transform && !position && !GAME_GameTransform_get_const(world, entity))) {
        return 0;
    }
    const GameMeshParts *authored = GAME_GameMeshParts_get_const(world, entity);
    const GameMeshAsset *imported = GAME_GameMeshAsset_get_const(world, entity);
    CF_M4x4 frame;
    scene3d_frame_state state = render_scene3d_visual_frame(world, entity, position, transform, &frame);
    if (state == SCENE3D_FRAME_INVALID) {
        snprintf(error, size, "Visual transform needs finite values and nonnegative scale.");
        return -1;
    }
    if (state == SCENE3D_FRAME_FLAT) {
        return 0;
    }
    scene3d_parts parts;
    if (!render_scene3d_parts_read(authored ? authored->parts : NULL, &parts, error, size)) {
        return -1;
    }
    if (imported && imported->source && *imported->source) {
        if (parts.count) {
            snprintf(error, size, "Mesh Asset and Mesh Parts cannot be combined on one visual.");
            return -1;
        }
        *mesh = render_mesh_asset_load(imported->source, error, size);
        if (!*mesh) {
            return -1;
        }
        parts.parts[0] = (scene3d_part){
            .primitive = GAME_PRIMITIVE_BOX,
            .dimensions = { shape->width, shape->height, shape->depth }
        };
        parts.count = 1;
    }
    if (!parts.count) {
        parts.parts[0] = (scene3d_part){
            .primitive = shape->primitive,
            .dimensions = { shape->width, shape->height, shape->depth }
        };
        parts.count = 1;
    }
    for (int i = 0; i < parts.count; i++) {
        const scene3d_part *part = &parts.parts[i];
        if (part->primitive < GAME_PRIMITIVE_BOX ||
            part->primitive >= GAME_PRIMITIVE_COUNT ||
            !isfinite(part->dimensions.x + part->dimensions.y + part->dimensions.z) ||
            part->dimensions.x <= 0 || part->dimensions.y <= 0 ||
            part->dimensions.z <= 0) {
            snprintf(error, size, "3D Geometry needs a valid primitive and positive dimensions.");
            return -1;
        }
        CF_M4x4 local = cf_m4_from_trs(
            part->position,
            game_visual_rotation(0, part->rotation),
            (CF_V3){ 1, 1, 1 }
        );
        visuals[i] = (scene3d_visual){ *part, cf_mul_m4(frame, local) };
    }
    return parts.count;
}

CF_M4x4 render_scene3d_visual_model(const scene3d_visual *visual)
{
    CF_V3 dimensions = visual->part.dimensions;
    if (visual->part.primitive == GAME_PRIMITIVE_CAPSULE) {
        dimensions.x *= 2;
        dimensions.z *= 2;
    }
    return cf_mul_m4(visual->frame, cf_m4_scale(dimensions));
}

bool render_scene3d_box_visible(CF_M4x4 box, CF_M4x4 view_projection)
{
    CF_M4x4 clip = cf_mul_m4(view_projection, box);
    /* The wider depth interval also conservatively contains the zero-to-one
     * clip convention. No perspective division: bounds crossing the eye or a
     * clip plane must remain visible. */
    for (int axis = 0; axis < 3; axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            float center = clip.elements[15] + sign * clip.elements[12 + axis];
            float extent = 0;
            for (int column = 0; column < 3; column++) {
                extent += fabsf(
                    clip.elements[column * 4 + 3] + sign * clip.elements[column * 4 + axis]
                );
            }
            if (center + extent / 2 < -0.0001f) {
                return false;
            }
        }
    }
    return true;
}

bool render_scene3d_visual_visible(const scene3d_visual *visual, CF_M4x4 view_projection)
{
    /* Every built-in mesh lies in [-1,1]^3. */
    CF_M4x4 box = cf_mul_m4(render_scene3d_visual_model(visual), cf_m4_scale((CF_V3){ 2, 2, 2 }));
    return render_scene3d_box_visible(box, view_projection);
}

CF_M4x4 render_scene3d_mesh_box(const render_mesh_asset *mesh, CF_M4x4 model)
{
    CF_V3 center = cf_mul_v3_f(cf_add_v3(mesh->min, mesh->max), .5f);
    CF_V3 extent = cf_sub_v3(mesh->max, mesh->min);
    float sliver = fmaxf(extent.x, fmaxf(extent.y, extent.z)) * 1e-3f;
    extent = cf_max_v3(extent, (CF_V3){ sliver, sliver, sliver });
    return cf_mul_m4(model, cf_m4_from_trs(center, cf_quat(0, 0, 0, 1), extent));
}
