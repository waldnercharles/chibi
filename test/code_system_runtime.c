#include "assets/code_source.h"
#include "assets/system_document.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/document/serialize.h"
#include "graph/nodes/component_nodes.h"
#include "graph/runtime/instance.h"
#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/systems/system.h"
#include "support/array.h"
#include "support/text_file.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double private_value(game_system *system, ecs_entity entity, uint64_t id)
{
    pin_value value;
    assert(!game_system_entity_variable(system, entity, id, &value));
    assert(value.type == PIN_F64);
    return value.d;
}

static double shared_value(game_system *system, uint64_t id)
{
    pin_value value;
    assert(!game_system_shared_variable(system, id, &value));
    assert(value.type == PIN_F64);
    return value.d;
}

static void tick(game_system *system, double now)
{
    flow_error error = { 0 };
    int result = game_system_tick_mode(system, now, 0.1, &error, GAME_SYSTEM_EXEC_AUTO);
    if (result < 0) {
        fprintf(stderr, "System runtime: %s\n", error.message);
    }
    assert(result == FLOW_OK);
}

static void persistent_wait_membership()
{
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
    for (int i = 0; i < 256; i++) {
        ecs_world_create_entity(world);
    }
    ecs_entity a = ecs_world_create_entity(world), b = ecs_world_create_entity(world);
    assert(!GAME_GameHealth_add(world, a, 0));
    assert(!GAME_GameHealth_add(world, b, 0));
    assert(!ecs_world_apply_commands(world));
    code_source *source = code_source_parse(
        "system Counters { owner \"ecs.entity\"; state count := 0; shared "
        "total := 0; "
        "on Tick() { count += 1; total += 1; await delay(Duration: 1); "
        "count += 10; total += 10; } }"
    );
    if (!source->asset) {
        for (int i = 0; i < acount(source->diagnostics); i++) {
            fprintf(
                stderr,
                "System fixture %d:%d: %s\n",
                source->diagnostics[i].line,
                source->diagnostics[i].column,
                source->diagnostics[i].message
            );
        }
    }
    assert(source->asset);
    behavior_asset *definition = code_source_build_behavior(source);
    assert(definition);
    uint64_t count = behavior_asset_find_variable_by_name(definition, "count")->id;
    uint64_t total = behavior_asset_find_variable_by_name(definition, "total")->id;
    assert(behavior_asset_find_variable_by_id(definition, total)->shared);
    assert(!behavior_asset_find_variable_by_id(definition, count)->shared);
    behavior_asset *copy = behavior_asset_clone(definition);
    assert(behavior_asset_equal(copy, definition));
    behavior_instance *probe = behavior_instance_create(copy, NULL, NULL, NULL);
    assert(probe);
    behavior_asset *changed = behavior_asset_clone(definition);
    behavior_asset_find_variable_by_id(changed, total)->shared = false;
    assert(behavior_instance_can_update(probe, changed) == INSTANCE_UPDATE_RESTART);
    behavior_instance_destroy(probe);
    behavior_asset_free(copy);
    behavior_asset_free(changed);
    game_system *system = game_system_new(world, definition);
    assert(!compile_report_blocked(game_system_compile(system)));
    ecs_query_expr *query = ecs_query_expr_has(GAME_COMPONENT_GameHealth.id);
    assert(!game_system_set_query(system, query));
    ecs_query_expr_free(query);
    tick(system, 0);
    assert(game_system_retained_count(system) == 2);
    assert(game_system_suspended_count(system) == 2);
    assert(private_value(system, a, count) == 1 && private_value(system, b, count) == 1);
    assert(shared_value(system, total) == 2);
    tick(system, 0.25);
    assert(shared_value(system, total) == 2);
    assert(!GAME_GameHealth_remove(world, a, 0));
    assert(!ecs_world_apply_commands(world));
    tick(system, 0.5);
    pin_value value;
    assert(game_system_entity_variable(system, a, count, &value) < 0);
    assert(game_system_retained_count(system) == 1);
    assert(!GAME_GameHealth_add(world, a, 0));
    assert(!ecs_world_apply_commands(world));
    tick(system, 0.75);
    assert(private_value(system, a, count) == 1);
    assert(shared_value(system, total) == 3);
    tick(system, 1.1);
    assert(private_value(system, b, count) == 11);
    assert(shared_value(system, total) == 13);
    tick(system, 1.2);
    assert(private_value(system, b, count) == 12);
    assert(game_system_suspended_count(system) == 2);
    assert(!ecs_world_destroy_entity(world, a));
    ecs_entity replacement = ecs_world_create_entity(world);
    assert(replacement.slot == a.slot && replacement.generation != a.generation);
    assert(!GAME_GameHealth_add(world, replacement, 0));
    assert(!ecs_world_apply_commands(world));
    tick(system, 1.3);
    assert(game_system_entity_variable(system, a, count, &value) < 0);
    assert(private_value(system, replacement, count) == 1);
    assert(game_system_retained_count(system) == 2);
    game_system_free(system);
    code_source_free(source);
    ecs_world_free(world);
}

