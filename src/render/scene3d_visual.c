#include "render/scene3d_visual.h"

#include "runtime/component_catalog.h"
#include "runtime/transform_propagation.h"

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

int render_scene3d_visuals(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    scene3d_visual visuals[SCENE3D_PART_LIMIT],
    const render_mesh_asset **mesh,
    char *error,
    size_t size
)
{
    *mesh = NULL;
    const GameGeometry3D *shape = GAME_GameGeometry3D_get_const(world, entity);
    if (!shape || (!position && !GAME_GameTransform_get_const(world, entity))) {
        return 0;
    }
    GameGlobalTransform2D pose = render_scene3d_placement(world, entity, position);
    position = &pose.position;
    const GameVisualTransform *transform = GAME_GameVisualTransform_get_const(world, entity);
    const GameMeshParts *authored = GAME_GameMeshParts_get_const(world, entity);
    const GameMeshAsset *imported = GAME_GameMeshAsset_get_const(world, entity);
    float angle = pose.radians;
    float scale = transform ? transform->scale : 1;
    CF_V3 rotation = transform
                       ? (CF_V3){ transform->pitch, transform->yaw, transform->roll }
                       : (CF_V3){ 0 };
    if (!isfinite(
            position->x + position->y + shape->elevation + angle + scale +
            rotation.x + rotation.y + rotation.z
        ) ||
        scale < 0) {
        snprintf(error, size, "Visual transform needs finite values and nonnegative scale.");
        return -1;
    }
    if (scale == 0) {
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
    CF_M4x4 frame = cf_m4_from_trs(
        (CF_V3){ position->x, shape->elevation, position->y },
        game_visual_rotation(angle, rotation),
        (CF_V3){ scale, scale, scale }
    );
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
    /* Half the bounds' size, from halves of each, so bounds near the float
     * limit cannot overflow it. The model scales it before it doubles. */
    CF_V3 half = cf_sub_v3(cf_mul_v3_f(mesh->max, .5f), cf_mul_v3_f(mesh->min, .5f));
    float sliver = fmaxf(half.x, fmaxf(half.y, half.z)) * 1e-3f;
    half = cf_max_v3(half, (CF_V3){ sliver, sliver, sliver });
    CF_M4x4 box = cf_m4_from_trs(render_mesh_asset_center(mesh), cf_quat(0, 0, 0, 1), half);
    return cf_mul_m4(cf_mul_m4(model, box), cf_m4_scale((CF_V3){ 2, 2, 2 }));
}
