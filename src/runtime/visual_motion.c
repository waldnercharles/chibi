#include "runtime/visual_motion.h"

#include "execution/status.h"
#include "runtime/component_catalog.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static bool finite_vector(CF_V3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool valid(const GameVisualMotion *motion)
{
    return finite_vector(motion->rotation) &&
           finite_vector(motion->angular_velocity) &&
           finite_vector(motion->reaction_rotation) && isfinite(motion->speed) &&
           motion->speed >= 0 && isfinite(motion->rest_elevation) &&
           isfinite(motion->reaction_lift) && isfinite(motion->reaction_age) &&
           motion->reaction_age >= 0 && isfinite(motion->reaction_duration) &&
           motion->reaction_duration >= 0;
}

static bool advance(GameVisualMotion *motion, float delta, GameVisualTransform *visual, float *elevation)
{
    motion->rotation = cf_add_v3(
        motion->rotation,
        cf_mul_v3_f(motion->angular_velocity, motion->speed * delta)
    );
    motion->reaction_age = fminf(motion->reaction_age + delta, motion->reaction_duration);
    float phase = motion->reaction_duration > 0
                    ? motion->reaction_age / motion->reaction_duration
                    : 1;
    /* Exact endpoints avoid a residual offset when the pulse completes. */
    float amplitude = phase > 0 && phase < 1 ? sinf(3.141592654f * phase) : 0;
    CF_V3 rotation = cf_add_v3(motion->rotation, cf_mul_v3_f(motion->reaction_rotation, amplitude));
    visual->pitch = rotation.x;
    visual->yaw = rotation.y;
    visual->roll = rotation.z;
    *elevation = motion->rest_elevation + motion->reaction_lift * amplitude;
    return finite_vector(motion->rotation) && finite_vector(rotation) &&
           isfinite(*elevation);
}

int game_visual_motion_tick(ecs_world *world, float delta)
{
    assert(world);
    if (!isfinite(delta) || delta < 0) {
        return FLOW_ERR_ARGUMENT;
    }
    ecs_component_id component = GAME_COMPONENT_GameVisualMotion.id;
    if (!ecs_world_component(world, component)) {
        return FLOW_OK;
    }
    /* Query only the source component so invalid dependencies cannot be
     * silently excluded. The first pass validates before any world mutation. */
    for (int pass = 0; pass < 2; pass++) {
        ecs_query query;
        int result = ecs_query_begin(world, &query, &component, 1);
        if (result != ECS_OK) {
            return result;
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            GameVisualMotion *motion = GAME_GameVisualMotion_get(world, entity);
            GameVisualTransform *visual = GAME_GameVisualTransform_get(world, entity);
            GameGeometry3D *spatial = GAME_GameGeometry3D_get(world, entity);
            if (!visual || !spatial) {
                ecs_query_end(&query);
                return FLOW_ERR_STATE;
            }
            if (!pass) {
                GameVisualMotion next = *motion;
                GameVisualTransform resolved = *visual;
                float elevation;
                if (!valid(motion) || !advance(&next, delta, &resolved, &elevation)) {
                    ecs_query_end(&query);
                    return FLOW_ERR_ARGUMENT;
                }
                continue;
            }
            advance(motion, delta, visual, &spatial->elevation);
        }
        ecs_query_end(&query);
    }
    return FLOW_OK;
}