static void host_spawn_resume()
{
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
    game_live_world *live = game_live_world_new(world);
    game_prototype *prototype = game_prototype_new("Child");
    assert(!game_entities_register(live->entities, prototype));
    game_prototype_free(prototype);
    ecs_entity owner = ecs_world_create_entity(world);
    assert(!GAME_GameHealth_add(world, owner, 0));
    assert(!ecs_world_apply_commands(world));
    code_source *source = code_source_parse(
        "system Spawner { owner \"ecs.entity\"; state completed := 0; shared "
        "total := 0; "
        "on Tick() { await spawn(Prototype: \"Child\"); completed += "
        "1; total += 1; } }"
    );
    if (!source->asset) {
        for (int i = 0; i < acount(source->diagnostics); i++) {
            fprintf(
                stderr,
                "System fixture %d:%d: %s\n",
                source->diagnostics[i].line,
                source->diagnostics[i].column,
                source->diagnostics[i].message
            );
        }
    }
    assert(source->asset);
    behavior_asset *definition = code_source_build_behavior(source);
    uint64_t completed = behavior_asset_find_variable_by_name(definition, "completed")
                             ->id;
    uint64_t total = behavior_asset_find_variable_by_name(definition, "total")->id;
    game_system *system = game_system_new(world, definition);
    assert(!compile_report_blocked(game_system_compile(system)));
    ecs_query_expr *query = ecs_query_expr_has(GAME_COMPONENT_GameHealth.id);
    assert(!game_system_set_query(system, query));
    ecs_query_expr_free(query);
    engine *engine = engine_new(world, live);
    engine_system_desc desc = { .name = "Spawner", .graph = system };
    assert(!engine_register_system(engine, &desc));
    tick(system, 0);
    assert(game_system_suspended_count(system) == 1);
    assert(private_value(system, owner, completed) == 0);
    assert(game_entities_flush(live->entities) >= 0);
    assert(game_entities_flush_initializers(live->entities, 0) >= 0);
    assert(shared_value(system, total) == 1);
    tick(system, 0.1);
    assert(private_value(system, owner, completed) == 1);
    assert(game_system_suspended_count(system) == 0);
    assert(shared_value(system, total) == 1);
    tick(system, 0.2);
    assert(game_system_suspended_count(system) == 1);
    assert(!GAME_GameHealth_remove(world, owner, 0));
    assert(!ecs_world_apply_commands(world));
    /* The host safepoint validates current query membership before it can
     * create the second child or resume shared-state writes. */
    assert(game_entities_flush(live->entities) >= 0);
    assert(shared_value(system, total) == 1);
    ecs_entity *children = NULL;
    game_entities_copy_live(live->entities, &children);
    assert(acount(children) == 1);
    afree(children);
    tick(system, 0.3);
    assert(game_system_retained_count(system) == 0);
    engine_free(engine);
    game_system_free(system);
    code_source_free(source);
    game_live_world_free(live);
    ecs_world_free(world);
}

