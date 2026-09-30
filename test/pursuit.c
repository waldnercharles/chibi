#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/support/event_fixture.h"
#include "test/support/report.h"
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

static behavior_asset *read_behavior(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "content/behaviors/%s.yaml", name);
    char *text = read_content(path);
    behavior_asset *asset = behavior_asset_load_json(text);
    free(text);
    assert(asset);
    if (strcmp(name, "EnemyBehavior") == 0) {
        graph *motion = behavior_asset_find_graph_by_name(asset, "Pursuit Velocity");
        assert(motion && motion->kind == GRAPH_FUNCTION);
        node *entry = graph_node_by_id(motion, motion->entry_id);
        node *result = graph_node_by_id(motion, motion->return_id);
        assert(entry && result && entry->output_count == 5 && result->input_count == 1);
        assert(entry->output_pins[0].kind == PIN_DATA && result->input_pins[0].kind == PIN_DATA);
        bool calls_motion = false;
        for (int i = 0; i < acount(asset->graph->nodes); i++) {
            node *n = asset->graph->nodes[i];
            assert(strcmp(n->desc->name, "Break_GameMovement") != 0);
            calls_motion |= n->desc->pure_kind == NODE_PURE_FUNCTION &&
                            n->reference_id == motion->graph_id;
        }
        assert(calls_motion);
        /* Speed reads the movement field, not a whole component to unwrap. */
        node *call = graph_node_by_id(asset->graph, 66);
        const pin_binding *speed = graph_pin_binding(asset->graph, call, 0, 2);
        assert(
            speed && speed->kind == PIN_BINDING_COMPONENT_FIELD &&
            !strcmp(speed->field_name, "speed")
        );
    }
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(asset, &report);
    for (int i = 0; errors && i < acount(report.items); i++) {
        fprintf(stderr, "%s: %s\n", path, report.items[i].message);
    }
    assert(errors == 0);
    compile_report_free(&report);
    return asset;
}

static int attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    return game_behavior_attach(user, entity, name);
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
    char *text = read_content("test/fixtures/content/prototypes/Enemy.yaml");
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
        game_live_world *live = NULL;
        engine *runtime = NULL;
        register_test_preview_catalog(preview);
        if (!isolated) {
            for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
                assert(
                    ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
                );
            }
            live = game_live_world_new(world);
            register_damage_event(live);
            runtime = engine_new(world, live);
        }
        const char *names[] = { "EnemyBehavior", "HealthBehavior" };
        for (int i = 0; i < 2; i++) {
            behavior_asset *asset = read_behavior(names[i]);
            if (isolated) {
                assert(game_prototype_preview_register_behavior(preview, names[i], asset) == 0);
                behavior_asset_free(asset);
                continue;
            }
            assert(
                game_live_add_definition(live, game_live_definition_new(names[i], asset)) == 0
            );
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
                    &(game_prototype_spawn_hooks){ attach, live->behaviors },
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
        game_live_world_free(live);
        ecs_world_free(world);
    }
    game_prototype_free(prototype);
    test_note("pursuit PASS: authored enemy targeting, normalized motion, range, target changes and arrival in both preview hosts\n");
}
