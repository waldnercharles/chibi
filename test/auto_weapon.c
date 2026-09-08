#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/event_fixture.h"
#include "support/xalloc.h"
#include "test/support/event_fixture.h"

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

static void expect_function_groups(behavior_asset *asset)
{
    const char *names[] = { "Check Firing Interval", "Offset Position" };
    for (int i = 0; i < 2; i++) {
        graph *function = behavior_asset_find_graph_by_name(asset, names[i]);
        assert(function && function->kind == GRAPH_FUNCTION);
        int calls = 0;
        for (int j = 0; j < acount(asset->graphs); j++) {
            graph *caller = asset->graphs[j];
            for (int k = 0; k < acount(caller->nodes); k++) {
                node *n = caller->nodes[k];
                calls += n->desc->pure_kind == NODE_PURE_FUNCTION &&
                         n->reference_id == function->graph_id;
            }
        }
        assert(calls == 1);
        for (int j = 0; j < acount(function->nodes); j++) {
            node *n = function->nodes[j];
            for (int k = 0; k < n->input_count; k++) {
                assert(n->input_pins[k].kind == PIN_DATA);
            }
            for (int k = 0; k < n->output_count; k++) {
                assert(n->output_pins[k].kind == PIN_DATA);
            }
        }
    }
}

static void expect_elapsed(behavior_instance *instance, float expected)
{
    behavior_variable *variable = behavior_asset_find_variable_by_name(instance->bp, "Elapsed");
    pin_value value;
    assert(variable && behavior_instance_get_variable(instance, variable->id, &value) == 0);
    assert(value.type == PIN_F32 && fabsf(value.f - expected) < 0.001f);
}

static behavior_asset *read_behavior(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "content/behaviors/%s.json", name);
    char *text = read_content(path);
    behavior_asset *asset = behavior_asset_load_json(text);
    if (asset) {
        asset = link_test_libraries(asset);
    }
    free(text);
    assert(asset);
    if (strcmp(name, "AutoWeaponBehavior") == 0) {
        expect_function_groups(asset);
        node *initialize = NULL;
        graph *function = behavior_asset_find_graph_by_name(asset, "Fire Projectile");
        assert(function && function->kind == GRAPH_FUNCTION);
        assert(behavior_asset_find_graph_by_name(asset, "Fire Ready Projectiles"));
        for (int i = 0; i < acount(function->nodes); i++) {
            node *n = function->nodes[i];
            if (strcmp(n->desc->name, "SetEntity_GameTransform") == 0) {
                initialize = n;
            }
        }
        assert(initialize && graph_set_breakpoint(function, initialize, true) == 0);
    }
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(asset, &report);
    for (int i = 0; i < acount(report.items); i++) {
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
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameTransform_get(world, entity) = (GameTransform){ x, y };
    *GAME_GameCollider_get(world, entity) = (GameCollider){ .radius = 12,
                                                            .category = 2 };
    return entity;
}

