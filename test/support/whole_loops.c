#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_function.h"
#include "graph/model/registry.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/whole_loop_fixture.h"

static node *variable(behavior_asset *a, graph *g, uint64_t id, bool set)
{
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(
        g,
        n,
        id,
        behavior_asset_find_variable_by_id(a, id)->name
    ));
    assert(!graph_set_pin_type(g, n, 1, set ? 1 : 0, PIN_I32, "int"));
    if (set) {
        assert(!graph_set_pin_type(g, n, 0, 1, PIN_I32, "int"));
    }
    return n;
}
static node *integer_binary(graph *g, const node_desc *desc)
{
    node *n = graph_add_node(g, desc);
    for (int i = 0; i < 2; i++) {
        assert(!graph_set_pin_type(g, n, 0, i, PIN_I32, "int"));
    }
    assert(!graph_set_pin_type(g, n, 1, 0, PIN_I32, "int"));
    return n;
}
static void integer_default(graph *g, node *n, int pin, int value)
{
    assert(!graph_set_pin_default(g, n, 0, pin, &(pin_value){ .type = PIN_I32, .i = value }));
}
behavior_asset *whole_loop_fixture(int kind)
{
    behavior_asset *a = behavior_asset_new("Compiled loops");
    uint64_t count = behavior_asset_add_variable(a, "Count", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                         ->id;
    uint64_t limit = behavior_asset_add_variable(
                         a,
                         "Limit",
                         PIN_I32,
                         "int",
                         PIN_CONTAINER_SINGLE,
                         &(pin_value){ .type = PIN_I32, .i = 1 },
                         ""
    )
                         ->id;
    graph *g = behavior_asset_add_function(a, "Iterate", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    pin_spec spec = { .name = "First", .type = PIN_I32, .type_name = "int" };
    assert(graph_add_pin(g, entry, 1, &spec) == 1);
    spec.name = "Last";
    assert(graph_add_pin(g, entry, 1, &spec) == 2);
    spec.name = "Index";
    assert(graph_add_pin(g, ret, 0, &spec) == 1);
    node *get = variable(a, g, count, false);
    node *add = integer_binary(g, &NODE_Add);
    integer_default(g, add, 1, 1);
    assert(!graph_connect_data(g, get, 0, add, 0));
    node *increment = variable(a, g, count, true);
    assert(!graph_connect_data(g, add, 0, increment, 1));
    if (kind == WHOLE_LOOP_OUTSIDE) {
        node *stop = graph_add_node(g, &NODE_Break);
        assert(!graph_connect_exec(g, entry, 0, stop, 0, 0));
        return a;
    }
    bool with_break = kind == WHOLE_LOOP_BREAK || kind == WHOLE_LOOP_NESTED;
    node *loop = graph_add_node(
        g,
        kind == WHOLE_LOOP_WHILE ? &NODE_WhileLoop
        : with_break             ? &NODE_ForLoopWithBreak
                                 : &NODE_ForLoop
    );
    assert(!graph_connect_exec(g, entry, 0, loop, 0, 0));
    int completed = kind == WHOLE_LOOP_WHILE ? 1 : 2;
    assert(!graph_connect_exec(g, loop, completed, ret, 0, 0));
    if (kind != WHOLE_LOOP_WHILE) {
        assert(!graph_connect_data(g, entry, 1, loop, with_break ? 2 : 1));
        assert(!graph_connect_data(
            g,
            kind == WHOLE_LOOP_SNAPSHOT ? variable(a, g, limit, false) : entry,
            kind == WHOLE_LOOP_SNAPSHOT ? 0 : 2,
            loop,
            with_break ? 3 : 2
        ));
        assert(!graph_connect_data(g, loop, 1, ret, 1));
    }
    if (kind == WHOLE_LOOP_EMPTY) {
        return a;
    }
    if (kind == WHOLE_LOOP_CALL) {
        graph *child = behavior_asset_add_function(a, "Break from callee", false);
        node *stop = graph_add_node(child, &NODE_Break);
        assert(!graph_connect_exec(child, child->nodes[0], 0, stop, 0, 0));
        node *call = behavior_asset_add_function_call_node_to_graph(a, g, child);
        assert(!graph_connect_exec(g, loop, 0, call, 0, 0));
        return a;
    }
    if (kind == WHOLE_LOOP_NESTED_CALL) {
        graph *child = behavior_asset_add_function(a, "Nested range", false);
        node *inner = graph_add_node(child, &NODE_ForLoop);
        integer_default(child, inner, 1, 1);
        integer_default(child, inner, 2, 2);
        node *child_get = variable(a, child, count, false);
        node *child_add = integer_binary(child, &NODE_Add);
        integer_default(child, child_add, 1, 1);
        node *child_set = variable(a, child, count, true);
        assert(!graph_connect_data(child, child_get, 0, child_add, 0));
        assert(!graph_connect_data(child, child_add, 0, child_set, 1));
        assert(!graph_connect_exec(child, child->nodes[0], 0, inner, 0, 0));
        assert(!graph_connect_exec(child, inner, 0, child_set, 0, 0));
        assert(!graph_connect_exec(child, inner, 2, child->nodes[1], 0, 0));
        node *call = behavior_asset_add_function_call_node_to_graph(a, g, child);
        assert(!graph_connect_exec(g, loop, 0, call, 0, 0));
        return a;
    }
    if (kind == WHOLE_LOOP_RANGE) {
        assert(!graph_connect_exec(g, loop, 0, increment, 0, 0));
        return a;
    }
    node *sequence = graph_add_node(g, &NODE_Sequence);
    if (kind == WHOLE_LOOP_NESTED) {
        node *inner = graph_add_node(g, &NODE_ForLoop);
        integer_default(g, inner, 1, 1);
        integer_default(g, inner, 2, 2);
        assert(!graph_connect_exec(g, loop, 0, inner, 0, 0));
        assert(!graph_connect_exec(g, inner, 0, sequence, 0, 0));
        /* The explicit input belongs to the outer node, but breaks the nearest
         * active loop. The outer range must still complete every iteration. */
        assert(!graph_connect_exec(g, sequence, 0, loop, 1, 0));
    } else {
        assert(!graph_connect_exec(g, loop, 0, sequence, 0, 0));
        node *first = kind == WHOLE_LOOP_RETURN ? ret
                    : kind == WHOLE_LOOP_SNAPSHOT ? variable(a, g, limit, true)
                                                  : graph_add_node(g, &NODE_Break);
        if (kind == WHOLE_LOOP_SNAPSHOT) {
            integer_default(g, first, 1, 12);
        }
        assert(!graph_connect_exec(g, sequence, 0, first, 0, 0));
    }
    node *tail = increment;
    if (kind == WHOLE_LOOP_WHILE) {
        node *denominator = variable(a, g, limit, false);
        node *divide = integer_binary(g, &NODE_Divide);
        integer_default(g, divide, 0, 1);
        assert(!graph_connect_data(g, denominator, 0, divide, 1));
        node *condition = graph_add_node(g, &NODE_Integer_Greater);
        assert(!graph_connect_data(g, divide, 0, condition, 0));
        assert(!graph_connect_data(g, condition, 0, loop, 1));
        tail = variable(a, g, limit, true);
        /* A second condition evaluation would divide by zero. */
        integer_default(g, tail, 1, 0);
    }
    assert(!graph_connect_exec(g, sequence, 1, tail, 0, 0));
    return a;
}
void whole_loop_smoke()
{
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int kind = 0; kind < WHOLE_LOOP_COUNT; kind++) {
        behavior_asset *a = whole_loop_fixture(kind);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, 0);
        graph *g = a->graphs[1];
        assert(behavior_program_function(
            behavior_program_graph(behavior_program_cached(a), g),
            g->nodes[0]
        ));
        pin_value args[] = { { .type = PIN_I32, .i = 3 }, { .type = PIN_I32, .i = 1 } };
        flow_options options = { .instance = bi };
        flow_error error = { 0 };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 2 }, &options, &error);
        assert(run);
        int result = flow_run_resume(run);
        assert(run->whole_function_executed);
        if (kind == WHOLE_LOOP_OUTSIDE || kind == WHOLE_LOOP_CALL) {
            assert(result == FLOW_ERR_STATE);
        } else {
            assert(!result);
            int expected = kind == WHOLE_LOOP_NESTED_CALL ? 6
                         : kind == WHOLE_LOOP_RANGE || kind == WHOLE_LOOP_NESTED ||
                                   kind == WHOLE_LOOP_SNAPSHOT
                             ? 3
                         : kind == WHOLE_LOOP_BREAK ? 1
                                                    : 0;
            assert(bi->execution.variables[0].i == expected);
        }
        flow_run_free(run);
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
}
