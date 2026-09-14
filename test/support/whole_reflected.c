#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "runtime/components.h"
#include "test/whole_reflected_fixture.h"

#include <assert.h>
#include <stdio.h>

behavior_asset *whole_reflected_fixture(int kind, bool waits)
{
    const char *names[] = { "Primitive Part", "Transform Visual Offset", "split_values" };
    assert(kind >= 0 && kind < 3);
    const node_desc *desc = registry_node_by_name(names[kind]);
    assert(desc && desc->host_call && !desc->host_call->service);
    behavior_asset *a = behavior_asset_new(names[kind]);
    graph *g = behavior_asset_add_function(a, "Apply", false);
    node *native = graph_add_node(g, desc);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, event, "Run"));
    for (int p = 0; p < desc->input_count; p++) {
        pin_spec spec = desc->inputs[p];
        assert(graph_add_pin(g, g->nodes[0], true, &spec) == p + 1);
        assert(graph_add_pin(a->graph, event, true, &spec) == p + 1);
        assert(!graph_connect_data(g, g->nodes[0], p + 1, native, p));
    }
    /* Include both aggregate values and exposed projections in the same read. */
    for (int p = 0; p < desc->output_count; p++) {
        if (desc->outputs[p].type == PIN_STRUCT) {
            assert(!graph_expose_output_fields(g, native, p));
        }
    }
    for (int p = 0; p < native->output_count; p++) {
        pin_spec spec = native->output_pins[p];
        spec.field_source = 0;
        spec.field_name = NULL;
        assert(graph_add_pin(g, g->nodes[1], false, &spec) == p + 1);
        assert(!graph_connect_data(g, native, p, g->nodes[1], p + 1));
        char name[24];
        snprintf(name, sizeof name, "Output %d", p);
        assert(
            behavior_asset_add_variable(a, name, spec.type, spec.type_name, PIN_CONTAINER_SINGLE, NULL, "")
        );
    }
    node *tail = g->nodes[0];
    if (waits) {
        node *delay = graph_add_node(g, &NODE_Delay);
        assert(
            !graph_set_pin_default(g, delay, false, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(g, tail, 0, delay, 0, 0));
        tail = delay;
    }
    assert(!graph_connect_exec(g, tail, 0, g->nodes[1], 0, 0));
    node *call = behavior_asset_add_function_call_node(a, g);
    assert(!graph_connect_exec(a->graph, event, 0, call, 0, 0));
    for (int p = 0; p < desc->input_count; p++) {
        assert(!graph_connect_data(a->graph, event, p + 1, call, p + 1));
    }
    tail = call;
    for (int p = 0; p < native->output_count; p++) {
        node *set = behavior_asset_add_set_node(a, a->variables[p].id);
        assert(!graph_connect_exec(a->graph, tail, 0, set, 0, 0));
        assert(!graph_connect_data(a->graph, call, p + 1, set, 1));
        tail = set;
    }
    return a;
}
void whole_reflected_smoke()
{
    behavior_asset *a = whole_reflected_fixture(0, true);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    GameGeometry3D geometry = { .primitive = GAME_PRIMITIVE_BOX_FRAME,
                                .width = 2,
                                .height = 3,
                                .depth = 4 };
    pin_value args[] = {
        { .type = PIN_STRUCT, .type_name = "GameGeometry3D", .ptr = &geometry },
        { .type = PIN_I32, .i = 7 }
    };
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ args, 2 }, NULL);
    assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_SUSPENDED);
    geometry.width = 99;
    assert(!behavior_instance_tick(bi, .125) && flow_run_status(run) == FLOW_COMPLETED);
    assert(bi->variables[0].i == 12 && bi->variables[1].i == GAME_PRIMITIVE_BOX);
    assert(bi->variables[4].f == .18f * 2);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    printf(
        "PASS: %s reflected native read, projected struct fields and caller "
        "snapshots across wait; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
