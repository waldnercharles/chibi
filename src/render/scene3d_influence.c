#include "render/scene3d_influence.h"

#include "platform/cute.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    INFLUENCE_LIMIT = 64
};

typedef struct
{
    ecs_entity entity;
    Transform anchor;
    SurfaceInfluence source;
    float touched;
    bool active;
} influence_sample;

struct render_influences
{
    const ecs_world *world;
    influence_sample samples[INFLUENCE_LIMIT];
    CF_V4 positions[INFLUENCE_LIMIT], colors[INFLUENCE_LIMIT];
    int count;
    float time;
    float scores[INFLUENCE_LIMIT];
    int weakest;
    bool reported_capacity;
};

render_influences *render_influences_new()
{
    return xcalloc(1, sizeof(render_influences));
}

void render_influences_free(render_influences *influences)
{
    free(influences);
}

void render_influences_reset(render_influences *influences)
{
    memset(influences, 0, sizeof *influences);
}

static float remaining(const influence_sample *sample, float time)
{
    if (sample->active) {
        return sample->source.strength;
    }
    if (sample->source.linger <= 0) {
        return 0;
    }
    float fade = fmaxf(0, 1 - (time - sample->touched) / sample->source.linger);
    return sample->source.strength * fade * fade;
}

static float score(const influence_sample *sample, float time)
{
    unsigned color = sample->source.color;
    float peak = fmaxf(
        (float)(color >> 24),
        fmaxf((float)((color >> 16) & 255u), (float)((color >> 8) & 255u))
    );
    return remaining(sample, time) * (peak / 255);
}

static void store_sample(render_influences *influences, int slot, influence_sample sample)
{
    influences->samples[slot] = sample;
    influences->scores[slot] = score(&sample, influences->time);
    influences->weakest = -1;
}

static void retain(render_influences *influences, influence_sample sample)
{
    int slot = -1;
    for (int i = 0; i < influences->count; i++) {
        const influence_sample *previous = &influences->samples[i];
        if (previous->entity.slot == sample.entity.slot &&
            previous->entity.generation == sample.entity.generation &&
            (slot < 0 || previous->touched >= influences->samples[slot].touched)) {
            slot = i;
        }
    }
    if (slot >= 0) {
        influence_sample *previous = &influences->samples[slot];
        float dx = (sample.anchor.x - previous->anchor.x) / sample.source.radius;
        float dz = (sample.anchor.y - previous->anchor.y) / sample.source.radius;
        if (dx * dx + dz * dz <= 0.0625f) {
            /* Keep the stamp anchored; otherwise sub-frame movement never leaves a trail. */
            sample.anchor = previous->anchor;
            store_sample(influences, slot, sample);
            return;
        }
    }
    if (influences->count < INFLUENCE_LIMIT) {
        store_sample(influences, influences->count++, sample);
        return;
    }
    if (!influences->reported_capacity) {
        fprintf(stderr, "Surface influences: %d-sample budget full; retaining strongest remaining colored strength.\n", INFLUENCE_LIMIT);
        influences->reported_capacity = true;
    }
    if (influences->weakest < 0) {
        influences->weakest = 0;
        for (int i = 1; i < influences->count; i++) {
            if (influences->scores[i] < influences->scores[influences->weakest]) {
                influences->weakest = i;
            }
        }
    }
    slot = influences->weakest;
    if (score(&sample, influences->time) > influences->scores[slot]) {
        store_sample(influences, slot, sample);
    }
}

void render_influences_collect(
    render_influences *influences,
    ecs_world *world,
    const engine *runtime,
    float time
)
{
    assert(isfinite(time));
    if (world != influences->world || time < influences->time) {
        render_influences_reset(influences);
    }
    influences->world = world;
    influences->time = time;
    influences->weakest = -1;
    for (int i = 0; i < influences->count; i++) {
        influences->samples[i].active = false;
        influences->scores[i] = score(&influences->samples[i], time);
    }
    ecs_component_id required[] = { COMPONENT_Transform.id,
                                    COMPONENT_SurfaceInfluence.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            Transform position = engine_render_transform(runtime, world, entity).position;
            SurfaceInfluence source = *SurfaceInfluence_get_const(world, entity);
            if (!isfinite(position.x) || !isfinite(position.y) ||
                !isfinite(source.strength) || !isfinite(source.radius) ||
                !isfinite(source.linger) || !isfinite(source.inner_radius) ||
                source.inner_radius < 0 ||
                source.inner_radius >= source.radius || source.strength <= 0 ||
                source.radius <= 0 || source.linger < 0 || !(source.color >> 8)) {
                continue;
            }
            source.strength = fminf(source.strength, 65504);
            retain(influences, (influence_sample){ entity, position, source, time, true });
        }
        ecs_query_end(&query);
    }
    int count = 0;
    for (int i = 0; i < influences->count; i++) {
        influence_sample sample = influences->samples[i];
        float strength = remaining(&sample, time);
        if (strength <= 0) {
            continue;
        }
        influences->samples[count] = sample;
        influences->positions[count] = (CF_V4){ sample.anchor.x,
                                                sample.anchor.y,
                                                sample.source.radius,
                                                sample.source.inner_radius /
                                                    sample.source.radius };
        unsigned color = sample.source.color;
        influences->colors[count] = (CF_V4){ (float)(color >> 24) / 255 * strength,
                                             (float)((color >> 16) & 255u) / 255 * strength,
                                             (float)((color >> 8) & 255u) / 255 * strength,
                                             0 };
        count++;
    }
    influences->count = count;
}

void render_influences_preview(render_influences *influences, const float color[3], float radius)
{
    render_influences_reset(influences);
    influences->positions[0] = (CF_V4){ 0, 0, radius, 0 };
    influences->colors[0] = (CF_V4){ color[0], color[1], color[2], 0 };
    influences->count = 1;
}

void render_influences_bind(render_influences *influences)
{
    CF_V4 count = { .x = (float)influences->count };
    cf_draw3d_set_uniform("scene_influence_count", &count, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("scene_influence_positions", influences->positions, CF_UNIFORM_TYPE_FLOAT4, INFLUENCE_LIMIT);
    cf_draw3d_set_uniform("scene_influence_colors", influences->colors, CF_UNIFORM_TYPE_FLOAT4, INFLUENCE_LIMIT);
}

void render_influences_sample(void *userdata, const float position[3], float color[3])
{
    const render_influences *influences = userdata;
    memset(color, 0, 3 * sizeof *color);
    for (int i = 0; i < influences->count; i++) {
        CF_V4 source = influences->positions[i];
        float dx = (position[0] - source.x) / source.z;
        float dz = (position[2] - source.y) / source.z;
        float squared = dx * dx + dz * dz;
        float fade = fmaxf(0, 1 - squared);
        if (source.w > 0) {
            float middle = (1 + source.w) * .5f;
            float half_width = (1 - source.w) * .5f;
            fade = fmaxf(0, 1 - fabsf(sqrtf(squared) - middle) / half_width);
        }
        CF_V4 tint = influences->colors[i];
        color[0] = fmaxf(color[0], tint.x * fade * fade);
        color[1] = fmaxf(color[1], tint.y * fade * fade);
        color[2] = fmaxf(color[2], tint.z * fade * fade);
    }
}
