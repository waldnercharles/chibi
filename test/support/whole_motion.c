#include "graph/compiler/behavior_function.h"
#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/support/report.h"
#include "test/whole_motion_fixture.h"

#include <assert.h>
#include <stdio.h>

behavior_asset *whole_motion_fixture(int kind)
{
    const char *names[] = { "Advance Damped Spring",
                            "Quantize Angle",
                            "Smooth Step",
                            "Square Root",
                            "Floor",
                            "Ceiling",
                            "Round" };
    assert(kind >= 0 && kind < WHOLE_MOTION_COUNT);
    behavior_asset *asset = behavior_asset_new(names[kind]);
    graph *g = behavior_asset_add_function(asset, "Calculate", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    node *op = graph_add_node(g, registry_node_by_name(names[kind]));
    for (int i = 0; i < op->input_count; i++) {
        assert(graph_add_pin(g, entry, 1, &op->input_pins[i]) == i + 1);
        assert(!graph_connect_data(g, entry, i + 1, op, i));
    }
    for (int i = 0; i < op->output_count; i++) {
        assert(graph_add_pin(g, ret, 0, &op->output_pins[i]) == i + 1);
        assert(!graph_connect_data(g, op, i, ret, i + 1));
        const pin_spec *p = &op->output_pins[i];
        assert(
            behavior_asset_add_variable(asset, p->name, p->type, p->type_name, PIN_CONTAINER_SINGLE, NULL, "")
        );
    }
    assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(!graph_set_event_name(asset->graph, event, "Tick"));
    for (int i = 0; i < op->input_count; i++) {
        assert(graph_add_pin(asset->graph, event, 1, &op->input_pins[i]) == i + 1);
    }
    node *call = behavior_asset_add_function_call_node(asset, g);
    assert(!graph_connect_exec(asset->graph, event, 0, call, 0, 0));
    for (int i = 0; i < op->input_count; i++) {
        assert(!graph_connect_data(asset->graph, event, i + 1, call, i + 1));
    }
    node *previous = call;
    for (int i = 0; i < op->output_count; i++) {
        node *set = behavior_asset_add_set_node(asset, asset->variables[i].id);
        assert(!graph_connect_data(asset->graph, call, i + 1, set, 1));
        assert(!graph_connect_exec(asset->graph, previous, 0, set, 0, 0));
        previous = set;
    }
    return asset;
}

void whole_motion_smoke()
{
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int kind = 0; kind < WHOLE_MOTION_COUNT; kind++) {
        behavior_asset *asset = whole_motion_fixture(kind);
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(instance, false);
        pin_value args[6];
        node *event = asset->graph->nodes[0];
        for (int i = 1; i < event->output_count; i++) {
            args[i - 1] = event->output_pins[i].default_value;
        }
        if (kind == WHOLE_MOTION_SPRING) {
            args[2].f = 1;
            args[5].f = .125f;
        } else {
            args[0].d = .5;
        }
        flow_run *run = behavior_instance_dispatch(
            instance,
            "Tick",
            (flow_inputs){ args, event->output_count - 1 },
            NULL
        );
        assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_COMPLETED);
        if (kind == WHOLE_MOTION_SPRING) {
            assert(instance->variables[0].f > 0 && instance->variables[0].f < 1);
            assert(instance->variables[1].f > 0);
        }
        if (kind == WHOLE_MOTION_SMOOTH) {
            assert(instance->variables[0].d == .5);
        }
        behavior_instance_destroy(instance);
        behavior_asset_free(asset);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    test_note(
        "PASS: %s whole spring, angle, easing and scalar rounding; zero oracle "
        "calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
