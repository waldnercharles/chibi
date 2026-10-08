#include "render/sdf_motion.h"

#include "runtime/component_catalog.h"

#include <assert.h>
#include <math.h>

static bool valid(const GameSdfMotion *motion)
{
    assert(motion);
    return isfinite(motion->amplitude) && isfinite(motion->angular_frequency) &&
           motion->angular_frequency >= 0 &&
           isfinite(motion->temporal_frequency) && isfinite(motion->time) &&
           isfinite(motion->pluck_amplitude) && isfinite(motion->pluck_origin) &&
           isfinite(motion->pluck_age) && isfinite(motion->pluck_speed);
}

bool game_sdf_motion_padding(const GameSdfMotion *motion, float *out)
{
    assert(out);
    if (!valid(motion)) {
        return false;
    }
    double padding = motion->amplitude > 0 ? motion->amplitude : 0;
    if (motion->pluck_amplitude > 0.01f) {
        padding += 2.0 * motion->pluck_amplitude;
    }
    float result = (float)padding;
    if (!isfinite(result)) {
        return false;
    }
    *out = result;
    return true;
}

bool game_sdf_motion_warp(const GameSdfMotion *motion, CF_V2 point, CF_V2 *out)
{
    assert(out);
    if (!valid(motion) || !isfinite(point.x) || !isfinite(point.y)) {
        return false;
    }
    if (motion->amplitude <= 0 && motion->pluck_amplitude <= 0) {
        *out = point;
        return true;
    }
    double frequency = round((double)motion->angular_frequency);
    double angle = atan2((double)point.y, (double)point.x);
    double deform = 0;
    if (motion->amplitude > 0) {
        double phase = (double)motion->time * motion->temporal_frequency;
        deform = sin(angle * frequency - phase) * motion->amplitude;
    }
    if (motion->pluck_amplitude > 0.01f) {
        double dist = angle - motion->pluck_origin;
        dist -= 6.2831853 * floor((dist + 3.1415926) / 6.2831853);
        double front = (double)motion->pluck_age * motion->pluck_speed;
        double p1 = dist - front;
        double p2 = dist + front;
        double envelope = 0.8 + frequency * 0.1;
        double wave = sin(dist * frequency - front) * exp(-p1 * p1 * envelope) +
                      sin(dist * frequency + front) * exp(-p2 * p2 * envelope);
        deform += wave * motion->pluck_amplitude;
    }
    double x = (double)point.x + 0.0001f;
    double y = (double)point.y + 0.0001f;
    double length = hypot(x, y);
    CF_V2 result = point;
    if (length > 0) {
        result.x = (float)(point.x + x / length * deform);
        result.y = (float)(point.y + y / length * deform);
    }
    if (!isfinite(result.x) || !isfinite(result.y)) {
        return false;
    }
    *out = result;
    return true;
}

int game_sdf_motion_tick(ecs_world *world, double delta)
{
    assert(world);
    if (!isfinite(delta) || delta < 0) {
        return ECS_ERR_ARGUMENT;
    }
    ecs_component_id component = GAME_COMPONENT_GameSdfMotion.id;
    if (!ecs_world_component(world, component)) {
        return ECS_OK;
    }
    /* Validate before updating so one invalid component cannot partially tick
     * the world. No structure changes occur between these two query passes. */
    for (int pass = 0; pass < 2; pass++) {
        ecs_query query;
        int result = ecs_query_begin(world, &query, &component, 1);
        if (result != ECS_OK) {
            return result;
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            GameSdfMotion *motion = GAME_GameSdfMotion_get(world, entity);
            float time = (float)((double)motion->time + delta);
            float age = (float)((double)motion->pluck_age + delta);
            if (!pass) {
                if (!valid(motion) || !isfinite(time) || !isfinite(age)) {
                    ecs_query_end(&query);
                    return ECS_ERR_ARGUMENT;
                }
                continue;
            }
            motion->time = time;
            motion->pluck_age = age;
        }
        ecs_query_end(&query);
    }
    return ECS_OK;
}
