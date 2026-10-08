#include "authoring/engine_preview.h"
#include "authoring/preview.h"
#include "authoring/prototype_preview.h"
#include "graph/document/serialize.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/support/code_fixture.h"
#include "test/support/event_fixture.h"
#include "test/support/simulation_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_content(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = xmalloc((size_t)size + 1);
    assert(fread(text, 1, (size_t)size, file) == (size_t)size);
    text[size] = 0;
    fclose(file);
    return text;
}

static test_source_fixture read_behavior(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "content/behaviors/%s.chibi", name);
    char *text = read_content(path);
    test_source_fixture fixture = test_source_prepare(path, text);
    free(text);
    if (!strcmp(name, "EnemyBehavior")) {
        const script_analysis *analysis = fixture.source->analysis;
        int motion = -1;
        for (int i = 0; i < analysis->function_count; i++) {
            const script_function *function = &analysis->functions[i];
            if (function->symbol >= 0 &&
                !strcmp(analysis->symbols[function->symbol].name, "Pursuit Velocity")) {
                assert(
                    function->pure && function->parameter_count == 5 &&
                    function->result_count == 1
                );
                motion = function->symbol;
            }
        }
        assert(motion >= 0);
        bool calls_motion = false, reads_speed = false;
        for (int i = 0; i < analysis->expression_count; i++) {
            const script_expression *call = &analysis->expressions[i];
            if (call->kind != SCRIPT_EXPR_CALL || call->symbol != motion) {
                continue;
            }
            calls_motion = true;
            for (int j = 0; j < call->argument_count; j++) {
                if (call->arguments[j].parameter != 2) {
                    continue;
                }
                const script_expression
                    *speed = &analysis->expressions[call->arguments[j].value];
                if (speed->kind == SCRIPT_EXPR_UNARY && speed->op == SCRIPT_OP_REFERENCE) {
                    speed = &analysis->expressions[speed->left];
                }
                reads_speed |= speed->kind == SCRIPT_EXPR_MEMBER &&
                               speed->member && speed->left >= 0 &&
                               analysis->expressions[speed->left].kind ==
                                   SCRIPT_EXPR_COMPONENT &&
                               speed->type.storage == PIN_F32;
            }
        }
        assert(calls_motion && reads_speed);
    }
    return fixture;
}

static int attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    game_preview_runtime *runtime = user;
    return game_source_attach(
        runtime->sources,
        entity,
        name,
        runtime->deferred,
        runtime->now,
        runtime->delta
    );
}

static ecs_entity target_at(ecs_world *world, float x, float y)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameTransform_add(world, entity, 0) == ECS_OK);
    assert(GAME_GameCollider_add(world, entity, 0) == ECS_OK);
    assert(GAME_GameBody_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameTransform_get(world, entity) = (GameTransform){ x, y };
    *GAME_GameCollider_get(world, entity) = (GameCollider){ .radius = 12,
                                                            .category = 1 };
    return entity;
}

static void expect_position(ecs_world *world, ecs_entity entity, float x, float y)
{
    const GameTransform *position = GAME_GameTransform_get_const(world, entity);
    if (position &&
        (fabsf(position->x - x) >= 0.001f || fabsf(position->y - y) >= 0.001f)) {
        fprintf(
            stderr,
            "pursuit position: expected (%g, %g), observed (%g, %g)\n",
            x,
            y,
            position->x,
            position->y
        );
    }
    assert(position && fabsf(position->x - x) < 0.001f && fabsf(position->y - y) < 0.001f);
}

void test_pursuit()
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char *text = read_content("test/fixtures/content/prototypes/Enemy.kdl");
    char error[256];
    game_prototype *prototype = game_prototype_load(text, &registry, error, sizeof error);
    free(text);
    assert(prototype);
    for (int isolated = 0; isolated < 2; isolated++) {
        game_prototype_preview *preview = isolated
                                            ? game_prototype_preview_new(&registry)
                                            : NULL;
        ecs_world *world = preview ? game_prototype_preview_world(preview)
                                   : ecs_world_new();
        game_preview_world *live = NULL;
        engine *runtime = NULL;
        register_test_preview_catalog(preview);
        if (!isolated) {
            for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
                assert(
                    ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
                );
            }
            live = game_preview_world_new(world);
            register_damage_event(live);
            runtime = authoring_engine_new(world, live->runtime);
        }
        const char *names[] = { "EnemyBehavior", "HealthBehavior" };
        for (int i = 0; i < 2; i++) {
            test_source_fixture fixture = read_behavior(names[i]);
            test_source_register(
                preview ? game_prototype_preview_live(preview)->runtime : live->runtime,
                &fixture
            );
            test_source_free(&fixture);
        }
        ecs_entity enemy;
        if (isolated) {
            assert(game_prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
            enemy = game_prototype_preview_entity(preview);
        } else {
            assert(
                game_prototype_spawn(
                    prototype,
                    world,
                    &(game_prototype_spawn_hooks){ attach, live->runtime },
                    &enemy,
                    error,
                    sizeof error
                ) == 0
            );
            assert(engine_set_playing(runtime, true) == 0);
        }
        assert(GAME_GameMovement_get_const(world, enemy)->speed == 90.0f);
        *GAME_GameTransform_get(world, enemy) = (GameTransform){ 0 };
        simulation_fixture clock = simulation_fixture_new(runtime, preview);
        simulation_advance(&clock, 0.0);
        simulation_advance(&clock, 0.1);
        expect_position(world, enemy, 0, 0);
        ecs_entity first = target_at(world, 30, 40);
        simulation_advance(&clock, 0.2);
        expect_position(world, enemy, 5.4f, 7.2f);
        *GAME_GameTransform_get(world, first) = *GAME_GameTransform_get(world, enemy);
        simulation_advance(&clock, 0.3);
        expect_position(world, enemy, 5.4f, 7.2f);
        *GAME_GameTransform_get(world, first) = (GameTransform){ 3000, 7.2f };
        simulation_advance(&clock, 0.4);
        expect_position(world, enemy, 5.4f, 7.2f);
        *GAME_GameTransform_get(world, first) = (GameTransform){ 100, 7.2f };
        ecs_entity nearer = target_at(world, -4.6f, 7.2f);
        simulation_advance(&clock, 0.5);
        expect_position(world, enemy, -3.6f, 7.2f);
        simulation_advance(&clock, 0.6);
        expect_position(world, enemy, -4.6f, 7.2f);
        assert(ecs_world_destroy_entity(world, nearer) == ECS_OK);
        simulation_advance(&clock, 0.7);
        expect_position(world, enemy, 4.4f, 7.2f);
        assert(ecs_world_destroy_entity(world, first) == ECS_OK);
        simulation_advance(&clock, 0.8);
        expect_position(world, enemy, 4.4f, 7.2f);
        if (isolated) {
            game_prototype_preview_free(preview);
            continue;
        }
        engine_free(runtime);
        game_preview_world_free(live);
        ecs_world_free(world);
    }
    game_prototype_free(prototype);
    puts("pursuit PASS: authored enemy targeting, normalized motion, range, target changes and arrival in both preview hosts");
}
