#include "../core/array.h"
#include "../core/serialize.h"
#include "../core/xalloc.h"
#include "../engine/engine.h"
#include "../game/generated.h"
#include "../game/live.h"
#include "../game/prototype_preview.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path)
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

static game_prototype *prototype(const char *name, const game_prototype_registry *registry)
{
    char path[256], error[256];
    snprintf(path, sizeof path, "content/prototypes/%s.json", name);
    char *text = read_file(path);
    game_prototype *result = game_prototype_load(text, registry, error, sizeof error);
    free(text);
    assert(result);
    return result;
}

static behavior_asset *behavior(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "content/behaviors/%s.json", name);
    char *text = read_file(path);
    behavior_asset *asset = behavior_asset_load_json(text);
    free(text);
    assert(asset);
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(asset, &report);
    for (int i = 0; i < acount(report.items); i++) {
        fprintf(stderr, "%s: %s\n", name, report.items[i].message);
    }
    assert(errors == 0);
    compile_report_free(&report);
    return asset;
}

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    game_prototype_preview *preview;
} host;

static int attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    return game_behavior_attach(user, entity, name);
}

static void tick(host *h, double time)
{
    for (int i = 0; i < 3; i++) {
        assert(
            (h->preview ? game_prototype_preview_tick(h->preview, time)
                        : engine_frame(h->runtime, time)) >= 0
        );
    }
}

static int enemies(host *h, ecs_entity *latest)
{
    ecs_component_id component = GAME_COMPONENT_GameCollider.id;
    ecs_query query;
    assert(ecs_query_begin(h->world, &query, &component, 1) == ECS_OK);
    int count = 0;
    ecs_entity entity;
    if (latest) {
        *latest = (ecs_entity){ 0 };
    }
    while (ecs_query_next(&query, &entity)) {
        if (!(GAME_GameCollider_get_const(h->world, entity)->category & 2)) {
            continue;
        }
        count++;
        if (latest && entity.slot > latest->slot) {
            *latest = entity;
        }
    }
    ecs_query_end(&query);
    return count;
}

static void position(host *h, ecs_entity entity, float x, float y)
{
    const GameTransform *value = GAME_GameTransform_get_const(h->world, entity);
    assert(value && fabsf(value->x - x) < 0.001f && fabsf(value->y - y) < 0.001f);
}

