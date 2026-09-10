#include "render/arena_visual.h"

#include "runtime/generated.h"

#include <math.h>
#include <stdio.h>

int render_arena_visuals(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    arena_visual visuals[ARENA_PART_LIMIT],
    char *error,
    size_t size
)
{
    if (!position) {
        position = GAME_GameTransform_get_const(world, entity);
    }
    const GameGeometry3D *shape = GAME_GameGeometry3D_get_const(world, entity);
    if (!position || !shape) {
        return 0;
    }
    const GameOrientation *heading = GAME_GameOrientation_get_const(world, entity);
    const GameVisualTransform *transform = GAME_GameVisualTransform_get_const(world, entity);
    const GameMeshParts *mesh = GAME_GameMeshParts_get_const(world, entity);
    float angle = heading ? heading->radians : 0;
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
    arena_parts parts;
    if (!render_arena_parts_parse(mesh ? mesh->parts : NULL, &parts, error, size)) {
        return -1;
    }
    if (!parts.count) {
        parts.parts[0] = (arena_part){
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
        const arena_part *part = &parts.parts[i];
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
        visuals[i] = (arena_visual){ *part, cf_mul_m4(frame, local) };
    }
    return parts.count;
}

CF_M4x4 render_arena_visual_model(const arena_visual *visual)
{
    CF_V3 dimensions = visual->part.dimensions;
    if (visual->part.primitive == GAME_PRIMITIVE_CAPSULE) {
        dimensions.x *= 2;
        dimensions.z *= 2;
    }
    return cf_mul_m4(visual->frame, cf_m4_scale(dimensions));
}
