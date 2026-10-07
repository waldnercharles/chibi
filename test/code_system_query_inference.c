#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "runtime/generated.h"
#include "runtime/systems/system.h"
#include "suite_internal.h"

#include <assert.h>

static node *add(graph *g, const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    assert(desc);
    return graph_add_node(g, desc);
}

static void membership(ecs_world *world, const char *name, bool dynamic, bool explicit_self, int required)
{
    behavior_asset *asset = behavior_asset_new("Query inference");
    assert(!behavior_asset_set_owner_type(asset, "ecs.entity"));
    graph *g = asset->graph;
    node *tick = add(g, "Event");
    node *branch = add(g, "Branch");
    assert(!graph_set_event_name(g, tick, "Tick"));
    assert(!graph_connect_exec(g, tick, 0, branch, 0, 0));
    node *access = add(g, name);
    if (dynamic) {
        assert(!component_node_configure(g, access, "GAME.GameHealth", NULL));
    }
    if (explicit_self) {
        node *self = behavior_asset_add_self_node(asset);
        assert(self);
        int entity = dynamic ? component_node_setting(access) + 1 : 0;
        assert(!graph_connect_data(g, self, 0, access, entity));
    }
    game_system *system = game_system_new(world, asset);
    assert(system && !compile_report_blocked(game_system_compile(system)));
    flow_error error = { 0 };
    test_check(
        game_system_query_count(system) == required &&
            game_system_tick_mode(system, 0, 0, &error, GAME_SYSTEM_EXEC_AUTO) == FLOW_OK &&
            game_system_match_count(system) == (required ? 1 : 2),
        name
    );
    game_system_free(system);
}

void test_code_system_query_inference()
{
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
    ecs_entity healthy = ecs_world_create_entity(world);
    ecs_world_create_entity(world);
    assert(!GAME_GameHealth_add(world, healthy, 0));
    assert(!ecs_world_apply_commands(world));
    membership(world, "Get_GameHealth", false, false, 1);
    membership(world, "Get Component", true, false, 1);
    membership(world, "Has_GameHealth", false, false, 0);
    membership(world, "Has Component", true, false, 0);
    membership(world, "GetEntity_GameHealth", false, true, 0);
    membership(world, "Get Component", true, true, 0);
    membership(world, "Add_GameHealth", false, false, 0);
    membership(world, "SetEntity_GameHealth", false, false, 0);
    ecs_world_free(world);
}
