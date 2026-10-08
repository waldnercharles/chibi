#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "runtime/component_catalog.h"
#include "runtime/systems/system.h"
#include "suite_internal.h"

#include <assert.h>
#include <stdio.h>

static node *add(graph *g, const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    assert(desc);
    return graph_add_node(g, desc);
}

static void connect(graph *g, node *from, int output, node *to, int input)
{
    int connected = graph_connect_data(g, from, output, to, input);
    assert(!connected);
    (void)connected;
}

static behavior_asset *system_asset()
{
    behavior_asset *asset = behavior_asset_new("System nodes");
    int owned = behavior_asset_set_owner_type(asset, "ecs.entity");
    assert(!owned);
    (void)owned;
    return asset;
}

/* A System whose Tick runs `first`. */
static game_system *ticked(ecs_world *world, behavior_asset *asset, node *first)
{
    node *tick = add(asset->graph, "Event");
    int named = graph_set_event_name(asset->graph, tick, "Tick");
    int wired = graph_connect_exec(asset->graph, tick, 0, first, 0, 0);
    assert(!named && !wired);
    (void)named;
    (void)wired;
    return game_system_new(world, asset);
}

/* Event, Get_, Break_, a literal, calls, Make_ and Set_: health grows by one
 * a tick up to its maximum, in bytecode. */
static void supported_sample(ecs_world *world, ecs_entity entity)
{
    behavior_asset *asset = system_asset();
    graph *g = asset->graph;
    node *get = add(g, "Get_GameHealth");
    node *split = add(g, "Break_GameHealth");
    node *one = add(g, "Integer");
    node *sum = add(g, "Add");
    node *capped = add(g, "Min");
    node *make = add(g, "Make_GameHealth");
    node *set = add(g, "Set_GameHealth");
    pin_value step = { .type = PIN_I32, .type_name = "int", .i = 1 };
    int stepped = graph_set_pin_default(g, one, 0, 0, &step);
    assert(!stepped);
    (void)stepped;
    connect(g, get, 0, split, 0);
    connect(g, split, 0, sum, 0);
    connect(g, one, 0, sum, 1);
    connect(g, sum, 0, capped, 0);
    connect(g, split, 1, capped, 1);
    connect(g, capped, 0, make, 0);
    connect(g, split, 1, make, 1);
    connect(g, make, 0, set, 1);
    game_system *system = ticked(world, asset, set);
    *GAME_GameHealth_get(world, entity) = (GameHealth){ 3, 4 };
    flow_error error = { 0 };
    bool ran = game_system_tick_mode(system, 0, 1.0 / 60.0, &error, GAME_SYSTEM_EXEC_AUTO) ==
                   FLOW_OK &&
               game_system_tick_mode(system, 0, 1.0 / 60.0, &error, GAME_SYSTEM_EXEC_AUTO) ==
                   FLOW_OK;
    if (!ran) {
        printf("  System nodes: %s\n", error.message ? error.message : "");
    }
    const node *nodes[] = { get, split, one, sum, capped, make, set };
    bool offered = true;
    for (size_t i = 0; i < sizeof nodes / sizeof *nodes; i++) {
        offered &= game_system_node_supported(nodes[i]->desc);
    }
    test_check(
        offered && game_system_uses_bytecode(system) && ran &&
            GAME_GameHealth_get_const(world, entity)->current == 4,
        "System graphs of supported nodes compile to bytecode"
    );
    game_system_free(system);
}

/* Nodes the compiler cannot lower are not offered, and a System holding one
 * runs through the flow interpreter instead of bytecode. */
static void unsupported(ecs_world *world)
{
    static const char *const names[] = { "Branch",
                                         "Delay",
                                         "Set_GameHealth_current",
                                         "SetEntity_GameHealth",
                                         "Add_GameHealth",
                                         "Set_GameUi" };
    int missed = 0;
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        const node_desc *desc = registry_node_by_name(names[i]);
        assert(desc);
        behavior_asset *asset = system_asset();
        game_system *system = ticked(world, asset, graph_add_node(asset->graph, desc));
        if (game_system_node_supported(desc) ||
            compile_report_blocked(game_system_compile(system)) ||
            game_system_uses_bytecode(system)) {
            printf("  %s should run interpreted and not be offered\n", names[i]);
            missed++;
        }
        game_system_free(system);
    }
    static const char *const excluded[] = {
        "Self",          "GetEntity_GameHealth", "Has_GameHealth",
        "Get Component", "Destroy Entity",       "Set UI Text",
        "Shader Time"
    };
    for (size_t i = 0; i < sizeof excluded / sizeof *excluded; i++) {
        missed += game_system_node_supported(registry_node_by_name(excluded[i]));
    }
    test_check(!missed, "Systems offer no node their compiler cannot lower");
}

void test_system_nodes()
{
    ecs_world *world = ecs_world_new();
    int registered = ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL) ||
                     ecs_world_register_component(world, &GAME_COMPONENT_GameUi, NULL);
    ecs_entity entity = ecs_world_create_entity(world);
    int added = GAME_GameHealth_add(world, entity, 0) ||
                ecs_world_apply_commands(world);
    assert(!registered && !added);
    (void)registered;
    (void)added;
    supported_sample(world, entity);
    unsupported(world);
    ecs_world_free(world);
}