static void run(
    bool isolated,
    const game_prototype_registry *registry,
    game_prototype *encounter,
    game_prototype *player,
    game_prototype *enemy,
    bool restarted
)
{
    host h = { 0 };
    h.preview = isolated ? game_prototype_preview_new(registry) : NULL;
    h.world = h.preview ? game_prototype_preview_world(h.preview) : ecs_world_new();
    if (!isolated) {
        for (int i = 0; i < registry->component_count; i++) {
            assert(ecs_world_register_component(h.world, registry->components[i], NULL) == ECS_OK);
        }
        h.live = game_live_world_new(h.world);
        h.runtime = engine_new(h.world, h.live);
    }
    const char *names[] = { "WaveBehavior", "EnemyBehavior", "HealthBehavior" };
    for (int i = 0; i < 3; i++) {
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
    if (h.preview) {
        assert(game_prototype_preview_register_prototype(h.preview, enemy) == 0);
    } else {
        assert(game_entities_register(h.live->entities, enemy) == 0);
    }
    char error[256];
    for (int launch = 0; launch < 2; launch++) {
        ecs_entity owner;
        if (h.preview) {
            assert(game_prototype_preview_launch(h.preview, encounter, error, sizeof error) == 0);
            owner = game_prototype_preview_entity(h.preview);
        } else {
            assert(
                game_prototype_spawn(
                    encounter,
                    h.world,
                    &(game_prototype_spawn_hooks){ attach, h.live->behaviors },
                    &owner,
                    error,
                    sizeof error
                ) == 0
            );
            assert(engine_set_playing(h.runtime, true) == 0);
        }
        tick(&h, 0);
        tick(&h, 10);
        assert(enemies(&h, NULL) == 0);
        ecs_entity target;
        assert(game_prototype_spawn_components(player, h.world, &target, error, sizeof error) == 0);
        *GAME_GameTransform_get(h.world, target) = (GameTransform){ 100, 25 };
        tick(&h, 11.49);
        assert(enemies(&h, NULL) == 0);
        tick(&h, 11.5);
        ecs_entity latest;
        int first_count = enemies(&h, &latest);
        if (first_count != 1) {
            behavior_instance
                *instance = h.preview
                              ? game_prototype_preview_instance(h.preview, "WaveBehavior")
                              : game_behavior_instance(h.live->behaviors, owner, "WaveBehavior");
            pin_value elapsed, spawned;
            assert(behavior_instance_get_variable(instance, 4, &elapsed) == 0);
            assert(behavior_instance_get_variable(instance, 5, &spawned) == 0);
            fprintf(
                stderr,
                "wave first spawn: isolated=%d launch=%d count=%d elapsed=%.9g "
                "spawned=%d\n",
                isolated,
                launch,
                first_count,
                elapsed.f,
                spawned.i
            );
        }
        assert(first_count == 1);
        position(&h, latest, -160, 25);
        /* The spawned authored Enemy really begins pursuing the player. */
        tick(&h, 11.6);
        position(&h, latest, -151, 25);
        GAME_GameMovement_get(h.world, latest)->speed = 0;
        if (launch || restarted) {
            break;
        }
        *GAME_GameTransform_get(h.world, target) = (GameTransform){ 200, -40 };
        tick(&h, 12.99);
        assert(enemies(&h, NULL) == 1);
        tick(&h, 13);
        assert(enemies(&h, &latest) == 2);
        position(&h, latest, 460, -40);
        GAME_GameMovement_get(h.world, latest)->speed = 0;
        if (h.preview) {
            assert(game_prototype_preview_idle(h.preview, 100) >= 0);
        } else {
            assert(engine_set_playing(h.runtime, false) == 0);
            tick(&h, 100);
        }
        assert(enemies(&h, NULL) == 2);
        if (!h.preview) {
            assert(engine_step(h.runtime) == 0);
        }
        tick(&h, 101.5);
        assert(enemies(&h, &latest) == 3);
        position(&h, latest, -60, -40);
        GAME_GameMovement_get(h.world, latest)->speed = 0;
        if (!h.preview) {
            assert(engine_set_playing(h.runtime, true) == 0);
        }
        tick(&h, 110.5);
        assert(enemies(&h, NULL) == 8);
        tick(&h, 130.5);
        assert(enemies(&h, NULL) == 8);
        if (h.preview) {
            /* This fixture's player is external to preview ownership. */
            assert(ecs_world_destroy_entity(h.world, target) == ECS_OK);
            assert(ecs_world_apply_commands(h.world) == ECS_OK);
            game_prototype_preview_stop(h.preview);
        } else {
            /* Restart the scene's owned runtime and world, preserving authored assets. */
            engine_free(h.runtime);
            game_live_world_free(h.live);
            ecs_world_free(h.world);
            run(false, registry, encounter, player, enemy, true);
            return;
        }
    }
    if (h.preview) {
        game_prototype_preview_free(h.preview);
    } else {
        engine_free(h.runtime);
        game_live_world_free(h.live);
        ecs_world_free(h.world);
    }
}

void test_wave()
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    game_prototype *encounter = prototype("Encounter", &registry);
    game_prototype *player = prototype("Player", &registry);
    game_prototype *enemy = prototype("Enemy", &registry);
    run(false, &registry, encounter, player, enemy, false);
    run(true, &registry, encounter, player, enemy, false);
    game_prototype_free(encounter);
    game_prototype_free(player);
    game_prototype_free(enemy);
    puts("wave PASS: authored spawn cadence, relative alternating positions, eight cap, pursuit, pause and reset");
}
