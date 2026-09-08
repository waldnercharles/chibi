#include "editor/workspace/scene_debug.h"

#include "graph/document/serialize.h"
#include "graph/model/registry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void test_scene_debug()
{
    ecs_world *world = ecs_world_new();
    game_behavior_world *behaviors = game_behavior_world_new(world);
    behavior_asset *asset = behavior_asset_new("DebugIdentity");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    assert(game_behavior_register_asset(behaviors, "DebugIdentity", asset) == 0);
    ecs_entity first = ecs_world_create_entity(world);
    ecs_entity second = ecs_world_create_entity(world);
    assert(game_behavior_attach(behaviors, first, "DebugIdentity") == 0);
    assert(game_behavior_attach(behaviors, second, "DebugIdentity") == 0);
    game_behavior_attachment_info info, resolved;
    assert(game_behavior_attachment_info_at(behaviors, first, 0, &info) == 0);
    scene_debug debug = { 0 };
    scene_debug_select(&debug, &info, 7);
    assert(scene_debug_resolve(&debug, world, behaviors, 7, &resolved));
    assert(ecs_entity_to_u64(resolved.entity) == ecs_entity_to_u64(first));
    assert(!scene_debug_resolve(&debug, world, behaviors, 8, &resolved));
    assert(game_behavior_detach_asset(behaviors, first, "DebugIdentity") == 1);
    assert(!scene_debug_resolve(&debug, world, behaviors, 7, &resolved));
    assert(game_behavior_attach(behaviors, first, "DebugIdentity") == 0);
    assert(!scene_debug_resolve(&debug, world, behaviors, 7, &resolved));
    assert(game_behavior_attachment_info_at(behaviors, first, 0, &info) == 0);
    scene_debug_select(&debug, &info, 7);
    assert(scene_debug_resolve(&debug, world, behaviors, 7, &resolved));
    assert(game_behavior_detach_asset(behaviors, first, "DebugIdentity") == 1);
    assert(ecs_world_destroy_entity(world, first) == ECS_OK);
    ecs_entity replacement = ecs_world_create_entity(world);
    assert(ecs_entity_to_u64(replacement) != ecs_entity_to_u64(first));
    assert(game_behavior_attach(behaviors, replacement, "DebugIdentity") == 0);
    assert(!scene_debug_resolve(&debug, world, behaviors, 7, &resolved));
    scene_debug_clear(&debug);
    game_behavior_world_free(behaviors);
    behavior_asset_free(asset);
    ecs_world_free(world);

    behavior_asset *working = behavior_asset_new("DebugRevision");
    node *value = graph_add_node(working->graph, registry_node_by_name("Integer"));
    assert(value);
    char *json = behavior_asset_save_json(working);
    behavior_asset *applied = behavior_asset_load_json(json);
    free(json);
    assert(applied && scene_debug_matches(working, applied));
    value->x = 140;
    value->y = -80;
    assert(scene_debug_matches(working, applied));
    pin_value changed = { .type = PIN_I32, .i = 17 };
    assert(graph_set_pin_default(working->graph, value, 0, 0, &changed) == 0);
    assert(!scene_debug_matches(working, applied));
    node *runtime_value = graph_node_by_id(applied->graph, value->id);
    assert(graph_set_pin_default(applied->graph, runtime_value, 0, 0, &changed) == 0);
    assert(scene_debug_matches(working, applied));
    changed.i = 18;
    assert(graph_set_pin_default(applied->graph, runtime_value, 0, 0, &changed) == 0);
    assert(!scene_debug_matches(working, applied));
    behavior_asset_free(working);
    behavior_asset_free(applied);
    scene_debug_clear(&debug);
    puts("scene debug PASS: instance identity, session, detach/reattach and entity reuse; semantic edits invalidate while layout changes remain compatible");
}
