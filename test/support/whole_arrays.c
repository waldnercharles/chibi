#include "graph/model/registry.h"
#include "graph/nodes/array_nodes.h"
#include "graph/nodes/math_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/value_array.h"
#include "test/support/report.h"
#include "test/whole_array_fixture.h"

#include <assert.h>
#include <stdio.h>

static node *variable(behavior_asset *a, graph *g, int index, bool set)
{
    const behavior_variable *v = &a->variables[index];
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, v->id, v->name));
    assert(!graph_set_pin_type(g, n, 1, set ? 1 : 0, v->type, v->type_name));
    assert(!graph_set_pin_container(g, n, 1, set ? 1 : 0, v->container));
    if (set) {
        assert(!graph_set_pin_type(g, n, 0, 1, v->type, v->type_name));
        assert(!graph_set_pin_container(g, n, 0, 1, v->container));
    }
    return n;
}
behavior_asset *whole_array_fixture(int kind)
{
    behavior_asset *a = whole_aggregate_fixture();
    behavior_asset_add_variable(a, "Sum", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "");
    bool aggregate = kind == WHOLE_ARRAY_STRUCT;
    pin_spec values = { .name = "Values",
                        .type = aggregate ? PIN_STRUCT : PIN_F64,
                        .type_name = aggregate ? "WholeRecord" : "double",
                        .container = PIN_CONTAINER_ARRAY };
    behavior_asset_add_variable(a, "Array", values.type, values.type_name, PIN_CONTAINER_ARRAY, NULL, "");
    pin_spec result = { .name = "Sum", .type = PIN_F64, .type_name = "double" };
    pin_spec index = { .name = "Index", .type = PIN_I32, .type_name = "int" };
    graph *g = behavior_asset_add_function(a, "Iterate array", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    assert(graph_add_pin(g, entry, 1, &values) == 1);
    assert(graph_add_pin(g, entry, 1, &index) == 2);
    assert(graph_add_pin(g, ret, 0, &result) == 1);
    assert(graph_add_pin(g, ret, 0, &index) == 2);
    node *sum = variable(a, g, 2, false);
    assert(!graph_connect_data(g, sum, 0, ret, 1));
    if (kind == WHOLE_ARRAY_SUMMARY) {
        const node_desc *operations[] = { &NODE_ArrayLength,
                                          &NODE_ArrayLastIndex,
                                          &NODE_ArrayIsEmpty,
                                          &NODE_ArrayIsNotEmpty,
                                          &NODE_ArrayIsValidIndex };
        for (int i = 0; i < 5; i++) {
            node *n = graph_add_node(g, operations[i]);
            assert(!graph_connect_data(g, entry, 1, n, 0));
            if (i) {
                pin_spec output = { .name = operations[i]->name,
                                    .type = i > 1 ? PIN_BOOL : PIN_I32,
                                    .type_name = i > 1 ? "bool" : "int" };
                assert(graph_add_pin(g, ret, 0, &output) == i + 2);
            }
            assert(!graph_connect_data(g, n, 0, ret, i + 2));
            if (i == 4) {
                assert(!graph_connect_data(g, entry, 2, n, 1));
            }
        }
        assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));
        return a;
    }
    node *loop = graph_add_node(g, kind == WHOLE_ARRAY_BREAK ? &NODE_ForEachWithBreak : &NODE_ForEach);
    assert(!graph_connect_data(
        g,
        kind == WHOLE_ARRAY_VARIABLE ? variable(a, g, 3, false) : entry,
        kind == WHOLE_ARRAY_VARIABLE ? 0 : 1,
        loop,
        kind == WHOLE_ARRAY_BREAK ? 2 : 1
    ));
    assert(!graph_connect_data(g, loop, 2, ret, 2));
    assert(!graph_connect_exec(g, entry, 0, loop, 0, 0));
    assert(!graph_connect_exec(g, loop, 3, ret, 0, 0));
    if (kind == WHOLE_ARRAY_STRUCT) {
        pin_spec element = { .name = "Last", .type = PIN_STRUCT, .type_name = "WholeRecord" };
        assert(graph_add_pin(g, ret, 0, &element) == 3);
        assert(!graph_connect_data(g, loop, 1, ret, 3));
        assert(!graph_expose_output_fields(g, loop, 1));
    }
    if (kind == WHOLE_ARRAY_EMPTY_BODY) {
        return a;
    }
    node *add = graph_add_node(g, &NODE_Add), *set = variable(a, g, 2, true);
    assert(!graph_set_pin_type(g, add, 0, 0, PIN_F64, "double"));
    assert(!graph_set_pin_type(g, add, 0, 1, PIN_F64, "double"));
    assert(!graph_set_pin_type(g, add, 1, 0, PIN_F64, "double"));
    assert(!graph_connect_data(g, sum, 0, add, 0));
    if (kind == WHOLE_ARRAY_MATH) {
        node *sine = graph_add_node(g, &NODE_Sine);
        assert(!graph_connect_data(g, sum, 0, sine, 0));
        assert(!graph_connect_data(g, sine, 0, add, 0));
    }
    assert(!graph_connect_data(g, loop, aggregate ? 6 : 1, add, 1));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, loop, 0, set, 0, 0));
    if (kind == WHOLE_ARRAY_BREAK) {
        node *sequence = graph_add_node(g, &NODE_Sequence);
        assert(!graph_remove_edge(g, loop, 0, set, 0));
        assert(!graph_connect_exec(g, loop, 0, sequence, 0, 0));
        assert(!graph_connect_exec(g, sequence, 0, loop, 1, 0));
        assert(!graph_connect_exec(g, sequence, 1, set, 0, 0));
    }
    if (kind == WHOLE_ARRAY_RETURN) {
        assert(!graph_connect_exec(g, set, 0, ret, 0, 0));
    }
    if (kind == WHOLE_ARRAY_NESTED) {
        node *outer = graph_add_node(g, &NODE_ForLoop);
        assert(!graph_remove_edge(g, entry, 0, loop, 0));
        assert(!graph_set_pin_default(g, outer, 0, 2, &(pin_value){ .type = PIN_I32, .i = 1 }));
        assert(!graph_connect_exec(g, entry, 0, outer, 0, 0));
        assert(!graph_connect_exec(g, outer, 0, loop, 0, 0));
        assert(!graph_remove_edge(g, loop, 3, ret, 0));
        assert(!graph_connect_exec(g, outer, 2, ret, 0, 0));
    }
    if (kind == WHOLE_ARRAY_CALL) {
        graph *child = behavior_asset_add_function(a, "Consume array", false);
        /* Move the completed body to a child and exercise array parameters in
         * an effectful frame. The published root retains graph index 3. */
        a->graphs[3] = child;
        a->graphs[4] = g;
        node *ce = child->nodes[0], *cr = child->nodes[1];
        assert(graph_add_pin(child, ce, 1, &values) == 1);
        assert(graph_add_pin(child, ce, 1, &index) == 2);
        assert(graph_add_pin(child, cr, 0, &result) == 1);
        assert(graph_add_pin(child, cr, 0, &index) == 2);
        node *call = behavior_asset_add_function_call_node_to_graph(a, child, g);
        assert(!graph_connect_exec(child, ce, 0, call, 0, 0));
        assert(!graph_connect_exec(child, call, 0, cr, 0, 0));
        for (int i = 1; i < 3; i++) {
            assert(!graph_connect_data(child, ce, i, call, i));
            assert(!graph_connect_data(child, call, i, cr, i));
        }
    }
    return a;
}
void whole_array_smoke()
{
    behavior_asset *a = whole_array_fixture(WHOLE_ARRAY_CALL);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    pin_value items[] = { { .type = PIN_F64, .d = 1.5 },
                          { .type = PIN_F64, .d = 2.5 },
                          { .type = PIN_F64, .d = -1 } };
    pin_array array = { .element_type = PIN_F64,
                        .type_name = "double",
                        .items = items,
                        .count = 3,
                        .capacity = 3 };
    pin_value input = { .type = PIN_F64,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = "double",
                        .ptr = &array };
    graph *g = a->graphs[3];
    flow_options options = { .instance = bi };
    flow_error error = { 0 };
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &input, 1 }, &options, &error);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    assert(run && !flow_run_resume(run) && run->whole_function_executed);
    assert(bi->variables[2].d == 3 && flow_run_input_value(run, g->nodes[1], 2)->i == 2);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    flow_run_free(run);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    test_note("PASS: whole borrowed arrays, nested ForEach and direct element loads; zero oracle calls\n");
}
