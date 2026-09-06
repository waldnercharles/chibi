#include "../core/registry.h"
#include "../game/system.h"

#include <assert.h>
#include <string.h>

void test_system_host_wait()
{
    ecs_world *world = ecs_world_new();
    behavior_asset *asset = behavior_asset_new("UnsupportedSystemSpawn");
    node *event = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, event, "Tick");
    const node_desc *spawn_desc = registry_node_by_name("Spawn Prototype");
    assert(spawn_desc);
    node *spawn = graph_add_node(asset->graph, spawn_desc);
    graph_connect_exec(asset->graph, event, 0, spawn, 0, 0);
    compile_report ordinary;
    compile_report_init(&ordinary);
    behavior_asset_compile(asset, &ordinary);
    for (int i = 0; i < acount(ordinary.items); i++) {
        assert(ordinary.items[i].rule != CMPL_RULE_SYSTEM_HOST_WAIT);
    }
    compile_report_free(&ordinary);

    game_system *system = game_system_new(world, asset);
    const compile_report *report = game_system_compile(system);
    const compile_diagnostic *diagnostic = NULL;
    for (int i = 0; i < acount(report->items); i++) {
        if (report->items[i].rule == CMPL_RULE_SYSTEM_HOST_WAIT) {
            diagnostic = &report->items[i];
        }
    }
    assert(diagnostic && diagnostic->severity == CMPL_ERROR);
    assert(diagnostic->graph_id == asset->graph->graph_id);
    assert(diagnostic->node_id == spawn->id);
    assert(strstr(diagnostic->message, "entity Behavior"));
    flow_error error;
    assert(game_system_tick(system, 0, 0.016, &error) == FLOW_ERR_COMPILE);
    assert(error.node == spawn);
    game_system_free(system);

    asset = behavior_asset_new("OrdinarySystemDelay");
    event = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, event, "Tick");
    node *delay = graph_add_node(asset->graph, &NODE_Delay);
    graph_connect_exec(asset->graph, event, 0, delay, 0, 0);
    compile_report_init(&ordinary);
    behavior_asset_compile_for_system(asset, &ordinary);
    for (int i = 0; i < acount(ordinary.items); i++) {
        assert(ordinary.items[i].rule != CMPL_RULE_SYSTEM_HOST_WAIT);
    }
    compile_report_free(&ordinary);
    behavior_asset_free(asset);
    ecs_world_free(world);
}
