#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/event_fixture.h"
#include "support/xalloc.h"
#include "test/support/event_fixture.h"
#include "test/support/simulation_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *engine;
    game_prototype_preview *preview;
    ecs_entity player;
    simulation_fixture clock;
} host;

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    assert(size >= 0);
    rewind(file);
    char *text = xmalloc((size_t)size + 1);
    assert(fread(text, 1, (size_t)size, file) == (size_t)size);
    text[size] = 0;
    fclose(file);
    return text;
}

static behavior_asset *behavior(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "content/behaviors/%s.kdl", name);
    char *text = read_file(path);
    behavior_asset *asset = behavior_asset_load(text);
    if (asset) {
        asset = link_test_libraries(asset);
    }
    free(text);
    assert(asset);
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(asset, &report);
    for (int i = 0; errors && i < acount(report.items); i++) {
        fprintf(stderr, "enemy attack: %s\n", report.items[i].message);
    }
    assert(errors == 0);
    compile_report_free(&report);
    return asset;
}

static int attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    return game_behavior_attach(user, entity, name);
}

static void launch(host *h, const game_prototype *player)
{
    char error[256];
    if (h->preview) {
        assert(game_prototype_preview_launch(h->preview, player, error, sizeof error) == 0);
        h->player = game_prototype_preview_entity(h->preview);
    } else {
        assert(
            game_prototype_spawn(
                player,
                h->world,
                &(game_prototype_spawn_hooks){ attach, h->live->behaviors },
                &h->player,
                error,
                sizeof error
            ) == 0
        );
        assert(engine_set_playing(h->engine, true) == 0);
    }
    *GAME_GameTransform_get(h->world, h->player) = (GameTransform){ 0 };
}

static host create(bool isolated, const game_prototype_registry *registry, const game_prototype *player)
{
    host h = { 0 };
    if (isolated) {
        h.preview = game_prototype_preview_new(registry);
        register_test_preview_catalog(h.preview);
        h.world = game_prototype_preview_world(h.preview);
    } else {
        h.world = ecs_world_new();
        for (int i = 0; i < registry->component_count; i++) {
            assert(ecs_world_register_component(h.world, registry->components[i], NULL) == ECS_OK);
        }
        h.live = game_live_world_new(h.world);
        register_damage_event(h.live);
        h.engine = engine_new(h.world, h.live);
    }
    const char *names[] = { "HealthBehavior", "PlayerContactBehavior" };
    for (int i = 0; i < 2; i++) {
        behavior_asset *asset = behavior(names[i]);
        if (h.preview) {
            assert(game_prototype_preview_register_behavior(h.preview, names[i], asset) == 0);
            behavior_asset_free(asset);
        } else {
            assert(
                game_live_add_definition(h.live, game_live_definition_new(names[i], asset)) == 0
            );
        }
    }
    launch(&h, player);
    h.clock = simulation_fixture_new(h.engine, h.preview);
    return h;
}

static void tick(host *h, double time)
{
    simulation_advance(&h->clock, fmax(time, h->clock.time + ENGINE_FIXED_SECONDS));
}

static void health(host *h, int expected)
{
    const GameHealth *value = GAME_GameHealth_get_const(h->world, h->player);
    assert(value && value->current == expected);
}

