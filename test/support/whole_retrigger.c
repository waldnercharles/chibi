#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/state_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/whole_retrigger_fixture.h"

#include <assert.h>
#include <stdio.h>

static const pin_spec inputs[] = { { "Duration", PIN_F64, "double" },
                                   { "Value", PIN_I32, "int" },
                                   { "Depth", PIN_I32, "int" } };
static void signature(graph *g, node *entry)
{
    for (int i = 0; i < 3; i++) {
        assert(graph_add_pin(g, entry, true, &inputs[i]) == i + 1);
    }
}
static node *variable(behavior_asset *a, graph *g, int index, bool set)
{
    behavior_variable *v = &a->variables[index];
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, v->id, v->name));
    assert(!graph_set_pin_type(g, n, true, set ? 1 : 0, PIN_I32, "int"));
    if (set) {
        assert(!graph_set_pin_type(g, n, false, 1, PIN_I32, "int"));
    }
    return n;
}
static node *arithmetic(graph *g, const node_desc *desc)
{
    node *n = graph_add_node(g, desc);
    for (int i = 0; i < 2; i++) {
        assert(!graph_set_pin_type(g, n, false, i, PIN_I32, "int"));
    }
    assert(!graph_set_pin_type(g, n, true, 0, PIN_I32, "int"));
    assert(!graph_set_pin_default(g, n, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 }));
    return n;
}
static void arguments(graph *g, node *entry, node *call)
{
    for (int i = 1; i <= 3; i++) {
        assert(!graph_connect_data(g, entry, i, call, i));
    }
}
static node *body(behavior_asset *a, graph *g, node *entry, node *tail, int kind)
{
    node *sequence = graph_add_node(g, &NODE_Sequence);
    node *delay = graph_add_node(g, &NODE_RetriggerableDelay);
    node *set = variable(a, g, 0, true);
    assert(!graph_connect_data(g, entry, 1, delay, 1));
    assert(!graph_connect_data(g, entry, 2, set, 1));
    assert(!graph_connect_exec(g, delay, 0, set, 0, 0));
    node *first = delay;
    if (kind == WHOLE_RETRIGGER_LOOP) {
        first = graph_add_node(g, &NODE_ForLoop);
        assert(
            !graph_set_pin_default(g, first, false, 2, &(pin_value){ .type = PIN_I32, .i = 1 })
        );
        assert(!graph_connect_exec(g, first, 0, delay, 0, 0));
    }
    assert(!graph_connect_exec(g, sequence, 0, first, 0, 0));
    assert(!graph_connect_exec(g, sequence, 1, tail, 0, 0));
    return sequence;
}
behavior_asset *whole_retrigger_fixture(int kind)
{
    behavior_asset *a = behavior_asset_new("Compiled retrigger");
    behavior_asset_add_variable(a, "Value", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
    behavior_asset_add_variable(a, "Tails", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
    graph *g = a->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Run"));
    signature(g, event);
    node *get = variable(a, g, 1, false), *tail = variable(a, g, 1, true);
    node *add = arithmetic(g, &NODE_Add);
    assert(!graph_connect_data(g, get, 0, add, 0));
    assert(!graph_connect_data(g, add, 0, tail, 1));
    if (kind == WHOLE_RETRIGGER_DIRECT) {
        node *first = body(a, g, event, tail, kind);
        assert(!graph_connect_exec(g, event, 0, first, 0, 0));
        return a;
    }
    graph *fn = behavior_asset_add_function(a, "Wait", false);
    node *entry = fn->nodes[0], *ret = fn->nodes[1];
    signature(fn, entry);
    node *first = body(a, fn, entry, ret, kind);
    if (kind == WHOLE_RETRIGGER_RECURSIVE) {
        node *branch = graph_add_node(fn, &NODE_Branch);
        node *greater = graph_add_node(fn, &NODE_Integer_Greater);
        node *subtract = arithmetic(fn, &NODE_Subtract);
        node *call = behavior_asset_add_function_call_node_to_graph(a, fn, fn);
        assert(!graph_connect_data(fn, entry, 3, greater, 0));
        assert(!graph_connect_data(fn, greater, 0, branch, 1));
        assert(!graph_connect_data(fn, entry, 3, subtract, 0));
        assert(!graph_connect_data(fn, subtract, 0, call, 3));
        assert(!graph_connect_data(fn, entry, 1, call, 1));
        assert(!graph_connect_data(fn, entry, 2, call, 2));
        assert(!graph_connect_exec(fn, branch, 0, call, 0, 0));
        assert(!graph_connect_exec(fn, call, 0, ret, 0, 0));
        assert(!graph_connect_exec(fn, branch, 1, first, 0, 0));
        first = branch;
    }
    assert(!graph_connect_exec(fn, entry, 0, first, 0, 0));
    if (kind == WHOLE_RETRIGGER_NESTED) {
        graph *outer = behavior_asset_add_function(a, "Outer", false);
        signature(outer, outer->nodes[0]);
        node *call = behavior_asset_add_function_call_node_to_graph(a, outer, fn);
        arguments(outer, outer->nodes[0], call);
        assert(!graph_connect_exec(outer, outer->nodes[0], 0, call, 0, 0));
        assert(!graph_connect_exec(outer, call, 0, outer->nodes[1], 0, 0));
        fn = outer;
    }
    node *call = behavior_asset_add_function_call_node_to_graph(a, g, fn);
    arguments(g, event, call);
    assert(!graph_connect_exec(g, event, 0, call, 0, 0));
    assert(!graph_connect_exec(g, call, 0, tail, 0, 0));
    return a;
}

void whole_retrigger_smoke()
{
    for (int kind = 0; kind < WHOLE_RETRIGGER_COUNT; kind++) {
        behavior_asset *a = whole_retrigger_fixture(kind);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, true);
        pin_value args[] = { { .type = PIN_F64, .d = 1 },
                             { .type = PIN_I32, .i = 7 },
                             { .type = PIN_I32, .i = 3 } };
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        flow_run *first = behavior_instance_dispatch(bi, "Run", (flow_inputs){ args, 3 }, NULL);
        assert(first && flow_run_status(first) == FLOW_SUSPENDED);
        assert(first->whole_function_executed && !acount(first->children));
        assert(flow_run_wake_time(first) == 1);
        const flow_run *leaf = flow_run_active_leaf(first);
        assert(!behavior_instance_tick(bi, .5));
        args[1].i = 99;
        flow_run *second = behavior_instance_dispatch(bi, "Run", (flow_inputs){ args, 3 }, NULL);
        assert(second && flow_run_status(second) == FLOW_COMPLETED);
        assert(second->whole_function_executed && !acount(second->children));
        assert(flow_run_wake_time(first) == 1.5 && flow_run_wake_time(leaf) == 1.5);
        assert(bi->execution.variables[0].i == 0 && bi->execution.variables[1].i == 1);
        assert(!behavior_instance_tick(bi, 1.));
        int result = behavior_instance_tick(bi, 1.5);
        if (kind == WHOLE_RETRIGGER_LOOP) {
            assert(result >= 0 && flow_run_wake_time(first) == 2.5);
            result = behavior_instance_tick(bi, 2.5);
        }
        assert(!result && flow_run_status(first) == FLOW_COMPLETED);
        assert(bi->execution.variables[0].i == 7 && bi->execution.variables[1].i == 2);
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    printf(
        "PASS: %s Retriggerable Delay, independent Sequence tails, cached wait "
        "views, nested/recursive calls and loops; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
