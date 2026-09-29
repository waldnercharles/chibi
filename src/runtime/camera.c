#include "runtime/camera.h"

#include "runtime/camera_shake.h"
#include "runtime/generated.h"

#include <math.h>

/* Real seconds the runtime has run, between fixed steps as presented. */
static double real_seconds(const engine *runtime)
{
    const engine_frame_timing *timing = engine_last_timing(runtime);
    return (timing->simulation_steps + timing->alpha) * ENGINE_FIXED_SECONDS;
}

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
        const GameCameraShake *shake = GAME_GameCameraShake_get_const(world, entity);
        if (shake && runtime) {
            position = cf_add(position, game_camera_shake_offset(shake, real_seconds(runtime)));
        }
        *out = (engine_viewport_view){ position.x + camera->offset.x,
                                       -position.y - camera->offset.y,
                                       camera->width,
                                       camera->height };
        if (found || !isfinite(out->center_x) || !isfinite(out->center_y) ||
            !isfinite(out->width) || !isfinite(out->height) ||
            out->width <= 0 || out->height <= 0) {
            found = -1;
            break;
        }
        found = 1;
    }
    ecs_query_end(&query);
    return found;
}
