#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "support/arena.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    behavior_asset *asset;
    node *presence;
    arena arena;
    eval_context eval;
    service_binding *services;
} presence_case;

static void expect_presence(presence_case *test, const char *component, bool expected, const char *error)
{
    eval_context_reset_runtime(&test->eval);
    arena_reset(&test->arena);
    pin_value input = { .type = PIN_NAME, .str = component };
    assert(graph_set_pin_default(test->asset->graph, test->presence, false, 0, &input) == 0);
    pin_value output;
    int result = graph_eval_output(test->asset->graph, test->presence, 0, &test->arena, &output, &test->eval);
    if (error) {
        assert(result < 0 && strstr(eval_context_status(&test->eval)->message, error));
        return;
    }
    assert(result == 0 && output.type == PIN_BOOL && output.b == expected);
}

static void check_presence(ecs_index_kind index)
{
    ecs_world *world = ecs_world_new();
    ecs_component_desc component = GAME_COMPONENT_GameInput;
    component.index = index;
    assert(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    game_live_world *live = game_live_world_new(world);
    presence_case test = { .asset = behavior_asset_new("Presence") };
    const node_desc *desc = registry_node_by_name("Has Entities With Component");
    assert(desc && (desc->access->flags & NODE_EFFECT_WORLD_READ));
    test.presence = graph_add_node(test.asset->graph, desc);
    eval_context_init(&test.eval);
    arena_init(&test.arena, 4096);
    apush(test.services, ((service_binding){ "game.entities", live->entities }));
    test.eval.services = test.services;
    expect_presence(&test, "", false, "Choose a component");
    expect_presence(&test, "missing.Component", false, "not registered");
    expect_presence(&test, "GAME.GameHealth", false, "not registered");
    expect_presence(&test, component.key, false, NULL);
    char *json = behavior_asset_save(test.asset);
    behavior_asset *restored = behavior_asset_load(json);
    assert(restored);
    free(json);
    eval_context_free(&test.eval);
    behavior_asset_free(test.asset);
    eval_context_init(&test.eval);
    test.eval.services = test.services;
    test.asset = restored;
    test.presence = restored->graph->nodes[0];
    assert(!strcmp(test.presence->input_pins[0].default_value.str, component.key));

    ecs_entity first = ecs_world_create_entity(world);
    expect_presence(&test, component.key, false, NULL);
    assert(GAME_GameInput_add(world, first, 0) == ECS_OK);
    expect_presence(&test, component.key, false, NULL);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    expect_presence(&test, component.key, true, NULL);
    assert(ecs_world_iteration_begin(world) == ECS_OK);
    expect_presence(&test, component.key, true, NULL);
    assert(ecs_world_iteration_end(world) == ECS_OK);
    assert(ecs_world_record_remove(world, first, component.id, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    expect_presence(&test, component.key, false, NULL);
    assert(GAME_GameInput_add(world, first, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_entities_request_destroy(live->entities, first);
    expect_presence(&test, component.key, false, NULL);

    ecs_entity second = ecs_world_create_entity(world);
    assert(GAME_GameInput_add(world, second, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    expect_presence(&test, component.key, true, NULL);
    assert(ecs_world_record_destroy(world, second, 0) == ECS_OK);
    expect_presence(&test, component.key, false, NULL);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(ecs_world_destroy_entity(world, first) == ECS_OK);
    ecs_entity replacement = ecs_world_create_entity(world);
    assert(replacement.slot == first.slot && replacement.generation != first.generation);
    assert(GAME_GameInput_add(world, replacement, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    expect_presence(&test, component.key, true, NULL);
    assert(game_entities_flush(live->entities) == 0);
    expect_presence(&test, component.key, true, NULL);
    assert(ecs_world_destroy_entity(world, replacement) == ECS_OK);
    expect_presence(&test, component.key, false, NULL);
    test.eval.services = NULL;
    expect_presence(&test, component.key, false, "requires a game world");
    afree(test.services);
    eval_context_free(&test.eval);
    arena_free(&test.arena);
    behavior_asset_free(test.asset);
    game_live_world_free(live);
    ecs_world_free(world);
}

void run_entity_presence_checks()
{
    check_presence(ECS_INDEX_DENSE);
    check_presence(ECS_INDEX_BITSET);
    puts("PASS: component presence survives serialization and follows structural changes and retirement");
}