static void live_component_resume()
{
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
    ecs_entity owner = ecs_world_create_entity(world);
    assert(!GAME_GameHealth_add(world, owner, 0));
    assert(!ecs_world_apply_commands(world));
    GAME_GameHealth_get(world, owner)->current = 2;
    code_source *source = code_source_parse(
        "system ReadHealth(health: GameHealth) { state observed := 0; "
        "await delay(Duration: 1); observed = health.current; }"
    );
    if (!source->asset) {
        for (int i = 0; i < acount(source->diagnostics); i++) {
            fprintf(
                stderr,
                "System fixture %d:%d: %s\n",
                source->diagnostics[i].line,
                source->diagnostics[i].column,
                source->diagnostics[i].message
            );
        }
    }
    assert(source->asset);
    behavior_asset *definition = code_source_build_behavior(source);
    uint64_t observed = behavior_asset_find_variable_by_name(definition, "observed")
                            ->id;
    game_system *system = game_system_new(world, definition);
    assert(!compile_report_blocked(game_system_compile(system)));
    tick(system, 0);
    GAME_GameHealth_get(world, owner)->current = 7;
    tick(system, 1.1);
    assert(private_value(system, owner, observed) == 7);
    game_system_free(system);
    code_source_free(source);
    ecs_world_free(world);
}

static const ecs_component_desc *query_component(const char *name, void *)
{
    return component_by_key(name);
}

static const char *query_name(ecs_component_id id, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (component->id == id) {
            return component->key;
        }
    }
    return NULL;
}

static void optional_membership_roundtrip()
{
    const char
        *text = "system Optional(health: Health, trigger: ActionBinding?) "
                "without Collider { if trigger { if trigger.Pressed { "
                "health.Current += 7; } } else { health.Current += 2; } }";
    char error[256];
    game_system_document
        *document = game_system_document_load(text, query_component, NULL, error, sizeof error);
    if (!document) {
        fprintf(stderr, "System optional source: %s\n%s\n", error, text);
    }
    assert(document);
    char *saved = game_system_document_save(document, query_component, query_name, NULL);
    assert(saved && !strstr(saved, "on Tick"));
    for (int pass = 0; pass < 2; pass++) {
        ecs_world *world = ecs_world_new();
        const ecs_component_desc *components[] = { &GAME_COMPONENT_GameHealth,
                                                   &GAME_COMPONENT_GameActionBinding,
                                                   &GAME_COMPONENT_GameCollider };
        for (int i = 0; i < 3; i++) {
            assert(!ecs_world_register_component(world, components[i], NULL));
        }
        ecs_entity present = ecs_world_create_entity(world),
                   absent = ecs_world_create_entity(world),
                   excluded = ecs_world_create_entity(world);
        assert(!GAME_GameHealth_add(world, present, 0));
        assert(!GAME_GameHealth_add(world, absent, 0));
        assert(!GAME_GameHealth_add(world, excluded, 0));
        assert(!GAME_GameActionBinding_add(world, present, 0));
        assert(!GAME_GameCollider_add(world, excluded, 0));
        assert(!ecs_world_apply_commands(world));
        GAME_GameHealth_get(world, present)->current = 0;
        GAME_GameHealth_get(world, absent)->current = 0;
        GAME_GameHealth_get(world, excluded)->current = 0;
        GAME_GameActionBinding_get(world, present)->pressed = true;
        behavior_asset *definition = behavior_asset_clone(
            game_system_document_behavior_asset(document)
        );
        game_system *system = game_system_new(world, definition);
        assert(!compile_report_blocked(game_system_compile(system)));
        assert(!game_system_set_query(system, game_system_document_query(document)));
        tick(system, 0);
        if (game_system_match_count(system) != 2) {
            fprintf(
                stderr,
                "Optional pass %d: matches=%d required=%d\n%s\n",
                pass,
                game_system_match_count(system),
                game_system_query_count(system),
                pass ? saved : text
            );
        }
        assert(game_system_match_count(system) == 2);
        assert(GAME_GameHealth_get_const(world, present)->current == 7);
        assert(GAME_GameHealth_get_const(world, absent)->current == 2);
        assert(GAME_GameHealth_get_const(world, excluded)->current == 0);
        assert(!GAME_GameActionBinding_remove(world, present, 0));
        assert(!ecs_world_apply_commands(world));
        tick(system, 0.1);
        if (game_system_match_count(system) != 2) {
            fprintf(
                stderr,
                "Optional pass %d: matches=%d required=%d\n%s\n",
                pass,
                game_system_match_count(system),
                game_system_query_count(system),
                pass ? saved : text
            );
        }
        assert(game_system_match_count(system) == 2);
        assert(GAME_GameHealth_get_const(world, present)->current == 9);
        assert(GAME_GameHealth_get_const(world, absent)->current == 4);
        assert(GAME_GameHealth_get_const(world, excluded)->current == 0);
        game_system_free(system);
        ecs_world_free(world);
        game_system_document_free(document);
        document = pass == 0
                     ? game_system_document_load(saved, query_component, NULL, error, sizeof error)
                     : NULL;
        if (pass == 0 && !document) {
            fprintf(stderr, "System optional reload: %s\n%s\n", error, saved);
        }
        assert(pass != 0 || document);
    }
    free(saved);
}

