#include "graph/document/events.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/value_array.h"
#include "render/particles.h"
#include "render/shader_catalog.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "runtime/visual_motion.h"
#include "runtime/visual_parts.h"
#include "runtime/visual_transform.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "test/support/event_fixture.h"
#include "test/support/simulation_fixture.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static behavior_asset *read_asset(const char *path)
{
    char *json = text_file_read(path);
    assert(json);
    behavior_asset *asset = behavior_asset_load(json);
    if (!asset) {
        fprintf(stderr, "%s: %s\n", path, graph_last_error());
    }
    free(json);
    assert(asset);
    return asset;
}

static behavior_asset *behavior(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "content/behaviors/%s.kdl", name);
    behavior_asset *asset = link_test_libraries(read_asset(path));
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

static game_prototype *prototype(const char *name, const game_prototype_registry *registry)
{
    char path[256], error[256];
    snprintf(path, sizeof path, "content/prototypes/%s.kdl", name);
    char *json = text_file_read(path);
    assert(json);
    game_prototype *result = game_prototype_load(json, registry, error, sizeof error);
    if (!result) {
        fprintf(stderr, "%s: %s\n", path, error);
    }
    free(json);
    assert(result);
    return result;
}

static pin_array *query_entities(behavior_asset *asset, node *query, arena *storage, eval_context *eval)
{
    eval_context_reset_runtime(eval);
    pin_value value;
    assert(graph_eval_output(asset->graph, query, 0, storage, &value, eval) == 0);
    assert(value.type == PIN_HANDLE && value.container == PIN_CONTAINER_ARRAY);
    assert(!strcmp(value.type_name, "ecs.entity"));
    pin_array *result = value.ptr;
    assert(result && result->element_type == PIN_HANDLE);
    assert(!strcmp(result->type_name, "ecs.entity"));
    return result;
}

static bool contains(const pin_array *array, ecs_entity entity)
{
    for (int i = 0; i < array->count; i++) {
        assert(array->items[i].type == PIN_HANDLE);
        assert(!strcmp(array->items[i].type_name, "ecs.entity"));
        if (array->items[i].u64 == ecs_entity_to_u64(entity)) {
            return true;
        }
    }
    return false;
}