static ecs_entity body(host *h, unsigned category, int health)
{
    ecs_entity entity = ecs_world_create_entity(h->world);
    assert(GAME_GameTransform_add(h->world, entity, 0) == ECS_OK);
    assert(GAME_GameCollider_add(h->world, entity, 0) == ECS_OK);
    assert(GAME_GameBody_add(h->world, entity, 0) == ECS_OK);
    assert(GAME_GameHealth_add(h->world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(h->world) == ECS_OK);
    *GAME_GameTransform_get(h->world, entity) = (GameTransform){ 0 };
    *GAME_GameCollider_get(h->world, entity) = (GameCollider){ .radius = 12,
                                                               .category = category,
                                                               .mask = 1,
                                                               .sensor = true };
    *GAME_GameHealth_get(h->world, entity) = (GameHealth){ health, health };
    return entity;
}

static void move(host *h, ecs_entity entity, float x)
{
    *GAME_GameTransform_get(h->world, entity) = (GameTransform){ x, 0 };
}

static void damage(host *h, int amount)
{
    game_behavior_world *world = h->preview
                                   ? game_prototype_preview_behaviors(h->preview)
                                   : h->live->behaviors;
    behavior_instance *instance = game_behavior_instance(world, h->player, "HealthBehavior");
    assert(instance);
    pin_value values[] = { { .type = PIN_I32, .type_name = "int", .i = amount },
                           { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 0 } };
    flow_error error;
    flow_run *run = behavior_instance_dispatch(instance, "Damage", (flow_inputs){ values, 2 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
}

static void attacks(bool isolated, const game_prototype_registry *registry, const game_prototype *player)
{
    host h = create(isolated, registry, player);
    tick(&h, 0);
    ecs_entity enemy = body(&h, 2, 50);
    ecs_entity second = body(&h, 2, 50);
    tick(&h, 0);
    health(&h, 90);
    tick(&h, 0.6);
    health(&h, 90);
    move(&h, enemy, 1000);
    move(&h, second, 1000);
    tick(&h, 0.6);
    move(&h, enemy, 0);
    tick(&h, 0.6);
    health(&h, 80);
    move(&h, enemy, 1000);
    tick(&h, 0.7);
    move(&h, enemy, 0);
    tick(&h, 0.8);
    health(&h, 80);
    tick(&h, 1.2);
    health(&h, 80);
    move(&h, enemy, 1000);
    tick(&h, 1.2);
    move(&h, enemy, 0);
    tick(&h, 1.2);
    health(&h, 70);
    damage(&h, 15);
    tick(&h, 1.2);
    health(&h, 55);
    move(&h, enemy, 1000);
    tick(&h, 1.8);
    GAME_GameCollider_get(h.world, h.player)->mask = 3;
    body(&h, 2, 0);
    body(&h, 2, -1);
    body(&h, 1, 50);
    tick(&h, 1.8);
    health(&h, 55);
    if (h.engine) {
        assert(engine_set_playing(h.engine, false) == 0);
        move(&h, enemy, 0);
        tick(&h, 10);
        health(&h, 55);
        assert(engine_set_playing(h.engine, true) == 0);
        tick(&h, 10);
        health(&h, 45);
    }
    damage(&h, 100);
    tick(&h, 10);
    assert(!ecs_entity_is_alive(h.world, h.player));
    move(&h, enemy, 1000);
    if (h.preview) {
        game_prototype_preview_stop(h.preview);
    } else {
        assert(engine_set_playing(h.engine, false) == 0);
        engine_reset(h.engine);
    }
    launch(&h, player);
    tick(&h, 10);
    health(&h, 100);
    move(&h, enemy, 0);
    tick(&h, 10);
    health(&h, 90);
    if (h.preview) {
        game_prototype_preview_free(h.preview);
        return;
    }
    engine_free(h.engine);
    game_live_world_free(h.live);
    ecs_world_free(h.world);
}

void test_enemy_attack()
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char *text = read_file("test/fixtures/content/prototypes/Player.kdl");
    char error[256];
    game_prototype *player = game_prototype_load(text, &registry, error, sizeof error);
    free(text);
    assert(player);
    assert(game_prototype_remove_graph(player, "InputBehavior") == 0);
    assert(game_prototype_remove_graph(player, "WeaponBehavior") == 0);
    assert(game_prototype_remove_graph(player, "AutoWeaponBehavior") == 0);
    assert(game_prototype_remove_graph(player, "MovementTrailBehavior") == 0);
    attacks(false, &registry, player);
    attacks(true, &registry, player);
    game_prototype_free(player);
    puts("enemy attack PASS: contact-begin cooldown, filtering, damage bypass, both hosts, engine pause and fresh player state");
}