static void reactive_checkpoint()
{
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameActionBinding, NULL));
    ecs_entity owner = ecs_world_create_entity(world);
    assert(!GAME_GameActionBinding_add(world, owner, 0));
    assert(!ecs_world_apply_commands(world));
    code_source *source = code_source_parse(
        "system Watch { state changes := 0; "
        "bind self.ActionBinding.Disabled = !self.ActionBinding.Pressed; "
        "on self.ActionBinding.PressedChanged(pressed) { "
        "changes += 1; await delay(Duration: 1); changes += 10; } on Tick() {} "
        "}"
    );
    assert(source->asset);
    uint64_t changes = source->asset->variables[0].id;
    game_system *system = game_system_new(world, code_source_build_behavior(source));
    assert(!compile_report_blocked(game_system_compile(system)));
    ecs_query_expr *query = ecs_query_expr_has(GAME_COMPONENT_GameActionBinding.id);
    assert(!game_system_set_query(system, query));
    ecs_query_expr_free(query);
    flow_error error = { 0 };
    assert(!game_system_maintain(system, 0, 0.1, &error));
    assert(GAME_GameActionBinding_get(world, owner)->disabled);
    assert(private_value(system, owner, changes) == 0);
    GameActionBinding *binding = GAME_GameActionBinding_get(world, owner);
    binding->pressed = true;
    pin_value value = { .type = PIN_BOOL, .type_name = "bool", .b = true };
    assert(!game_system_field_changed(
        system,
        owner,
        GAME_COMPONENT_GameActionBinding.id,
        offsetof(GameActionBinding, pressed),
        value,
        &error
    ));
    assert(private_value(system, owner, changes) == 1);
    assert(game_system_suspended_count(system) == 1);
    binding->pressed = false;
    value.b = false;
    assert(!game_system_field_changed(
        system,
        owner,
        GAME_COMPONENT_GameActionBinding.id,
        offsetof(GameActionBinding, pressed),
        value,
        &error
    ));
    assert(!game_system_maintain(system, 0.2, 0.1, &error));
    assert(binding->disabled && private_value(system, owner, changes) == 1);
    tick(system, 1.1);
    assert(private_value(system, owner, changes) == 11);
    assert(!game_system_suspended_count(system));
    assert(!game_system_maintain(system, 1.2, 0.1, &error));
    assert(private_value(system, owner, changes) == 11);
    binding->pressed = true;
    value.b = true;
    assert(!game_system_field_changed(
        system,
        owner,
        GAME_COMPONENT_GameActionBinding.id,
        offsetof(GameActionBinding, pressed),
        value,
        &error
    ));
    assert(private_value(system, owner, changes) == 12);
    assert(!GAME_GameActionBinding_remove(world, owner, 0));
    assert(!ecs_world_apply_commands(world));
    assert(!game_system_maintain(system, 1.3, 0.1, &error));
    assert(!game_system_retained_count(system));
    game_system_free(system);
    code_source_free(source);
    ecs_world_free(world);
}

static void synchronous_batch_path()
{
    char *text = text_file_read("test/fixtures/systems/MovementSystem.code");
    assert(text);
    code_source *source = code_source_parse(text);
    free(text);
    assert(source->asset);
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL));
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL));
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameInput, NULL));
    game_system *system = game_system_new(world, code_source_build_behavior(source));
    assert(!compile_report_blocked(game_system_compile(system)));
    assert(game_system_uses_bytecode(system));
    assert(!graph_sljit_available() || game_system_uses_sljit_batch(system));
    tick(system, 0);
    assert(game_system_retained_count(system) == 0);
    game_system_free(system);
    code_source_free(source);
    ecs_world_free(world);
}

void test_code_system_runtime()
{
    persistent_wait_membership();
    host_spawn_resume();
    live_component_resume();
    synchronous_batch_path();
    optional_membership_roundtrip();
    reactive_checkpoint();
}