static void query_snapshot(ecs_index_kind index)
{
    ecs_world *world = ecs_world_new();
    ecs_component_desc component = GAME_COMPONENT_GameEncounterMember;
    component.index = index;
    assert(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    game_live_world *live = game_live_world_new(world);
    behavior_asset *asset = behavior_asset_new("Find members");
    node *query = graph_add_node(asset->graph, registry_node_by_name("Find Entities With Component"));
    assert(query);
    pin_value key = { .type = PIN_NAME, .str = component.key };
    assert(graph_set_pin_default(asset->graph, query, false, 0, &key) == 0);
    char *json = behavior_asset_save(asset);
    behavior_asset *restored = behavior_asset_load(json);
    assert(restored);
    free(json);
    behavior_asset_free(asset);
    asset = restored;
    query = asset->graph->nodes[0];
    eval_context eval;
    eval_context_init(&eval);
    service_binding *services = NULL;
    apush(services, ((service_binding){ "game.entities", live->entities }));
    eval.services = services;
    arena storage;
    arena_init(&storage, 4096);
    assert(query_entities(asset, query, &storage, &eval)->count == 0);
    ecs_entity first = ecs_world_create_entity(world);
    ecs_entity second = ecs_world_create_entity(world);
    assert(GAME_GameEncounterMember_add(world, first, 0) == ECS_OK);
    assert(GAME_GameEncounterMember_add(world, second, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    pin_array *snapshot = query_entities(asset, query, &storage, &eval);
    assert(snapshot->count == 2 && contains(snapshot, first) && contains(snapshot, second));
    game_entities_request_destroy(live->entities, first);
    pin_array *current = query_entities(asset, query, &storage, &eval);
    assert(current->count == 1 && contains(current, second));
    assert(ecs_world_record_destroy(world, second, 0) == ECS_OK);
    assert(query_entities(asset, query, &storage, &eval)->count == 0);
    assert(snapshot->count == 2 && contains(snapshot, first) && contains(snapshot, second));
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(game_entities_flush(live->entities) == 0);
    ecs_entity replacement = ecs_world_create_entity(world);
    assert(
        replacement.generation != first.generation || replacement.slot != first.slot
    );
    assert(GAME_GameEncounterMember_add(world, replacement, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    current = query_entities(asset, query, &storage, &eval);
    assert(current->count == 1 && contains(current, replacement));
    assert(!contains(snapshot, replacement));
    assert(GAME_GameEncounterMember_remove(world, replacement, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(query_entities(asset, query, &storage, &eval)->count == 0);
    key.str = "missing.Component";
    assert(graph_set_pin_default(asset->graph, query, false, 0, &key) == 0);
    eval_context_reset_runtime(&eval);
    pin_value value;
    assert(graph_eval_output(asset->graph, query, 0, &storage, &value, &eval) < 0);
    assert(strstr(eval_context_status(&eval)->message, "registered component"));
    eval_context_free(&eval);
    afree(services);
    arena_free(&storage);
    behavior_asset_free(asset);
    game_live_world_free(live);
    ecs_world_free(world);
}

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    game_prototype_preview *preview;
    simulation_fixture clock;
    ecs_entity manager, player;
} encounter_host;

static int attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    return game_behavior_attach(user, entity, name);
}

static game_behavior_world *behaviors(encounter_host *h)
{
    return h->preview ? game_prototype_preview_behaviors(h->preview) : h->live->behaviors;
}

static ecs_entity spawn(encounter_host *h, const game_prototype *prototype)
{
    ecs_entity entity;
    char error[256];
    assert(
        game_prototype_spawn(
            prototype,
            h->world,
            &(game_prototype_spawn_hooks){ attach, behaviors(h) },
            &entity,
            error,
            sizeof error
        ) == 0
    );
    return entity;
}

static void keep_health(game_prototype *prototype)
{
    /* Movement and visual feedback are covered elsewhere; these fixtures retain
     * the supplied membership, health and authored death behavior. */
    for (int i = acount(prototype->graphs) - 1; i >= 0; i--) {
        if (strcmp(prototype->graphs[i], "HealthBehavior")) {
            assert(game_prototype_remove_graph(prototype, prototype->graphs[i]) == 0);
        }
    }
}

static encounter_host create_with_delay(bool preview, float delay)
{
    static game_prototype_registry registry;
    registry = (game_prototype_registry){ GAME_REGISTRY_COMPONENTS,
                                          GAME_REGISTRY_COMPONENT_COUNT };
    encounter_host h = { 0 };
    h.preview = preview ? game_prototype_preview_new(&registry) : NULL;
    h.world = h.preview ? game_prototype_preview_world(h.preview) : ecs_world_new();
    if (h.preview) {
        register_test_preview_catalog(h.preview);
    } else {
        for (int i = 0; i < registry.component_count; i++) {
            assert(ecs_world_register_component(h.world, registry.components[i], NULL) == ECS_OK);
        }
        h.live = game_live_world_new(h.world);
        register_damage_event(h.live);
        h.runtime = engine_new(h.world, h.live);
    }
    const char *names[] = { "HealthBehavior",
                            "ArenaWaveBehavior",
                            "WavePulseBehavior",
                            "SpawnTelegraphBehavior" };
    for (int i = 0; i < 4; i++) {
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
    game_prototype *pulse = prototype("ArenaWavePulse", &registry);
    assert(
        (h.preview ? game_prototype_preview_register_prototype(h.preview, pulse)
                   : game_entities_register(h.live->entities, pulse)) == 0
    );
    game_prototype_free(pulse);
    game_prototype *marker = prototype("ArenaSpawnMarker", &registry);
    assert(
        game_prototype_write_field(
            marker,
            "GameSpawnRequest",
            "delay",
            (pin_value){ .type = PIN_F32, .f = delay }
        ) == 0
    );
    assert(
        (h.preview ? game_prototype_preview_register_prototype(h.preview, marker)
                   : game_entities_register(h.live->entities, marker)) == 0
    );
    game_prototype_free(marker);
    game_prototype *enemy = prototype("ArenaEnemy", &registry);
    keep_health(enemy);
    assert(
        (h.preview ? game_prototype_preview_register_prototype(h.preview, enemy)
                   : game_entities_register(h.live->entities, enemy)) == 0
    );
    game_prototype_free(enemy);
    game_prototype *player = prototype("ArenaPlayer", &registry);
    keep_health(player);
    h.player = spawn(&h, player);
    assert(GAME_GameEncounterMember_get_const(h.world, h.player)->role == GAME_ENCOUNTER_PLAYER);
    game_prototype_free(player);
    game_prototype *encounter = prototype("ArenaEncounter", &registry);
    assert(
        game_prototype_write_field(
            encounter,
            "GameEncounter",
            "total",
            (pin_value){ .type = PIN_I32, .i = 3 }
        ) == 0
    );
    if (h.preview) {
        char error[256];
        assert(game_prototype_preview_launch(h.preview, encounter, error, sizeof error) == 0);
        h.manager = game_prototype_preview_entity(h.preview);
    } else {
        h.manager = spawn(&h, encounter);
    }
    game_prototype_free(encounter);
    h.clock = simulation_fixture_new(h.runtime, h.preview);
    behavior_instance *wave = game_behavior_instance(behaviors(&h), h.manager, "ArenaWaveBehavior");
    behavior_variable *interval = behavior_asset_find_variable_by_name(wave->asset, "Interval");
    assert(interval);
    assert(
        behavior_instance_set_variable(
            wave,
            interval->id,
            &(pin_value){ .type = PIN_F32, .type_name = "float", .f = .1f }
        ) == 0
    );
    behavior_variable *elapsed = behavior_asset_find_variable_by_name(wave->asset, "Elapsed");
    assert(elapsed);
    assert(
        behavior_instance_set_variable(
            wave,
            elapsed->id,
            &(pin_value){ .type = PIN_F32, .type_name = "float", .f = 0 }
        ) == 0
    );
    simulation_frame(&h.clock, 0);
    return h;
}

static encounter_host create(bool preview)
{
    return create_with_delay(preview, .05f);
}

static void destroy_host(encounter_host *h)
{
    if (h->preview) {
        game_prototype_preview_free(h->preview);
        return;
    }
    engine_free(h->runtime);
    game_live_world_free(h->live);
    ecs_world_free(h->world);
}

static GameEncounter state(encounter_host *h)
{
    return *GAME_GameEncounter_get_const(h->world, h->manager);
}

static float elapsed(encounter_host *h)
{
    behavior_instance *wave = game_behavior_instance(behaviors(h), h->manager, "ArenaWaveBehavior");
    behavior_variable *variable = behavior_asset_find_variable_by_name(wave->asset, "Elapsed");
    pin_value value;
    assert(variable && behavior_instance_get_variable(wave, variable->id, &value) == 0);
    return value.f;
}

static ecs_entity *enemies(encounter_host *h)
{
    ecs_entity *result = NULL;
    ecs_query query;
    ecs_component_id id = GAME_COMPONENT_GameEncounterMember.id;
    assert(ecs_query_begin(h->world, &query, &id, 1) == ECS_OK);
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (GAME_GameEncounterMember_get_const(h->world, entity)->role == GAME_ENCOUNTER_ENEMY &&
            GAME_GameHealth_has(h->world, entity)) {
            apush(result, entity);
        }
    }
    ecs_query_end(&query);
    return result;
}

static void damage(encounter_host *h, ecs_entity target, int amount)
{
    pin_value values[] = { { .type = PIN_I32, .type_name = "int", .i = amount },
                           { .type = PIN_HANDLE,
                             .type_name = "ecs.entity",
                             .u64 = ecs_entity_to_u64(h->player) } };
    game_entities *entities = h->preview ? NULL : h->live->entities;
    if (entities) {
        assert(
            game_entities_request_event(entities, target, "Damage", (flow_inputs){ values, 2 }) == 0
        );
        return;
    }
    assert(
        game_behavior_dispatch(behaviors(h), target, "Damage", (flow_inputs){ values, 2 }, NULL) >= 0
    );
}

static void clear_encounter(bool preview)
{
    encounter_host h = create(preview);
    simulation_advance(&h.clock, .08);
    assert(state(&h).spawned == 0 && state(&h).status == GAME_ENCOUNTER_ACTIVE);
    assert(GAME_GameInput_remove(h.world, h.player, 0) == ECS_OK);
    assert(ecs_world_apply_commands(h.world) == ECS_OK);
    simulation_advance(&h.clock, .35);
    GameEncounter s = state(&h);
    assert(s.status == GAME_ENCOUNTER_ACTIVE && s.total == 3 && s.spawned == 3 && s.alive == 3);
    float stopped = elapsed(&h);
    simulation_advance(&h.clock, .55);
    assert(elapsed(&h) == stopped); /* Finishing spawns also stops cadence accumulation. */
    ecs_entity *targets = enemies(&h);
    assert(acount(targets) == 3);
    damage(&h, targets[0], 30);
    simulation_step(&h.clock);
    assert(!ecs_entity_is_alive(h.world, targets[0]));
    assert(state(&h).alive == 2 && state(&h).status == GAME_ENCOUNTER_ACTIVE);
    damage(&h, targets[1], 30);
    simulation_step(&h.clock);
    assert(state(&h).alive == 1 && state(&h).status == GAME_ENCOUNTER_ACTIVE);
    damage(&h, targets[2], 30);
    damage(&h, targets[2], 30); /* Duplicate queued lethal damage cannot count twice. */
    simulation_step(&h.clock);
    assert(state(&h).alive == 0 && state(&h).status == GAME_ENCOUNTER_CLEARED);
    simulation_advance(&h.clock, h.clock.time + .5);
    assert(state(&h).spawned == 3 && state(&h).alive == 0 && elapsed(&h) == stopped);
    afree(targets);
    destroy_host(&h);
}

static void defeat_encounter(bool preview, bool simultaneous)
{
    encounter_host h = create(preview);
    simulation_advance(&h.clock, simultaneous ? .35 : .2);
    ecs_entity *targets = enemies(&h);
    assert(acount(targets) == 3);
    int spawned = state(&h).spawned;
    float stopped = elapsed(&h);
    if (simultaneous) {
        for (int i = 0; i < acount(targets); i++) {
            damage(&h, targets[i], 30);
        }
    }
    damage(&h, h.player, 1000);
    simulation_step(&h.clock);
    assert(!ecs_entity_is_alive(h.world, h.player));
    assert(state(&h).status == GAME_ENCOUNTER_DEFEATED);
    assert(state(&h).alive == (simultaneous ? 0 : 3));
    assert(state(&h).spawned == spawned);
    simulation_advance(&h.clock, h.clock.time + .5);
    assert(state(&h).spawned == spawned && elapsed(&h) == stopped);
    afree(targets);
    destroy_host(&h);
    h = create(preview);
    assert(
        state(&h).status == GAME_ENCOUNTER_ACTIVE && state(&h).spawned == 0 &&
        state(&h).alive == 0
    );
    simulation_advance(&h.clock, .12);
    assert(state(&h).spawned == 3 && state(&h).alive == 3);
    destroy_host(&h);
}

static void cross_batch_defeat()
{
    encounter_host h = create(false);
    simulation_advance(&h.clock, .35);
    ecs_entity *targets = enemies(&h);
    assert(acount(targets) == 3);
    damage(&h, targets[0], 30);
    damage(&h, targets[1], 30);
    simulation_step(&h.clock);
    assert(state(&h).alive == 1 && state(&h).status == GAME_ENCOUNTER_ACTIVE);
    damage(&h, targets[2], 30);
    for (int i = 0; i < 64; i++) {
        damage(&h, h.player, 0);
    }
    damage(&h, h.player, 1000);
    simulation_step(&h.clock);
    assert(!ecs_entity_is_alive(h.world, h.player));
    assert(!ecs_entity_is_alive(h.world, targets[2]));
    assert(state(&h).alive == 0 && state(&h).status == GAME_ENCOUNTER_DEFEATED);
    assert(engine_last_timing(h.clock.runtime)->event_backlog == 0);
    afree(targets);
    destroy_host(&h);

    h = create(false);
    assert(GAME_GameHealth_remove(h.world, h.player, 0) == ECS_OK);
    assert(ecs_world_apply_commands(h.world) == ECS_OK);
    simulation_step(&h.clock);
    assert(state(&h).status == GAME_ENCOUNTER_DEFEATED);
    destroy_host(&h);
}

static behavior_asset *damage_probe()
{
    behavior_asset *definition = read_asset("content/events/Damaged.kdl");
    behavior_asset *asset = behavior_asset_new("Accepted damage probe");
    uint64_t count = behavior_asset_add_variable(asset, "Count", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                         ->id;
    uint64_t amount = behavior_asset_add_variable(asset, "Amount", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                          ->id;
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(graph_event_apply(asset->graph, event, definition) == 0);
    node *set = behavior_asset_add_set_node(asset, amount);
    assert(graph_connect_exec(asset->graph, event, 0, set, 0, 0) == 0);
    assert(graph_connect_data(asset->graph, event, 1, set, 1) == 0);
    node *get = behavior_asset_add_get_node(asset, count);
    node *add = graph_add_node(asset->graph, registry_node_by_name("Add"));
    assert(graph_set_pin_type(asset->graph, add, false, 0, PIN_I32, "int") >= 0);
    assert(graph_set_pin_type(asset->graph, add, false, 1, PIN_I32, "int") >= 0);
    assert(graph_set_pin_type(asset->graph, add, true, 0, PIN_I32, "int") >= 0);
    assert(
        graph_set_pin_default(asset->graph, add, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 }) == 0
    );
    assert(graph_connect_data(asset->graph, get, 0, add, 0) == 0);
    node *increment = behavior_asset_add_set_node(asset, count);
    assert(graph_connect_data(asset->graph, add, 0, increment, 1) == 0);
    assert(graph_connect_exec(asset->graph, set, 0, increment, 0, 0) == 0);
    behavior_asset_free(definition);
    node *log = graph_add_node(asset->graph, registry_node_by_name("Print String"));
    assert(
        graph_connect_data_conversion(asset->graph, event, 1, log, 1, registry_node_by_name("To String")) == 0
    );
    assert(graph_connect_exec(asset->graph, increment, 0, log, 0, 0) == 0);
    definition = read_asset("content/events/Died.kdl");
    event = graph_add_node(asset->graph, &NODE_Event);
    assert(graph_event_apply(asset->graph, event, definition) == 0);
    log = graph_add_node(asset->graph, registry_node_by_name("Print String"));
    assert(
        graph_set_pin_default(asset->graph, log, false, 1, &(pin_value){ .type = PIN_STR, .str = "died" }) == 0
    );
    assert(graph_connect_exec(asset->graph, event, 0, log, 0, 0) == 0);
    behavior_asset_free(definition);
    return asset;
}

static int probe_value(behavior_instance *instance, const char *name)
{
    behavior_variable *variable = behavior_asset_find_variable_by_name(instance->asset, name);
    pin_value value;
    assert(variable && behavior_instance_get_variable(instance, variable->id, &value) == 0);
    return value.i;
}

static void accepted_damage()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    register_damage_event(live);
    assert(
        game_live_add_definition(
            live,
            game_live_definition_new("HealthBehavior", behavior("HealthBehavior"))
        ) == 0
    );
    assert(
        game_live_add_definition(
            live,
            game_live_definition_new("Accepted damage probe", damage_probe())
        ) == 0
    );
    engine *runtime = engine_new(world, live);
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameHealth_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameHealth_get(world, entity) = (GameHealth){ 100, 100 };
    assert(game_behavior_attach(live->behaviors, entity, "HealthBehavior") == 0);
    assert(game_behavior_attach(live->behaviors, entity, "Accepted damage probe") == 0);
    behavior_instance *probe = game_behavior_instance(live->behaviors, entity, "Accepted damage probe");
    simulation_fixture clock = simulation_fixture_new(runtime, NULL);
    simulation_frame(&clock, 0);
    pin_value values[] = { { .type = PIN_I32, .type_name = "int" },
                           { .type = PIN_HANDLE, .type_name = "ecs.entity" } };
    const int amounts[] = { 0, -5, 5, 10, 7 };
    for (int i = 0; i < 5; i++) {
        if (i == 3) {
            assert(GAME_GameDash_add(world, entity, 0) == ECS_OK);
            assert(ecs_world_apply_commands(world) == ECS_OK);
            GAME_GameDash_get(world, entity)->remaining = .2f;
        }
        if (i == 4) {
            GAME_GameDash_get(world, entity)->remaining = 0;
        }
        values[0].i = amounts[i];
        assert(
            game_entities_request_event(live->entities, entity, "Damage", (flow_inputs){ values, 2 }) == 0
        );
        simulation_step(&clock);
        int expected = i < 2 ? 100 : i < 4 ? 95 : 88;
        assert(GAME_GameHealth_get_const(world, entity)->current == expected);
        assert(probe_value(probe, "Count") == (i < 2 ? 0 : i < 4 ? 1 : 2));
        assert(probe_value(probe, "Amount") == (i < 2 ? 0 : i < 4 ? 5 : 7));
    }
    *GAME_GameHealth_get(world, entity) = (GameHealth){ 16777217, 16777217 };
    values[0].i = 1;
    assert(
        game_entities_request_event(live->entities, entity, "Damage", (flow_inputs){ values, 2 }) == 0
    );
    simulation_step(&clock);
    assert(GAME_GameHealth_get_const(world, entity)->current == 16777216);
    assert(probe_value(probe, "Amount") == 1 && probe_value(probe, "Count") == 3);
    values[0].i = INT_MAX;
    for (int i = 0; i < 3; i++) {
        assert(
            game_entities_request_event(live->entities, entity, "Damage", (flow_inputs){ values, 2 }) == 0
        );
    }
    simulation_step(&clock);
    assert(!ecs_entity_is_alive(world, entity));
    assert(game_behavior_log_count(live->behaviors) == 5);
    game_behavior_log_info hit, died;
    assert(game_behavior_log_info_at(live->behaviors, 3, &hit) == 0);
    assert(game_behavior_log_info_at(live->behaviors, 4, &died) == 0);
    assert(!strcmp(hit.message, "16777216") && !strcmp(died.message, "died"));
    assert(ecs_entity_to_u64(hit.entity) == ecs_entity_to_u64(entity));
    assert(ecs_entity_to_u64(died.entity) == ecs_entity_to_u64(entity));
    /* No Encounter or Member component was needed for ordinary health. */
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(world);
}

static int feedback_pulses(ecs_world *world, float radius, ecs_entity *found)
{
    ecs_query query;
    ecs_component_id component = GAME_COMPONENT_GameSurfaceInfluence.id;
    assert(ecs_query_begin(world, &query, &component, 1) == ECS_OK);
    ecs_entity entity;
    int count = 0;
    while (ecs_query_next(&query, &entity)) {
        const GameSurfaceInfluence *pulse = GAME_GameSurfaceInfluence_get_const(world, entity);
        if (pulse->radius == radius) {
            *found = entity;
            count++;
        }
    }
    ecs_query_end(&query);
    return count;
}

static void authored_feedback()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    register_damage_event(live);
    const char *behaviors[] = { "HealthBehavior",
                                "SurfacePulseBehavior",
                                "ArenaImpactFeedback" };
    for (int i = 0; i < 3; i++) {
        assert(
            game_live_add_definition(
                live,
                game_live_definition_new(behaviors[i], behavior(behaviors[i]))
            ) == 0
        );
    }
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    const char *effects[] = { "ArenaHitPulse", "ArenaDeathPulse" };
    for (int i = 0; i < 2; i++) {
        game_prototype *effect = prototype(effects[i], &registry);
        assert(game_entities_register(live->entities, effect) == 0);
        game_prototype_free(effect);
    }
    engine *runtime = engine_new(world, live);
    ecs_entity target = ecs_world_create_entity(world);
    assert(GAME_GameTransform_add(world, target, 0) == ECS_OK);
    assert(GAME_GameHealth_add(world, target, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameTransform_get(world, target) = (GameTransform){ 127, -83 };
    *GAME_GameHealth_get(world, target) = (GameHealth){ 30, 30 };
    assert(game_behavior_attach(live->behaviors, target, "HealthBehavior") == 0);
    assert(game_behavior_attach(live->behaviors, target, "ArenaImpactFeedback") == 0);
    simulation_fixture clock = simulation_fixture_new(runtime, NULL);
    simulation_frame(&clock, 0);
    pin_value damage[] = { { .type = PIN_I32, .type_name = "int" },
                           { .type = PIN_HANDLE, .type_name = "ecs.entity" } };
    assert(
        game_entities_request_event(live->entities, target, "Damage", (flow_inputs){ damage, 2 }) == 0
    );
    simulation_step(&clock);
    ecs_entity hit = { 0 }, death = { 0 };
    assert(feedback_pulses(world, 42, &hit) == 0);
    damage[0].i = 10;
    assert(
        game_entities_request_event(live->entities, target, "Damage", (flow_inputs){ damage, 2 }) == 0
    );
    simulation_step(&clock);
    assert(GAME_GameHealth_get_const(world, target)->current == 20);
    assert(feedback_pulses(world, 42, &hit) == 1);
    const GameTransform *position = GAME_GameTransform_get_const(world, hit);
    assert(position->x == 127 && position->y == -83);
    float initial = GAME_GameSurfaceInfluence_get_const(world, hit)->strength;
    assert(initial > 0);
    simulation_advance(&clock, clock.time + .1);
    float faded = GAME_GameSurfaceInfluence_get_const(world, hit)->strength;
    assert(faded > 0 && faded < initial);
    damage[0].i = 30;
    for (int i = 0; i < 3; i++) {
        assert(
            game_entities_request_event(live->entities, target, "Damage", (flow_inputs){ damage, 2 }) == 0
        );
    }
    simulation_step(&clock);
    assert(!ecs_entity_is_alive(world, target));
    assert(feedback_pulses(world, 42, &hit) == 1);
    assert(feedback_pulses(world, 64, &death) == 1);
    position = GAME_GameTransform_get_const(world, death);
    assert(position->x == 127 && position->y == -83);
    assert(game_particles_count(live->particles) == 0);
    simulation_advance(&clock, clock.time + .5);
    assert(feedback_pulses(world, 42, &hit) == 0);
    assert(feedback_pulses(world, 64, &death) == 0);
    assert(game_particles_count(live->particles) == 0);
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(world);
}

static void register_pickup_content(encounter_host *h)
{
    const char *names[] = { "ArenaPickupBehavior",
                            "ArenaDropBehavior",
                            "ArenaShardBehavior",
                            "ArenaImpactFeedback",
                            "SurfacePulseBehavior" };
    for (int i = 0; i < 5; i++) {
        assert(
            game_live_add_definition(
                h->live,
                game_live_definition_new(names[i], behavior(names[i]))
            ) == 0
        );
    }
    static game_prototype_registry registry;
    registry = (game_prototype_registry){ GAME_REGISTRY_COMPONENTS,
                                          GAME_REGISTRY_COMPONENT_COUNT };
    const char *prototypes[] = { "ArenaExperience",
                                 "ArenaEnemyShard",
                                 "ArenaHitPulse",
                                 "ArenaDeathPulse" };
    for (int i = 0; i < 4; i++) {
        game_prototype *value = prototype(prototypes[i], &registry);
        assert(game_entities_register(h->live->entities, value) == 0);
        game_prototype_free(value);
    }
}

static ecs_entity experience_at(encounter_host *h, float offset)
{
    static game_prototype_registry registry;
    registry = (game_prototype_registry){ GAME_REGISTRY_COMPONENTS,
                                          GAME_REGISTRY_COMPONENT_COUNT };
    game_prototype *value = prototype("ArenaExperience", &registry);
    ecs_entity entity = spawn(h, value);
    game_prototype_free(value);
    *GAME_GameTransform_get(h->world, entity) = *GAME_GameTransform_get_const(h->world, h->player);
    GAME_GameTransform_get(h->world, entity)->x += offset;
    return entity;
}

static float distance_to_player(encounter_host *h, ecs_entity entity)
{
    const GameTransform *a = GAME_GameTransform_get_const(h->world, entity);
    const GameTransform *b = GAME_GameTransform_get_const(h->world, h->player);
    return hypotf(a->x - b->x, a->y - b->y);
}

static void pickup_lifecycle()
{
    encounter_host h = create(false);
    register_pickup_content(&h);
    assert(game_behavior_destroy_entity(h.live->behaviors, h.manager) == 0);
    assert(ecs_world_apply_commands(h.world) == ECS_OK);
    if (!GAME_GameExperience_has(h.world, h.player)) {
        assert(GAME_GameExperience_add(h.world, h.player, 0) == ECS_OK);
        assert(ecs_world_apply_commands(h.world) == ECS_OK);
    }
    ecs_entity pickup = experience_at(&h, 80);
    ecs_entity second = experience_at(&h, -80);
    simulation_step(&h.clock);
    float first_yaw = GAME_GameVisualTransform_get_const(h.world, pickup)->yaw;
    float second_yaw = GAME_GameVisualTransform_get_const(h.world, second)->yaw;
    assert(fabsf(first_yaw - second_yaw) > .001f);
    simulation_advance(&h.clock, .25);
    const GameVisualTransform *visual = GAME_GameVisualTransform_get_const(h.world, pickup);
    assert(visual->scale > .795f && visual->scale < .805f);
    assert(visual->yaw > first_yaw + 2.9f && visual->yaw < first_yaw + 3.2f);
    assert(
        fabsf(
            GAME_GameVisualTransform_get_const(h.world, second)->scale - visual->scale
        ) < .001f
    );
    simulation_advance(&h.clock, .5);
    assert(fabsf(GAME_GameVisualTransform_get_const(h.world, pickup)->scale - 1) < .001f);
    assert(GAME_GamePickup_get_const(h.world, pickup)->state == 0);
    simulation_advance(&h.clock, 11.25);
    assert(GAME_GameVisualTransform_get_const(h.world, pickup)->scale > 0);
    assert(GAME_GameVisualTransform_get_const(h.world, pickup)->scale < .5f);
    simulation_advance(&h.clock, 12.1);
    assert(!ecs_entity_is_alive(h.world, pickup));
    assert(GAME_GameExperience_get_const(h.world, h.player)->current == 0);

    pickup = experience_at(&h, 30);
    simulation_step(&h.clock);
    assert(GAME_GamePickup_get_const(h.world, pickup)->state == 1);
    assert(GAME_GameTrail_get_const(h.world, pickup)->width > 0);
    float initial = distance_to_player(&h, pickup);
    double started = h.clock.time;
    simulation_advance(&h.clock, started + .05);
    assert(distance_to_player(&h, pickup) > initial);
    for (int i = 0; i < 4; i++) {
        assert(
            game_entities_request_event(h.live->entities, pickup, "CollectExperience", (flow_inputs){ 0 }) == 0
        );
    }
    simulation_advance(&h.clock, started + .2);
    assert(
        fabsf(
            GAME_GameTransform_get_const(h.world, pickup)->y -
            GAME_GameTransform_get_const(h.world, h.player)->y
        ) > 5
    );
    assert(GAME_GameExperience_get_const(h.world, h.player)->current == 0);
    simulation_advance(&h.clock, started + .7);
    assert(!ecs_entity_is_alive(h.world, pickup));
    assert(GAME_GameExperience_get_const(h.world, h.player)->current == 1);
    simulation_advance(&h.clock, h.clock.time + .2);
    assert(GAME_GameExperience_get_const(h.world, h.player)->current == 1);

    pickup = experience_at(&h, 0);
    simulation_step(&h.clock);
    assert(GAME_GamePickup_get_const(h.world, pickup)->state == 1);
    simulation_advance(&h.clock, h.clock.time + .05);
    assert(distance_to_player(&h, pickup) > 1);
    game_entities_request_destroy(h.live->entities, h.player);
    simulation_step(&h.clock);
    simulation_step(&h.clock);
    assert(!ecs_entity_is_alive(h.world, pickup));
    destroy_host(&h);
}

static int component_count(ecs_world *world, ecs_component_id component)
{
    ecs_query query;
    assert(ecs_query_begin(world, &query, &component, 1) == ECS_OK);
    int count = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        count++;
    }
    ecs_query_end(&query);
    return count;
}

static void telegraphed_arrivals()
{
    encounter_host h = create_with_delay(false, 1);
    GAME_GameEncounter_get(h.world, h.manager)->total = 8;
    simulation_advance(&h.clock, .25);
    assert(state(&h).spawned == 8 && state(&h).alive == 8);
    ecs_entity *targets = enemies(&h);
    assert(acount(targets) == 0);
    afree(targets);
    GameTransform marks[8];
    ecs_query query;
    ecs_component_id type = GAME_COMPONENT_GameSpawnRequest.id;
    assert(ecs_query_begin(h.world, &query, &type, 1) == ECS_OK);
    ecs_entity entity;
    int count = 0;
    const GameTransform center = *GAME_GameTransform_get_const(h.world, h.manager);
    while (ecs_query_next(&query, &entity)) {
        assert(count < 8);
        marks[count++] = *GAME_GameTransform_get_const(h.world, entity);
        assert(GAME_GameGeometry3D_get_const(h.world, entity)->width > 0);
        assert(!GAME_GameCollider_has(h.world, entity));
        float row = (marks[count - 1].x - center.x) / 48 + 3.5f;
        bool expected = fabsf(marks[count - 1].y - center.y - 116) < .01f &&
                        fabsf(row - roundf(row)) < .001f && row >= -.001f &&
                        row <= 7.001f;
        if (!expected) {
            fprintf(
                stderr,
                "formation marker: center=(%g,%g) actual=(%g,%g) row=%g\n",
                center.x,
                center.y,
                marks[count - 1].x,
                marks[count - 1].y,
                row
            );
        }
        assert(expected);
        assert(hypotf(marks[count - 1].x - center.x, marks[count - 1].y - center.y) > 100);
    }
    ecs_query_end(&query);
    assert(count == 8);
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            assert(hypotf(marks[i].x - marks[j].x, marks[i].y - marks[j].y) >= 47.9f);
        }
    }
    simulation_advance(&h.clock, 1.05);
    targets = enemies(&h);
    assert(acount(targets) == 0 && state(&h).alive == 8);
    afree(targets);
    simulation_advance(&h.clock, 1.3);
    targets = enemies(&h);
    assert(acount(targets) == 8 && state(&h).alive == 8);
    for (int i = 0; i < acount(targets); i++) {
        const GameTransform *position = GAME_GameTransform_get_const(h.world, targets[i]);
        int matching = 0;
        for (int j = 0; j < count; j++) {
            matching += hypotf(position->x - marks[j].x, position->y - marks[j].y) < .01f;
        }
        assert(matching == 1);
    }
    assert(component_count(h.world, type) == 0);
    afree(targets);
    destroy_host(&h);
}

static void native_visual_birth_pose()
{
    encounter_host h = create_with_delay(false, .25f);
    assert(
        game_live_add_definition(
            h.live,
            game_live_definition_new("ArenaEnemyVisualBehavior", behavior("ArenaEnemyVisualBehavior"))
        ) == 0
    );
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    game_prototype *enemy = prototype("ArenaEnemy", &registry);
    for (int i = acount(enemy->graphs) - 1; i >= 0; i--) {
        if (strcmp(enemy->graphs[i], "HealthBehavior") &&
            strcmp(enemy->graphs[i], "ArenaEnemyVisualBehavior")) {
            assert(game_prototype_remove_graph(enemy, enemy->graphs[i]) == 0);
        }
    }
    /* An unfinished, zero-amplitude reaction makes an extra age advance visible
     * without changing the expected randomized birth orientation. */
    assert(
        game_prototype_write_field(
            enemy,
            "GameVisualMotion",
            "reaction_age",
            (pin_value){ .type = PIN_F32, .f = .05f }
        ) == 0
    );
    assert(game_entities_register(h.live->entities, enemy) == 0);
    simulation_advance(&h.clock, .2);
    /* This fresh prototype has its own identity; the shared host also contains
     * a health-only ArenaEnemy. Direct the real telegraphs to this generation
     * explicitly instead of relying on the first same-name registry match. */
    ecs_query query;
    ecs_component_id component = GAME_COMPONENT_GameSpawnRequest.id;
    assert(ecs_query_begin(h.world, &query, &component, 1) == ECS_OK);
    ecs_entity marker;
    int markers = 0;
    while (ecs_query_next(&query, &marker)) {
        GAME_GameSpawnRequest_get(h.world, marker)->prototype = game_prototype_identity(enemy);
        markers++;
    }
    ecs_query_end(&query);
    assert(markers == 3);
    ecs_entity *targets = NULL;
    for (int frame = 0; frame < 60; frame++) {
        simulation_step(&h.clock);
        targets = enemies(&h);
        if (acount(targets)) {
            break;
        }
        afree(targets);
    }
    assert(acount(targets) == 3);
    for (int i = 0; i < acount(targets); i++) {
        const GameVisualMotion *motion = GAME_GameVisualMotion_get_const(h.world, targets[i]);
        const GameVisualTransform
            *visual = GAME_GameVisualTransform_get_const(h.world, targets[i]);
        assert(
            fabsf(motion->rotation.x) + fabsf(motion->rotation.y) +
                fabsf(motion->rotation.z) >
            .001f
        );
        assert(
            visual->pitch == motion->rotation.x &&
            visual->yaw == motion->rotation.y &&
            visual->roll == motion->rotation.z
        );
        assert(motion->reaction_age == .05f);
    }
    GameVisualMotion motion = *GAME_GameVisualMotion_get_const(h.world, targets[0]);
    GameVisualTransform visual = *GAME_GameVisualTransform_get_const(h.world, targets[0]);
    assert(engine_set_playing(h.runtime, false) == 0);
    simulation_advance(&h.clock, h.clock.time + .3);
    assert(!memcmp(&motion, GAME_GameVisualMotion_get_const(h.world, targets[0]), sizeof motion));
    assert(
        !memcmp(&visual, GAME_GameVisualTransform_get_const(h.world, targets[0]), sizeof visual)
    );
    afree(targets);
    destroy_host(&h);
    game_prototype_free(enemy);
}

static void native_visual_motion()
{
    ecs_world *world = ecs_world_new();
    const ecs_component_desc *components[] = { &GAME_COMPONENT_GameVisualMotion,
                                               &GAME_COMPONENT_GameVisualTransform,
                                               &GAME_COMPONENT_GameGeometry3D,
                                               &GAME_COMPONENT_GameOrientation };
    for (int i = 0; i < 4; i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    ecs_entity entity = ecs_world_create_entity(world);
    for (int i = 0; i < 4; i++) {
        assert(ecs_world_record_add(world, entity, components[i]->id, NULL, 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    GameVisualMotion *motion = GAME_GameVisualMotion_get(world, entity);
    *motion = (GameVisualMotion){ .rotation = { .3f, -.2f, .7f },
                                  .angular_velocity = { .4f, -.2f, .6f },
                                  .speed = 1.6f,
                                  .rest_elevation = 12,
                                  .reaction_rotation = { .36f, 0, .5f },
                                  .reaction_lift = 1.6f,
                                  .reaction_duration = .5f };
    GameVisualTransform *visual = GAME_GameVisualTransform_get(world, entity);
    visual->scale = .7f;
    GAME_GameOrientation_get(world, entity)->radians = .42f;
    assert(game_visual_motion_tick(world, 0) == FLOW_OK);
    assert(visual->pitch == .3f && visual->yaw == -.2f && visual->roll == .7f);
    assert(GAME_GameGeometry3D_get_const(world, entity)->elevation == 12);
    assert(game_visual_motion_tick(world, .25f) == FLOW_OK);
    assert(fabsf(visual->pitch - .82f) < .00001f);
    assert(fabsf(visual->yaw + .28f) < .00001f);
    assert(fabsf(visual->roll - 1.44f) < .00001f);
    assert(fabsf(GAME_GameGeometry3D_get_const(world, entity)->elevation - 13.6f) < .00001f);
    assert(game_visual_motion_tick(world, .25f) == FLOW_OK);
    assert(
        visual->pitch == motion->rotation.x && visual->roll == motion->rotation.z
    );
    assert(GAME_GameGeometry3D_get_const(world, entity)->elevation == 12);

    /* Inspector speed edits apply on the next tick. Restarting a reaction uses
     * the continuing base pose rather than accumulating the previous offset. */
    motion->speed = 0;
    CF_V3 resting = motion->rotation;
    motion->reaction_age = 0;
    motion->reaction_rotation = (CF_V3){ -.2f, .1f, -.3f };
    motion->reaction_lift = 2;
    assert(game_visual_motion_tick(world, .25f) == FLOW_OK);
    assert(fabsf(visual->roll - (resting.z - .3f)) < .00001f);
    motion->reaction_age = 0;
    motion->reaction_rotation = (CF_V3){ .1f, -.1f, .2f };
    motion->reaction_lift = .5f;
    assert(game_visual_motion_tick(world, .25f) == FLOW_OK);
    assert(fabsf(visual->roll - (resting.z + .2f)) < .00001f);
    assert(GAME_GameGeometry3D_get_const(world, entity)->elevation == 12.5f);
    assert(game_visual_motion_tick(world, 1) == FLOW_OK);
    assert(visual->roll == resting.z && visual->scale == .7f);
    assert(GAME_GameGeometry3D_get_const(world, entity)->elevation == 12);
    assert(GAME_GameOrientation_get_const(world, entity)->radians == .42f);
    motion->speed = 2;
    assert(game_visual_motion_tick(world, .25f) == FLOW_OK);
    assert(fabsf(visual->roll - (resting.z + .3f)) < .00001f);

    GameVisualMotion before = *motion;
    assert(game_visual_motion_tick(world, -.1f) == FLOW_ERR_ARGUMENT);
    assert(game_visual_motion_tick(world, NAN) == FLOW_ERR_ARGUMENT);
    assert(!memcmp(motion, &before, sizeof before));
    ecs_entity invalid = ecs_world_create_entity(world);
    assert(GAME_GameVisualMotion_add(world, invalid, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    motion = GAME_GameVisualMotion_get(world, entity);
    assert(game_visual_motion_tick(world, .1f) == FLOW_ERR_STATE);
    assert(!memcmp(motion, &before, sizeof before));
    assert(GAME_GameVisualTransform_add(world, invalid, 0) == ECS_OK);
    assert(GAME_GameGeometry3D_add(world, invalid, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    GAME_GameVisualMotion_get(world, invalid)->reaction_duration = NAN;
    assert(game_visual_motion_tick(world, .1f) == FLOW_ERR_ARGUMENT);
    assert(!memcmp(motion, &before, sizeof before));
    ecs_world_free(world);
}

static float feedback_parameter(encounter_host *h, int material, const char *name)
{
    float parameters[32][4];
    render_shader_parameters(
        material,
        h->world,
        h->player,
        GAME_GameShader_get_const(h->world, h->player)->parameters,
        parameters
    );
    for (int i = 0; i < 32; i++) {
        const char *parameter = render_shader_parameter_name(material, i);
        if (parameter && !strcmp(parameter, name)) {
            return parameters[i][0];
        }
    }
    assert(false);
    return 0;
}

static void combat_feedback_gates()
{
    encounter_host h = create(false);
    behavior_asset *material = link_test_libraries(
        read_asset("content/materials/ArenaPlayer.kdl")
    );
    shader_error error;
    assert(render_shader_upsert(material, &error));
    int id = render_shader_find(material->name);
    behavior_asset_free(material);
    assert(
        game_live_add_definition(
            h.live,
            game_live_definition_new("CombatFeedback", behavior("CombatFeedback"))
        ) == 0
    );
    assert(game_behavior_attach(h.live->behaviors, h.player, "CombatFeedback") == 0);
    GAME_GameShader_get(h.world, h.player)->parameters = "\"Hit Flash\" 0.7; ";
    game_behavior_set_delta_seconds(h.live->behaviors, .07f);
    assert(
        game_behavior_dispatch(h.live->behaviors, h.player, "Tick", (flow_inputs){ 0 }, NULL) >= 0
    );
    assert(fabsf(feedback_parameter(&h, id, "Hit Flash") - .7f) < .00001f);
    pin_value damage[] = { { .type = PIN_I32, .type_name = "int", .i = 1 },
                           { .type = PIN_HANDLE, .type_name = "ecs.entity" } };
    assert(
        game_behavior_dispatch(h.live->behaviors, h.player, "Damaged", (flow_inputs){ damage, 2 }, NULL) >= 0
    );
    assert(feedback_parameter(&h, id, "Hit Flash") == 2);
    assert(
        game_behavior_dispatch(h.live->behaviors, h.player, "Tick", (flow_inputs){ 0 }, NULL) >= 0
    );
    assert(fabsf(feedback_parameter(&h, id, "Hit Flash") - 1) < .00001f);
    game_behavior_set_delta_seconds(h.live->behaviors, .2f);
    assert(
        game_behavior_dispatch(h.live->behaviors, h.player, "Tick", (flow_inputs){ 0 }, NULL) >= 0
    );
    assert(feedback_parameter(&h, id, "Hit Flash") == 0);
    assert(GAME_GameDash_add(h.world, h.player, 0) == ECS_OK);
    assert(ecs_world_apply_commands(h.world) == ECS_OK);
    GameDash *dash = GAME_GameDash_get(h.world, h.player);
    dash->remaining = .1f;
    dash->duration = .2f;
    assert(
        game_behavior_dispatch(h.live->behaviors, h.player, "Tick", (flow_inputs){ 0 }, NULL) >= 0
    );
    assert(fabsf(feedback_parameter(&h, id, "Dash Glow") - .9f) < .00001f);
    dash->remaining = 0;
    assert(
        game_behavior_dispatch(h.live->behaviors, h.player, "Tick", (flow_inputs){ 0 }, NULL) >= 0
    );
    assert(feedback_parameter(&h, id, "Dash Glow") == 0);
    assert(GAME_GameDash_remove(h.world, h.player, 0) == ECS_OK);
    assert(GAME_GameShader_remove(h.world, h.player, 0) == ECS_OK);
    assert(ecs_world_apply_commands(h.world) == ECS_OK);
    /* No idle material write or absent-dash helper may run after completion. */
    assert(
        game_behavior_dispatch(h.live->behaviors, h.player, "Tick", (flow_inputs){ 0 }, NULL) >= 0
    );
    render_shader_clear_world(h.world);
    destroy_host(&h);
}

static void enemy_contact_motion()
{
    encounter_host h = create(false);
    assert(
        game_live_add_definition(
            h.live,
            game_live_definition_new("EnemyBehavior", behavior("EnemyBehavior"))
        ) == 0
    );
    assert(
        game_live_add_definition(
            h.live,
            game_live_definition_new("ArenaEnemyVisualBehavior", behavior("ArenaEnemyVisualBehavior"))
        ) == 0
    );
    GAME_GameBody_get(h.world, h.player)->type = GAME_BODY_STATIC;
    simulation_advance(&h.clock, .35);
    ecs_entity *targets = enemies(&h);
    assert(acount(targets) == 3);
    GameTransform center = *GAME_GameTransform_get_const(h.world, h.player);
    for (int i = 0; i < acount(targets); i++) {
        const GameCollider *collider = GAME_GameCollider_get_const(h.world, targets[i]);
        assert((collider->mask & collider->category) != 0 && !collider->sensor);
        *GAME_GameTransform_get(h.world, targets[i]) = (GameTransform){
            center.x - 90 + 90 * i,
            center.y + 125
        };
        assert(game_behavior_attach(h.live->behaviors, targets[i], "EnemyBehavior") == 0);
        assert(game_behavior_attach(h.live->behaviors, targets[i], "ArenaEnemyVisualBehavior") == 0);
    }
    simulation_step(&h.clock);
    GameVisualTransform initial = *GAME_GameVisualTransform_get_const(h.world, targets[0]);
    const GameVisualTransform *second = GAME_GameVisualTransform_get_const(h.world, targets[1]);
    assert(fabsf(initial.pitch - second->pitch) + fabsf(initial.roll - second->roll) > .01f);
    float rest_elevation = GAME_GameGeometry3D_get_const(h.world, targets[0])->elevation;
    float diameter = 2 * GAME_GameCollider_get_const(h.world, targets[0])->radius;
    bool bumped = false;
    float minimum = 1000;
    for (int step = 0; step < 360; step++) {
        simulation_step(&h.clock);
        for (int i = 0; i < acount(targets); i++) {
            bumped |= GAME_GameGeometry3D_get_const(h.world, targets[i])->elevation >
                      rest_elevation + .5f;
            assert(GAME_GameOrientation_get_const(h.world, targets[i])->radians == .35f);
            for (int j = i + 1; j < acount(targets); j++) {
                const GameTransform *a = GAME_GameTransform_get_const(h.world, targets[i]);
                const GameTransform *b = GAME_GameTransform_get_const(h.world, targets[j]);
                float distance = hypotf(a->x - b->x, a->y - b->y);
                minimum = fminf(minimum, distance);
                assert(distance >= diameter - 1);
            }
        }
    }
    assert(minimum < diameter + 1 && bumped);
    const GameVisualTransform *final = GAME_GameVisualTransform_get_const(h.world, targets[0]);
    assert(fabsf(final->pitch - initial.pitch) + fabsf(final->roll - initial.roll) > .1f);
    GameVisualMotion paused = *GAME_GameVisualMotion_get_const(h.world, targets[0]);
    GameVisualTransform paused_pose = *final;
    assert(engine_set_playing(h.runtime, false) == 0);
    simulation_advance(&h.clock, h.clock.time + .2);
    assert(!memcmp(&paused, GAME_GameVisualMotion_get_const(h.world, targets[0]), sizeof paused));
    assert(
        !memcmp(&paused_pose, GAME_GameVisualTransform_get_const(h.world, targets[0]), sizeof paused_pose)
    );
    afree(targets);
    destroy_host(&h);
}

static void authored_parts_at_birth(bool moving)
{
    encounter_host h = create(false);
    register_pickup_content(&h);
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    game_prototype *definition = prototype("ArenaExperience", &registry);
    ecs_entity snapshot;
    char error[256];
    assert(
        game_prototype_spawn_components(definition, h.world, &snapshot, error, sizeof error) == ECS_OK
    );
    GameTransform origin = { 51, -23 };
    GameGeometry3D shape = { .primitive = GAME_PRIMITIVE_BOX_FRAME,
                             .width = 11,
                             .height = 9,
                             .depth = 13,
                             .elevation = 17 };
    GameVisualTransform visual = { .pitch = .37f, .yaw = -.29f, .roll = .61f, .scale = .7f };
    GameOrientation heading = { .radians = .48f };
    *GAME_GameTransform_get(h.world, snapshot) = origin;
    *GAME_GameGeometry3D_get(h.world, snapshot) = shape;
    *GAME_GameVisualTransform_get(h.world, snapshot) = visual;
    if (moving) {
        assert(GAME_GameVisualMotion_add(h.world, snapshot, 0) == ECS_OK);
        assert(ecs_world_apply_commands(h.world) == ECS_OK);
        *GAME_GameVisualMotion_get(h.world, snapshot) = (GameVisualMotion){
            .rotation = { visual.pitch, visual.yaw, visual.roll },
            .angular_velocity = { .2f, -.4f, .6f },
            .speed = 1.6f,
            .rest_elevation = shape.elevation,
            .reaction_rotation = { .36f, 0, .5f },
            .reaction_lift = 1.6f,
            .reaction_duration = .5f
        };
        assert(game_visual_motion_tick(h.world, .25f) == FLOW_OK);
        visual = *GAME_GameVisualTransform_get_const(h.world, snapshot);
        shape = *GAME_GameGeometry3D_get_const(h.world, snapshot);
        /* All twelve fragments must inherit this integrated, actively recoiling
         * pose, not the motion component's base rotation or rest elevation. */
        assert(fabsf(visual.pitch - .81f) < .00001f);
        assert(fabsf(shape.elevation - 18.6f) < .00001f);
    }
    *GAME_GameOrientation_get(h.world, snapshot) = heading;
    *GAME_GameShader_get(h.world, snapshot) = (GameShader){
        .material = "ArenaEnemy",
        .parameters = "HitFlash 0.3; "
    };
    GAME_GamePickup_get(h.world, snapshot)->state = 3;
    game_behavior_set_delta_seconds(h.live->behaviors, 0);
    /* Runtime Spawn Prototype initializes the snapshot before ordered Begin
     * handlers attach. ArenaDrop consumes it on Begin, before ordinary Tick. */
    for (int i = 0; i < acount(definition->graphs); i++) {
        assert(game_behavior_attach(h.live->behaviors, snapshot, definition->graphs[i]) == 0);
    }
    game_prototype_free(definition);
    assert(game_entities_flush_initializers(h.live->entities, h.clock.time) == 0);
    CF_M4x4 frame = cf_m4_from_trs(
        (CF_V3){ origin.x, shape.elevation, origin.y },
        game_visual_rotation(
            heading.radians,
            (CF_V3){ visual.pitch, visual.yaw, visual.roll }
        ),
        (CF_V3){ visual.scale, visual.scale, visual.scale }
    );
    bool seen[12] = { 0 };
    ecs_entity pieces[12];
    int count = 0;
    ecs_component_id required[] = { GAME_COMPONENT_GameGeometry3D.id,
                                    GAME_COMPONENT_GameVelocity.id };
    ecs_query query;
    assert(ecs_query_begin(h.world, &query, required, 2) == ECS_OK);
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameGeometry3D *piece = GAME_GameGeometry3D_get_const(h.world, entity);
        if (piece->primitive != GAME_PRIMITIVE_BOX) {
            continue;
        }
        const GameTransform *position = GAME_GameTransform_get_const(h.world, entity);
        int matched = -1;
        for (int i = 0; i < game_visual_part_count(shape.primitive); i++) {
            game_visual_part part = game_visual_part_at(shape, i);
            CF_V3 center = cf_m4_transform_point(frame, part.center);
            if (fabsf(piece->width - part.dimensions.x) < .001f &&
                fabsf(piece->height - part.dimensions.y) < .001f &&
                fabsf(piece->depth - part.dimensions.z) < .001f &&
                fabsf(position->x - center.x) < .001f &&
                fabsf(position->y - center.z) < .001f &&
                fabsf(piece->elevation - center.y) < .001f) {
                matched = i;
                break;
            }
        }
        assert(matched >= 0 && !seen[matched]);
        seen[matched] = true;
        assert(!memcmp(GAME_GameVisualTransform_get_const(h.world, entity), &visual, sizeof visual));
        assert(
            GAME_GameOrientation_get_const(h.world, entity)->radians == heading.radians
        );
        const GameShader *material = GAME_GameShader_get_const(h.world, entity);
        assert(
            !strcmp(material->material, "ArenaEnemy") &&
            !strcmp(material->parameters, "HitFlash 0.3; ")
        );
        assert(count < 12);
        pieces[count++] = entity;
    }
    ecs_query_end(&query);
    assert(count == 12);
    /* Lifecycle shrink must begin from the source's scale, including a scaled source. */
    game_behavior_set_delta_seconds(h.live->behaviors, .001f);
    for (int i = 0; i < count; i++) {
        assert(
            game_behavior_dispatch(h.live->behaviors, pieces[i], "Tick", (flow_inputs){ 0 }, NULL) >= 0
        );
        float scale = GAME_GameVisualTransform_get_const(h.world, pieces[i])->scale;
        assert(scale > .69f && scale < visual.scale);
    }
    destroy_host(&h);
}

static int source_shards(ecs_world *world, GameGeometry3D source)
{
    ecs_component_id component = GAME_COMPONENT_GameGeometry3D.id;
    ecs_query query;
    assert(ecs_query_begin(world, &query, &component, 1) == ECS_OK);
    ecs_entity entity;
    int count = 0;
    while (ecs_query_next(&query, &entity)) {
        const GameGeometry3D *shape = GAME_GameGeometry3D_get_const(world, entity);
        if (shape->primitive != GAME_PRIMITIVE_BOX ||
            !GAME_GameVelocity_has(world, entity)) {
            continue;
        }
        bool matches = false;
        for (int i = 0; i < game_visual_part_count(source.primitive); i++) {
            game_visual_part part = game_visual_part_at(source, i);
            matches |= shape->primitive == part.primitive &&
                       fabsf(shape->width - part.dimensions.x) < .001f &&
                       fabsf(shape->height - part.dimensions.y) < .001f &&
                       fabsf(shape->depth - part.dimensions.z) < .001f;
        }
        assert(matches);
        assert(!strcmp(GAME_GameShader_get_const(world, entity)->material, "ArenaEnemy"));
        float yaw = GAME_GameVisualTransform_get_const(world, entity)->yaw;
        assert(fabsf(yaw - .3f) < .001f || fabsf(yaw - 1.3f) < .001f || fabsf(yaw - 2.3f) < .001f);
        assert(GAME_GameOrientation_get_const(world, entity)->radians == .35f);
        assert(!GAME_GameCollider_has(world, entity));
        count++;
    }
    ecs_query_end(&query);
    return count;
}

static void final_experience()
{
    encounter_host h = create(false);
    register_pickup_content(&h);
    if (!GAME_GameExperience_has(h.world, h.player)) {
        assert(GAME_GameExperience_add(h.world, h.player, 0) == ECS_OK);
        assert(ecs_world_apply_commands(h.world) == ECS_OK);
    }
    simulation_advance(&h.clock, .35);
    ecs_entity *targets = enemies(&h);
    assert(acount(targets) == 3);
    GameGeometry3D source = *GAME_GameGeometry3D_get_const(h.world, targets[0]);
    for (int i = 0; i < acount(targets); i++) {
        *GAME_GameVisualTransform_get(h.world, targets[i]) = (GameVisualTransform){
            .pitch = .4f,
            .yaw = .3f + i,
            .roll = -.6f,
            .scale = 1
        };
        GAME_GameVisualMotion_get(h.world, targets[i])->rotation = (CF_V3){ .4f,
                                                                            .3f + i,
                                                                            -.6f };
        assert(game_behavior_attach(h.live->behaviors, targets[i], "ArenaImpactFeedback") == 0);
        damage(&h, targets[i], 30);
        damage(&h, targets[i], 30);
    }
    simulation_step(&h.clock);
    assert(component_count(h.world, GAME_COMPONENT_GamePickup.id) == 3);
    assert(state(&h).alive == 0 && state(&h).status == GAME_ENCOUNTER_ACTIVE);
    assert(GAME_GameExperience_get_const(h.world, h.player)->current == 0);
    simulation_advance(&h.clock, h.clock.time + .2);
    assert(source_shards(h.world, source) == 36);
    simulation_advance(&h.clock, h.clock.time + .8);
    assert(component_count(h.world, GAME_COMPONENT_GamePickup.id) == 0);
    assert(GAME_GameExperience_get_const(h.world, h.player)->current == 3);
    assert(state(&h).status == GAME_ENCOUNTER_CLEARED);
    afree(targets);
    destroy_host(&h);
}

static void ship_ticks(encounter_host *h, int count, float delta)
{
    game_behavior_set_delta_seconds(h->live->behaviors, delta);
    for (int i = 0; i < count; i++) {
        float previous = GAME_GameVisualTransform_get_const(h->world, h->player)->roll;
        assert(
            game_behavior_dispatch(h->live->behaviors, h->player, "Tick", (flow_inputs){ 0 }, NULL) >= 0
        );
        const GameVisualTransform *visual = GAME_GameVisualTransform_get_const(h->world, h->player);
        assert(isfinite(visual->roll) && isfinite(visual->pitch));
        assert(fabsf(visual->roll - previous) < .04f && fabsf(visual->pitch) < .1f);
    }
}

static void ship_motion()
{
    encounter_host h = create(false);
    assert(game_behavior_destroy_entity(h.live->behaviors, h.manager) == 0);
    assert(ecs_world_apply_commands(h.world) == ECS_OK);
    assert(
        game_live_add_definition(
            h.live,
            game_live_definition_new("ArenaShipMotionBehavior", behavior("ArenaShipMotionBehavior"))
        ) == 0
    );
    assert(game_behavior_attach(h.live->behaviors, h.player, "ArenaShipMotionBehavior") == 0);
    behavior_instance *motion = game_behavior_instance(h.live->behaviors, h.player, "ArenaShipMotionBehavior");
    behavior_variable *speed = behavior_asset_find_variable_by_name(motion->asset, "Roll Speed (Degrees/Second)");
    behavior_variable *lift = behavior_asset_find_variable_by_name(motion->asset, "Reverse Nose Lift (Degrees)");
    assert(
        speed && lift && !strcmp(speed->category, "Motion") &&
        !strcmp(lift->category, "Motion")
    );
    float nose_angle = lift->default_value.f * 3.14159265358979323846f / 180;
    const float face = 1.57079632679489661923f;
    GAME_GameAim_get(h.world, h.player)->direction = (CF_V2){ 1, 0 };
    GAME_GameInput_get(h.world, h.player)->move = (CF_V2){ 0, 1 };
    GAME_GameOrientation_get(h.world, h.player)->radians = 0;
    const GameCollider collider = *GAME_GameCollider_get_const(h.world, h.player);

    /* Sustained strafe accelerates gradually, then reaches the next pyramid
     * face in about three seconds without a direction-change spin. */
    ship_ticks(&h, 1, 1.0f / 60);
    float first = GAME_GameVisualTransform_get_const(h.world, h.player)->roll;
    assert(first > 0 && first < .002f);
    ship_ticks(&h, 29, 1.0f / 60);
    assert(GAME_GameVisualTransform_get_const(h.world, h.player)->roll < .15f);
    ship_ticks(&h, 180, 1.0f / 60);
    float roll = GAME_GameVisualTransform_get_const(h.world, h.player)->roll;
    assert(roll > face && roll < face + .35f);
    ship_ticks(&h, 1, 0);
    assert(GAME_GameVisualTransform_get_const(h.world, h.player)->roll == roll);

    /* Reversal preserves angular momentum before the opposing drive wins. */
    GAME_GameInput_get(h.world, h.player)->move = (CF_V2){ 0, -1 };
    ship_ticks(&h, 1, 1.0f / 60);
    float interrupted = GAME_GameVisualTransform_get_const(h.world, h.player)->roll;
    assert(interrupted > roll && interrupted - roll < .02f);
    ship_ticks(&h, 89, 1.0f / 60);
    roll = GAME_GameVisualTransform_get_const(h.world, h.player)->roll;
    assert(roll < interrupted - .2f);

    /* Release settles to the nearest face, including a nonzero face. */
    float resting = roundf(roll / face) * face;
    assert(resting == face);
    GAME_GameInput_get(h.world, h.player)->move = (CF_V2){ 0, 0 };
    ship_ticks(&h, 120, 1.0f / 60);
    const GameVisualTransform *visual = GAME_GameVisualTransform_get_const(h.world, h.player);
    assert(fabsf(visual->roll - resting) < .002f);

    GAME_GameInput_get(h.world, h.player)->move = (CF_V2){ -1, 0 };
    ship_ticks(&h, 120, 1.0f / 60);
    visual = GAME_GameVisualTransform_get_const(h.world, h.player);
    assert(fabsf(visual->roll - resting) < .002f);
    assert(visual->pitch > nose_angle * .99f && visual->pitch < nose_angle * 1.01f);
    GAME_GameInput_get(h.world, h.player)->move = (CF_V2){ 1, 0 };
    ship_ticks(&h, 120, 1.0f / 60);
    visual = GAME_GameVisualTransform_get_const(h.world, h.player);
    assert(fabsf(visual->pitch) < .001f);
    assert(fabsf(visual->roll - resting) < .002f);

    /* The roll remains unwrapped after a full turn; ship_ticks checks every
     * frame for continuity through the 2pi boundary. */
    GAME_GameInput_get(h.world, h.player)->move = (CF_V2){ 0, 1 };
    ship_ticks(&h, 720, 1.0f / 60);
    roll = GAME_GameVisualTransform_get_const(h.world, h.player)->roll;
    assert(roll > resting + 4 * face);
    GAME_GameInput_get(h.world, h.player)->move = (CF_V2){ 0, 0 };
    resting = roundf(roll / face) * face;
    ship_ticks(&h, 120, 1.0f / 60);
    assert(fabsf(GAME_GameVisualTransform_get_const(h.world, h.player)->roll - resting) < .002f);

    /* Aim determines the local direction. The authored speed control slows
     * the same motion without modifying aim or the gameplay collider. */
    GAME_GameAim_get(h.world, h.player)->direction = (CF_V2){ 0, 1 };
    GAME_GameOrientation_get(h.world, h.player)->radians = face;
    assert(
        behavior_instance_set_variable(
            motion,
            speed->id,
            &(pin_value){ .type = PIN_F32, .type_name = "float", .f = 18 }
        ) == 0
    );
    GAME_GameInput_get(h.world, h.player)->move = (CF_V2){ 1, 0 };
    ship_ticks(&h, 60, 1.0f / 60);
    roll = GAME_GameVisualTransform_get_const(h.world, h.player)->roll;
    assert(roll < resting - .05f && roll > resting - .25f);
    assert(GAME_GameOrientation_get_const(h.world, h.player)->radians == face);
    assert(memcmp(&collider, GAME_GameCollider_get_const(h.world, h.player), sizeof collider) == 0);
    destroy_host(&h);
}

void test_encounter()
{
    query_snapshot(ECS_INDEX_DENSE);
    query_snapshot(ECS_INDEX_BITSET);
    accepted_damage();
    authored_feedback();
    pickup_lifecycle();
    telegraphed_arrivals();
    native_visual_motion();
    native_visual_birth_pose();
    combat_feedback_gates();
    enemy_contact_motion();
    ship_motion();
    authored_parts_at_birth(false);
    authored_parts_at_birth(true);
    final_experience();
    cross_batch_defeat();
    for (int preview = 0; preview < 2; preview++) {
        clear_encounter(preview);
        defeat_encounter(preview, false);
        defeat_encounter(preview, true);
    }
    puts("encounter PASS: query snapshots, accepted damage, floor pulses, ship motion, source shards, XP lifecycle and final collection, same-tick defeat precedence and fresh runs in both hosts");
}

static uint64_t reflected_world_hash(const ecs_world *world)
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    uint64_t hash = UINT64_C(1469598103934665603);
    for (uint32_t slot = 1; slot <= ecs_world_high_water_mark(world); slot++) {
        ecs_entity entity = { slot, ecs_world_generation(world, slot) };
        if (!ecs_entity_is_alive(world, entity)) {
            continue;
        }
        hash = (hash ^ ecs_entity_to_u64(entity)) * UINT64_C(1099511628211);
        for (int i = 0; i < ecs_world_component_count(world); i++) {
            const ecs_component_desc *component = ecs_world_component_at(world, i);
            if (!ecs_component_has(world, entity, component->id)) {
                continue;
            }
            /* Unauthored prototype tags receive process-global IDs. Compare
             * their presence; payload components retain their full identity. */
            if (!strcmp(component->type->name, "PrototypeTag") &&
                !strncmp(component->key, "prototype-", 10)) {
                hash = (hash ^ 1u) * UINT64_C(1099511628211);
                continue;
            }
            av_value *value = game_prototype_entity_component_value(world, entity, component, &registry);
            char *json = av_value_write(value);
            av_value_free(value);
            assert(json);
            for (const unsigned char *p = (const unsigned char *)json; *p; p++) {
                hash = (hash ^ *p) * UINT64_C(1099511628211);
            }
            free(json);
        }
    }
    return hash;
}

void test_encounter_backend_parity()
{
    uint64_t snapshots[2][90] = { 0 };
    behavior_backend modes[] = {
        BEHAVIOR_ORACLE,
        BEHAVIOR_BYTECODE,
#ifndef __EMSCRIPTEN__
        BEHAVIOR_SLJIT,
#endif
    };
    behavior_backend previous = behavior_debug_backend(BEHAVIOR_UNAVAILABLE);
    for (int mode = 0; mode < (int)(sizeof modes / sizeof *modes); mode++) {
        behavior_debug_backend(modes[mode]);
        for (int preview = 0; preview < 2; preview++) {
            srand(12345);
            uint64_t eval_before = eval_oracle_calls(),
                     flow_before = flow_oracle_calls();
            encounter_host h = create(preview);
            for (int frame = 0; frame < 90; frame++) {
                if (frame == 30 || frame == 40 || frame == 50) {
                    ecs_entity *targets = enemies(&h);
                    if (acount(targets)) {
                        damage(&h, targets[0], 30);
                    }
                    afree(targets);
                }
                simulation_step(&h.clock);
                uint64_t hash = reflected_world_hash(h.world);
                if (!mode) {
                    snapshots[preview][frame] = hash;
                }
                if (hash != snapshots[preview][frame]) {
                    fprintf(
                        stderr,
                        "backend parity mismatch: %s preview=%d frame=%d\n",
                        behavior_backend_name(modes[mode]),
                        preview,
                        frame
                    );
                }
                assert(hash == snapshots[preview][frame]);
                ecs_query query;
                assert(ecs_query_begin(h.world, &query, NULL, 0) == ECS_OK);
                ecs_entity entity;
                while (ecs_query_next(&query, &entity) > 0) {
                    int count = game_behavior_attachment_count_for_entity(behaviors(&h), entity);
                    for (int i = 0; i < count; i++) {
                        game_behavior_attachment_info info;
                        assert(
                            game_behavior_attachment_info_at(behaviors(&h), entity, i, &info) == 0
                        );
                        assert(behavior_instance_backend(info.instance) == modes[mode]);
                    }
                }
                ecs_query_end(&query);
            }
            if (modes[mode] != BEHAVIOR_ORACLE) {
                assert(eval_oracle_calls() == eval_before && flow_oracle_calls() == flow_before);
            }
            destroy_host(&h);
        }
    }
    behavior_debug_backend(previous);
    puts("PASS: native, bytecode and oracle frame hashes match for authored encounter spawning, damage and death in Scene and prototype hosts");
}