static int shots(ecs_world *world, ecs_entity *latest)
{
    ecs_query query;
    ecs_component_id id = GAME_COMPONENT_GameProjectile.id;
    assert(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    ecs_entity entity;
    int count = 0;
    *latest = (ecs_entity){ 0 };
    while (ecs_query_next(&query, &entity)) {
        count++;
        if (ecs_entity_to_u64(entity) > ecs_entity_to_u64(*latest)) {
            *latest = entity;
        }
    }
    ecs_query_end(&query);
    return count;
}

static void tick(engine *runtime, game_prototype_preview *preview, double time)
{
    assert(
        (preview ? game_prototype_preview_tick(preview, time)
                 : engine_frame(runtime, time)) >= 0
    );
}

static game_prototype *read_prototype(const char *name, const game_prototype_registry *registry)
{
    char path[256], error[256];
    snprintf(path, sizeof path, "content/prototypes/%s.json", name);
    char *text = read_content(path);
    game_prototype *prototype = game_prototype_load(text, registry, error, sizeof error);
    free(text);
    assert(prototype);
    return prototype;
}

static void expect_shot(ecs_world *world, int count, float x, float y, int damage)
{
    ecs_entity shot;
    assert(shots(world, &shot) == count);
    const GameTransform *position = GAME_GameTransform_get_const(world, shot);
    const GameProjectile *projectile = GAME_GameProjectile_get_const(world, shot);
    assert(position && projectile);
    assert(fabsf(position->x - x * 32) < 0.001f && fabsf(position->y - y * 32) < 0.001f);
    assert(
        fabsf(projectile->direction_x - x) < 0.001f &&
        fabsf(projectile->direction_y - y) < 0.001f
    );
    assert(projectile->damage == damage && projectile->speed == 0 && projectile->lifetime == 100);
}

void test_auto_weapon()
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    game_prototype *player = read_prototype("Player", &registry);
    game_prototype *projectile = read_prototype("Projectile", &registry);
    assert(
        game_prototype_write_field(
            projectile,
            "GameProjectile",
            "speed",
            (pin_value){ .type = PIN_F32, .f = 0 }
        ) == 0
    );
    assert(
        game_prototype_write_field(
            projectile,
            "GameProjectile",
            "lifetime",
            (pin_value){ .type = PIN_F32, .f = 100 }
        ) == 0
    );
    for (int isolated = 0; isolated < 2; isolated++) {
        game_prototype_preview *preview = isolated
                                            ? game_prototype_preview_new(&registry)
                                            : NULL;
        register_test_preview_catalog(preview);
        ecs_world *world = preview ? game_prototype_preview_world(preview)
                                   : ecs_world_new();
        game_live_world *live = NULL;
        engine *runtime = NULL;
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
        const char *names[] = {
            "InputBehavior",         "WeaponBehavior",
            "AutoWeaponBehavior",    "HealthBehavior",
            "ProjectileBehavior",    "ProjectileHitBehavior",
            "PlayerContactBehavior", "MovementTrailBehavior"
        };
        for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
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
        ecs_entity owner;
        char error[256];
        if (isolated) {
            assert(game_prototype_preview_register_prototype(preview, projectile) == 0);
            assert(game_prototype_preview_launch(preview, player, error, sizeof error) == 0);
            owner = game_prototype_preview_entity(preview);
        } else {
            assert(game_entities_register(live->entities, projectile) == 0);
            assert(
                game_prototype_spawn(
                    player,
                    world,
                    &(game_prototype_spawn_hooks){ attach, live->behaviors },
                    &owner,
                    error,
                    sizeof error
                ) == 0
            );
            assert(engine_set_playing(runtime, true) == 0);
        }
        behavior_instance *instance = isolated
                                        ? game_prototype_preview_instance(preview, "AutoWeaponBehavior")
                                        : game_behavior_instance(live->behaviors, owner, "AutoWeaponBehavior");
        assert(instance);
        behavior_instance_set_breakpoints_enabled(instance, false);
        *GAME_GameTransform_get(world, owner) = (GameTransform){ 0 };
        GAME_GameWeapon_get(world, owner)->damage = 17;
        ecs_entity latest;
        tick(runtime, preview, 0);
        tick(runtime, preview, 0.5);
        assert(shots(world, &latest) == 0);
        ecs_entity target = target_at(world, 100, 0);
        tick(runtime, preview, 0.6);
        assert(shots(world, &latest) == 0);
        tick(runtime, preview, 0.75);
        expect_shot(world, 1, 1, 0, 17);
        tick(runtime, preview, 0.9);
        assert(shots(world, &latest) == 1);
        expect_elapsed(instance, 0.15f);
        tick(runtime, preview, 1.1);
        expect_shot(world, 2, 1, 0, 17);
        expect_elapsed(instance, 0.10f);
        tick(runtime, preview, 1.25);
        expect_shot(world, 3, 1, 0, 17);
        expect_elapsed(instance, 0);
        *GAME_GameTransform_get(world, target) = (GameTransform){ -100, 0 };
        tick(runtime, preview, 1.5);
        expect_shot(world, 4, -1, 0, 17);
        *GAME_GameTransform_get(world, target) = (GameTransform){ 30, 40 };
        tick(runtime, preview, 1.75);
        expect_shot(world, 5, 0.6f, 0.8f, 17);
        *GAME_GameTransform_get(world, target) = (GameTransform){ 0 };
        tick(runtime, preview, 2.0);
        expect_shot(world, 6, 0, 0, 17);
        assert(ecs_world_destroy_entity(world, target) == ECS_OK);
        tick(runtime, preview, 2.5);
        assert(shots(world, &latest) == 6);
        target = target_at(world, 100, 0);
        behavior_instance_set_breakpoints_enabled(instance, true);
        tick(runtime, preview, 2.75);
        assert(shots(world, &latest) == 7 && acount(instance->activations) == 1);
        flow_run *run = instance->activations[0];
        assert(flow_run_status(run) == FLOW_PAUSED);
        GAME_GameWeapon_get(world, owner)->damage = 999;
        assert(ecs_world_destroy_entity(world, target) == ECS_OK);
        *GAME_GameTransform_get(world, owner) = (GameTransform){ 1000, 1000 };
        assert(flow_run_resume(run) == FLOW_OK);
        tick(runtime, preview, 2.75);
        expect_shot(world, 7, 1, 0, 17);
        behavior_instance_set_breakpoints_enabled(instance, false);
        *GAME_GameTransform_get(world, owner) = (GameTransform){ 0 };
        target_at(world, -100, 0);
        GAME_GameWeapon_get(world, owner)->cooldown = 0;
        tick(runtime, preview, 2.77);
        tick(runtime, preview, 2.77);
        tick(runtime, preview, 2.77);
        expect_shot(world, 9, -1, 0, 999);
        GAME_GameWeapon_get(world, owner)->cooldown = -1;
        tick(runtime, preview, 2.79);
        tick(runtime, preview, 2.79);
        tick(runtime, preview, 2.79);
        expect_shot(world, 11, -1, 0, 999);
        GAME_GameWeapon_get(world, owner)->cooldown = 0.25f;
        tick(runtime, preview, 3.39);
        tick(runtime, preview, 3.39);
        tick(runtime, preview, 3.39);
        expect_shot(world, 13, -1, 0, 999);
        behavior_instance_set_breakpoints_enabled(instance, true);
        tick(runtime, preview, 3.89);
        assert(shots(world, &latest) == 14 && acount(instance->activations) == 1);
        run = instance->activations[0];
        assert(flow_run_status(run) == FLOW_PAUSED);
        GAME_GameWeapon_get(world, owner)->damage = 777;
        *GAME_GameTransform_get(world, owner) = (GameTransform){ 1000, 1000 };
        assert(flow_run_resume(run) >= 0);
        expect_shot(world, 14, -1, 0, 999);
        tick(runtime, preview, 3.89);
        assert(shots(world, &latest) == 15 && flow_run_status(run) == FLOW_PAUSED);
        assert(flow_run_resume(run) >= 0);
        tick(runtime, preview, 3.89);
        expect_shot(world, 15, -1, 0, 999);
        behavior_instance_set_breakpoints_enabled(instance, false);
        *GAME_GameTransform_get(world, owner) = (GameTransform){ 0 };
        tick(runtime, preview, 4.14);
        expect_shot(world, 16, -1, 0, 777);
        if (isolated) {
            game_prototype_preview_free(preview);
            continue;
        }
        engine_free(runtime);
        game_live_world_free(live);
        ecs_world_free(world);
    }
    game_prototype_free(player);
    game_prototype_free(projectile);
    puts("auto weapon PASS: authored target aiming, cooldown remainder, projectile defaults and held initialization in both hosts");
}
