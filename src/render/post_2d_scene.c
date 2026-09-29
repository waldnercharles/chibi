#include "render/post_2d_scene.h"

#include "runtime/generated.h"

#include <math.h>

static void collect_look(ecs_world *world, GamePost2D *look)
{
    ecs_component_id component = GAME_COMPONENT_GamePost2D.id;
    if (!ecs_world_component(world, component)) {
        return;
    }
    ecs_query query;
    if (ecs_query_begin(world, &query, &component, 1) != ECS_OK) {
        return;
    }
    ecs_entity entity;
    if (ecs_query_next(&query, &entity)) {
        *look = *GAME_GamePost2D_get_const(world, entity);
    }
    ecs_query_end(&query);
}

static bool in_view(render_view_2d view, float x, float y, float reach)
{
    return x + reach >= view.min_x && x - reach <= view.min_x + view.width &&
           y + reach >= view.max_y - view.height && y - reach <= view.max_y;
}

static void collect_hazes(ecs_world *world, const engine *runtime, render_view_2d view, render_post_2d_distortion *d)
{
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameHeatHaze.id };
    if (!ecs_world_component(world, required[1])) {
        return;
    }
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return;
    }
    ecs_entity entity;
    while (d->haze_count < RENDER_POST_2D_HAZES && ecs_query_next(&query, &entity)) {
        const GameHeatHaze *haze = GAME_GameHeatHaze_get_const(world, entity);
        GameTransform position = *GAME_GameTransform_get_const(world, entity);
        if (runtime) {
            engine_render_position(runtime, entity, &position);
        }
        if (!isfinite(haze->radius + haze->strength + position.x + position.y) ||
            haze->radius <= 0 || haze->strength == 0 ||
            !in_view(view, position.x, -position.y, haze->radius) ||
            !engine_entity_visible(runtime, world, entity)) {
            continue;
        }
        float *h = d->hazes[d->haze_count++];
        h[0] = position.x;
        h[1] = -position.y;
        h[2] = haze->radius;
        h[3] = haze->strength;
    }
    ecs_query_end(&query);
}

static void collect_waves(game_visual_effects_state state, render_view_2d view, render_post_2d_distortion *d)
{
    for (int i = 0; i < state.shockwave_count; i++) {
        game_shockwave wave = state.shockwaves[i];
        float t = fminf(wave.age / wave.duration, 1);
        float grown = 1 - (1 - t) * (1 - t) * (1 - t);
        float fade = (1 - t) * (1 - t);
        float radius = wave.radius * grown;
        if (!in_view(view, wave.position.x, -wave.position.y, radius + 3 * wave.thickness)) {
            continue;
        }
        float *w = d->waves[d->wave_count++];
        w[0] = wave.position.x;
        w[1] = -wave.position.y;
        w[2] = radius;
        w[3] = wave.thickness;
        w[4] = wave.strength * fade;
    }
}

void render_post_2d_collect(
    ecs_world *world,
    const engine *runtime,
    render_view_2d camera,
    render_post_2d_settings *settings,
    render_post_2d_distortion *distortion
)
{
    collect_look(world, &settings->look);
    distortion->view = camera;
    distortion->fluid_haze = fmaxf(settings->look.heat_haze, 0);
    collect_hazes(world, runtime, camera, distortion);
    const game_visual_effects *effects = engine_visual_effects(runtime);
    if (!effects) {
        return;
    }
    game_visual_effects_state state = game_visual_effects_current(effects);
    for (int i = 0; i < 4; i++) {
        settings->flash[i] = state.flash[i];
    }
    settings->look.chromatic_aberration += state.aberration;
    collect_waves(state, camera, distortion);
}
