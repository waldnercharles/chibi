#include "runtime/camera.h"

#include "runtime/generated.h"

#include <math.h>

int game_camera_view(ecs_world *world, const engine *runtime, engine_viewport_view *out)
{
    if (!ecs_world_component(world, GAME_COMPONENT_GameCamera.id)) {
        return 0;
    }
    ecs_component_id required[] = { GAME_COMPONENT_GameCamera.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 1) != ECS_OK) {
        return 0;
    }
    int found = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameCamera *camera = GAME_GameCamera_get_const(world, entity);
        if (camera->disabled || ecs_entity_is_retiring(world, entity)) {
            continue;
        }
        const GameTransform *transform = GAME_GameTransform_get_const(world, entity);
        if (!transform) {
            found = -1;
            break;
        }
        GameTransform position = *transform;
        if (runtime) {
            engine_render_position(runtime, entity, &position);
        }
        *out = (engine_viewport_view){ position.x + camera->offset.x,
                                       -position.y - camera->offset.y,
                                       camera->width,
                                       camera->height,
                                       camera->focal_distance };
        if (found || !isfinite(out->center_x) || !isfinite(out->center_y) ||
            !isfinite(out->width) || !isfinite(out->height) ||
            out->width <= 0 || out->height <= 0 ||
            !isfinite(out->focal_distance) || out->focal_distance < 0 ||
            (out->focal_distance > 0 && out->focal_distance <= 1)) {
            found = -1;
            break;
        }
        found = 1;
    }
    ecs_query_end(&query);
    return found;
}
