#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "runtime/generated.h"
#include "runtime/rhythm.h"
#include "test/support/report.h"
#include "test/whole_rhythm_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

behavior_asset *whole_rhythm_fixture(bool waits)
{
    behavior_asset *a = behavior_asset_new("Rhythm reader");
    graph *read = behavior_asset_add_function(a, "Read", true);
    node *rhythm = graph_add_node(read, &NODE_Scene_Rhythm);
    for (int p = 0; p < 4; p++) {
        pin_spec spec = NODE_Scene_Rhythm.outputs[p];
        assert(graph_add_pin(read, read->nodes[1], false, &spec) == p);
        assert(!graph_connect_data(read, rhythm, p, read->nodes[1], p));
        assert(
            behavior_asset_add_variable(a, spec.name, PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "")
        );
    }
    graph *apply = behavior_asset_add_function(a, "Apply", false);
    node *call = behavior_asset_add_function_call_node_to_graph(a, apply, read);
    for (int p = 0; p < 4; p++) {
        pin_spec spec = NODE_Scene_Rhythm.outputs[p];
        assert(graph_add_pin(apply, apply->nodes[1], false, &spec) == p + 1);
        assert(!graph_connect_data(apply, call, p, apply->nodes[1], p + 1));
    }
    node *tail = apply->nodes[0];
    if (waits) {
        node *delay = graph_add_node(apply, &NODE_Delay);
        assert(
            !graph_set_pin_default(apply, delay, false, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(apply, tail, 0, delay, 0, 0));
        tail = delay;
    }
    assert(!graph_connect_exec(apply, tail, 0, apply->nodes[1], 0, 0));
    graph *g = a->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Tick"));
    call = behavior_asset_add_function_call_node(a, apply);
    assert(!graph_connect_exec(g, event, 0, call, 0, 0));
    tail = call;
    for (int p = 0; p < 4; p++) {
        node *set = behavior_asset_add_set_node(a, a->variables[p].id);
        assert(!graph_connect_exec(g, tail, 0, set, 0, 0));
        assert(!graph_connect_data(g, call, p + 1, set, 1));
        tail = set;
    }
    return a;
}
void whole_rhythm_smoke()
{
    behavior_asset *a = whole_rhythm_fixture(true);
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameRhythm, NULL));
    ecs_entity clock = ecs_world_create_entity(world);
    GameRhythm settings = { 60, .25f };
    assert(!ecs_world_record_add(world, clock, GAME_COMPONENT_GameRhythm.id, &settings, 0));
    assert(!ecs_world_apply_commands(world));
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!behavior_instance_bind_service(bi, "ecs.world", world));
    behavior_instance_set_trace_enabled(bi, false);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *run = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, NULL);
    assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_SUSPENDED);
    assert(!behavior_instance_tick(bi, .125));
    assert(flow_run_status(run) == FLOW_COMPLETED);
    assert(bi->variables[0].d == .125 && bi->variables[1].d == .25);
    assert(bi->variables[2].d == 1 && bi->variables[3].d == .25);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    ecs_world_free(world);
    test_note(
        "PASS: %s whole Scene Rhythm, nested calls and live clock after wait; "
        "zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
