#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/support/report.h"
#include "test/whole_array_fixture.h"
#include "test/whole_delay_fixture.h"
#include "test/whole_loop_fixture.h"

#include <assert.h>
#include <stdio.h>

void whole_delay_add(behavior_asset *a)
{
    for (int j = 0; j < acount(a->graphs); j++) {
        graph *g = a->graphs[j];
        int count = acount(g->nodes);
        for (int i = 0; i < count; i++) {
            node *n = g->nodes[i];
            if (n->desc != &NODE_SetVariable) {
                continue;
            }
            const edge *edges;
            int outgoing = graph_exec_output_edges(g, n, 0, &edges);
            assert(outgoing <= 1);
            node *target = outgoing ? graph_node_by_id(g, edges[0].to_id) : NULL;
            int pin = 0, output = 0;
            if (target) {
                assert(graph_pin_by_id(target, edges[0].to_pin_id, &output, &pin));
                assert(!graph_remove_edge(g, n, 0, target, pin));
            }
            node *delay = graph_add_node(g, &NODE_Delay);
            assert(
                !graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
            );
            assert(!graph_connect_exec(g, n, 0, delay, 0, 0));
            if (target) {
                assert(!graph_connect_exec(g, delay, 0, target, pin, 0));
            }
        }
    }
}
behavior_asset *whole_delay_fixture(int kind)
{
    behavior_asset *a = whole_loop_fixture(kind);
    whole_delay_add(a);
    return a;
}

static void smoke(bool traced)
{
    behavior_asset *asset = whole_delay_fixture(WHOLE_LOOP_NESTED_CALL);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, traced);
    graph *g = asset->graphs[1];
    pin_value args[] = { { .type = PIN_I32, .i = 1 }, { .type = PIN_I32, .i = 4 } };
    flow_options options = { .instance = instance, .step_budget = 1000 };
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 2 }, &options, NULL);
    assert(run);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    int status = flow_run_resume(run), waits = 0;
    while (status == FLOW_SUSPENDED_RESULT) {
        assert(run->whole_function_executed && !acount(run->children));
        if (traced) {
            pin_value value;
            assert(!flow_run_watch_input(run, run->waiting_node, 1, &value));
            assert(value.type == PIN_F64 && value.d == .125);
            int count;
            assert(flow_run_traversals(run, &count) && count > 0);
        }
        double wake = flow_run_wake_time(run);
        assert(flow_run_tick_at(run, wake - .01) == FLOW_SUSPENDED_RESULT);
        status = flow_run_tick_at(run, wake);
        assert(++waits < 100);
    }
    assert(!status && waits == 8 && instance->variables[0].i == 8);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    flow_run_free(run);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}
void whole_delay_smoke()
{
    smoke(false);
    smoke(true);
    behavior_asset *asset = whole_array_fixture(WHOLE_ARRAY_CALL);
    whole_delay_add(asset);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, false);
    graph *g = asset->graphs[3];
    pin_value items[] = { { .type = PIN_F64, .type_name = "double", .d = 1.25 },
                          { .type = PIN_F64, .type_name = "double", .d = 2.5 } };
    pin_array array = { .element_type = PIN_F64,
                        .type_name = "double",
                        .items = items,
                        .count = 2,
                        .capacity = 2 };
    pin_value args[] = { { .type = PIN_F64,
                           .type_name = "double",
                           .container = PIN_CONTAINER_ARRAY,
                           .ptr = &array },
                         { .type = PIN_I32, .i = 1 } };
    flow_options options = { .instance = instance };
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 2 }, &options, NULL);
    assert(run);
    int result = flow_run_resume(run), waits = 0;
    while (result == FLOW_SUSPENDED_RESULT) {
        const flow_run *leaf = flow_run_active_leaf(run);
        assert(leaf != run && flow_run_call_depth(leaf) == 1);
        assert(flow_run_status(leaf) == FLOW_SUSPENDED);
        flow_inputs inputs = flow_run_inputs(leaf);
        assert(inputs.count == 2 && inputs.values[1].i == 1);
        const pin_array *copy = inputs.values[0].ptr;
        assert(
            copy && copy->count == 2 && copy->items[0].d == 1.25 &&
            copy->items[1].d == 2.5
        );
        result = flow_run_tick_at(run, flow_run_wake_time(run));
        assert(++waits < 4);
    }
    assert(!result && waits == 2);
    flow_run_free(run);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    test_note(
        "PASS: %s authored Delay and nested calls in whole compiled "
        "activations with frame inputs, watches and traversals; zero oracle "
        "calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
