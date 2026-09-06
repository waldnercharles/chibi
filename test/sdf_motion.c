#include "../game/sdf_motion.h"

#include "../engine/engine.h"
#include "../game/generated.h"
#include "../game/live.h"
#include "../game/prototype_preview.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

static void close_to(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.0001f);
}

static void motion_math()
{
    GameSdfMotion motion = { 0 };
    CF_V2 result = { 9, 8 };
    float padding;
    assert(game_sdf_motion_warp(&motion, (CF_V2){ 3, 4 }, &result));
    assert(result.x == 3 && result.y == 4);
    assert(game_sdf_motion_padding(&motion, &padding) && padding == 0);
    motion.amplitude = 2;
    motion.angular_frequency = 1.6f;
    assert(game_sdf_motion_warp(&motion, (CF_V2){ 1, 1 }, &result));
    close_to(result.x, 1 + sqrtf(2));
    close_to(result.y, 1 + sqrtf(2));
    assert(game_sdf_motion_warp(&motion, (CF_V2){ 1, 0 }, &result));
    close_to(result.x, 1);
    motion.time = 1.57079632679f;
    motion.temporal_frequency = 1;
    assert(game_sdf_motion_warp(&motion, (CF_V2){ 1, 0 }, &result));
    close_to(result.x, -1);
    assert(game_sdf_motion_padding(&motion, &padding) && padding == 2);
    assert(game_sdf_motion_warp(&motion, (CF_V2){ -0.0001f, -0.0001f }, &result));
    assert(result.x == -0.0001f && result.y == -0.0001f);

    motion = (GameSdfMotion){ .angular_frequency = 4, .pluck_amplitude = 3 };
    float angle = 0.392699081699f;
    CF_V2 point = { cosf(angle), sinf(angle) };
    assert(game_sdf_motion_warp(&motion, point, &result));
    float displacement = 6 * expf(-angle * angle * 1.2f);
    close_to(hypotf(result.x - point.x, result.y - point.y), displacement);
    assert(game_sdf_motion_padding(&motion, &padding) && padding == 6);
    motion.pluck_age = 1;
    motion.pluck_speed = 2;
    assert(game_sdf_motion_warp(&motion, point, &result));
    assert(hypotf(result.x - point.x, result.y - point.y) <= padding + 0.0001f);
    motion.pluck_amplitude = 0.01f;
    assert(game_sdf_motion_padding(&motion, &padding) && padding == 0);
    assert(game_sdf_motion_warp(&motion, point, &result));
    assert(result.x == point.x && result.y == point.y);
    motion.angular_frequency = -1;
    result = (CF_V2){ 9, 8 };
    padding = 7;
    assert(!game_sdf_motion_warp(&motion, point, &result));
    assert(!game_sdf_motion_padding(&motion, &padding));
    assert(result.x == 9 && result.y == 8 && padding == 7);
    motion.angular_frequency = NAN;
    assert(!game_sdf_motion_padding(&motion, &padding));
    motion = (GameSdfMotion){ .amplitude = FLT_MAX, .pluck_amplitude = FLT_MAX };
    assert(!game_sdf_motion_padding(&motion, &padding) && padding == 7);
    motion = (GameSdfMotion){ 0 };
    assert(!game_sdf_motion_warp(&motion, (CF_V2){ INFINITY, 0 }, &result));
}

static void clocks()
{
    ecs_world *world = ecs_world_new();
    assert(game_sdf_motion_tick(world, 1) == ECS_OK);
    assert(game_sdf_motion_tick(world, -1) < 0);
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameSdfMotion_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    GameSdfMotion *motion = GAME_GameSdfMotion_get(world, entity);
    *motion = (GameSdfMotion){ .time = 2, .pluck_age = 3 };
    game_live_world *live = game_live_world_new(world);
    engine *runtime = engine_new(world, live);
    assert(engine_frame(runtime, 0) >= 0);
    assert(engine_set_playing(runtime, true) == 0);
    assert(engine_frame(runtime, 0.25) >= 0);
    close_to(motion->time, 2.25f);
    close_to(motion->pluck_age, 3.25f);
    assert(engine_set_playing(runtime, false) == 0);
    assert(engine_frame(runtime, 5) >= 0);
    close_to(motion->time, 2.25f);
    assert(engine_step(runtime) == 0);
    assert(engine_frame(runtime, 5.5) >= 0);
    close_to(motion->time, 2.75f);
    assert(engine_frame(runtime, 9) >= 0);
    close_to(motion->time, 2.75f);
    ecs_entity invalid = ecs_world_create_entity(world);
    assert(GAME_GameSdfMotion_add(world, invalid, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    GAME_GameSdfMotion_get(world, invalid)->time = NAN;
    assert(game_sdf_motion_tick(world, 1) < 0);
    close_to(GAME_GameSdfMotion_get_const(world, entity)->time, 2.75f);
    assert(game_sdf_motion_tick(world, INFINITY) < 0);
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(world);
}

static void preview_clocks()
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char error[256];
    game_prototype *prototype = game_prototype_load(
        "{\"name\":\"Motion\",\"components\":{\"GameSdfMotion\":{\"time\":2,"
        "\"pluck_age\":3}}}",
        &registry,
        error,
        sizeof error
    );
    assert(prototype);
    game_prototype_preview *preview = game_prototype_preview_new(&registry);
    assert(game_prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
    assert(game_prototype_preview_tick(preview, 0) >= 0);
    assert(game_prototype_preview_tick(preview, 0.25) >= 0);
    const GameSdfMotion *motion = GAME_GameSdfMotion_get_const(
        game_prototype_preview_world(preview),
        game_prototype_preview_entity(preview)
    );
    close_to(motion->time, 2.25f);
    close_to(motion->pluck_age, 3.25f);
    assert(game_prototype_preview_idle(preview, 50) == FLOW_OK);
    close_to(motion->time, 2.25f);
    assert(game_prototype_preview_tick(preview, 50.25) >= 0);
    close_to(motion->time, 2.5f);
    assert(game_prototype_preview_idle(preview, 90) == FLOW_OK);
    assert(game_prototype_preview_tick(preview, 90.5) >= 0);
    close_to(motion->time, 3);
    assert(game_prototype_preview_idle(preview, 100) == FLOW_OK);
    close_to(motion->time, 3);
    assert(game_prototype_preview_idle(preview, NAN) == FLOW_ERR_ARGUMENT);
    game_prototype_preview_stop(preview);
    assert(game_prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
    motion = GAME_GameSdfMotion_get_const(
        game_prototype_preview_world(preview),
        game_prototype_preview_entity(preview)
    );
    close_to(motion->time, 2);
    close_to(motion->pluck_age, 3);
    game_prototype_preview_free(preview);
    game_prototype_free(prototype);
}

void test_sdf_motion()
{
    motion_math();
    clocks();
    preview_clocks();
    puts("sdf motion PASS: radial waves, conservative padding and simulation clock lifecycle");
}
