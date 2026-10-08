#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/whole_store_fixture.h"

#include <assert.h>
#include <stdio.h>

static node *variable(behavior_asset *a, graph *g, bool write)
{
    node *n = graph_add_node(g, write ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, a->variables[0].id, "Record"));
    assert(!graph_set_pin_type(g, n, 1, write ? 1 : 0, PIN_STRUCT, "WholeRecord"));
    if (write) {
        assert(!graph_set_pin_type(g, n, 0, 1, PIN_STRUCT, "WholeRecord"));
    }
    return n;
}
behavior_asset *whole_store_fixture(int kind)
{
    behavior_asset *a = whole_aggregate_fixture();
    graph *g = behavior_asset_add_function(a, "Store", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    pin_spec record = { .name = "Record", .type = PIN_STRUCT, .type_name = "WholeRecord" };
    assert(graph_add_pin(g, entry, 1, &record) == 1);
    assert(graph_add_pin(g, ret, 0, &record) == 1);
    node *set = variable(a, g, true);
    assert(!graph_connect_exec(g, entry, 0, set, 0, 0));
    assert(!graph_connect_data(g, entry, 1, set, 1));
    assert(!graph_connect_data(g, set, 1, ret, 1));
    node *last = set;
    if (kind == WHOLE_STORE_MAKE) {
        node *move = behavior_asset_add_function_call_node_to_graph(a, g, a->graphs[2]);
        assert(!graph_connect_data(g, entry, 1, move, 0));
        assert(!graph_set_pin_default(g, move, 0, 1, &(pin_value){ .type = PIN_F32, .f = .25f }));
        assert(!graph_connect_data(g, move, 0, set, 1));
    }
    if (kind == WHOLE_STORE_SNAPSHOT) {
        last = variable(a, g, true);
        assert(
            !graph_set_pin_default(g, last, 0, 1, &(pin_value){ .type = PIN_STRUCT, .type_name = "WholeRecord" })
        );
        assert(!graph_connect_exec(g, set, 0, last, 0, 0));
    }
    if (kind == WHOLE_STORE_LOOP) {
        node *loop = graph_add_node(g, &NODE_ForLoop);
        assert(!graph_set_pin_default(g, loop, 0, 1, &(pin_value){ .type = PIN_I32, .i = 0 }));
        assert(!graph_set_pin_default(g, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 2 }));
        node *get = variable(a, g, false), *next = variable(a, g, true);
        node *move = behavior_asset_add_function_call_node_to_graph(a, g, a->graphs[2]);
        assert(!graph_connect_data(g, get, 0, move, 0));
        assert(!graph_set_pin_default(g, move, 0, 1, &(pin_value){ .type = PIN_F32, .f = .25f }));
        assert(!graph_connect_data(g, move, 0, next, 1));
        assert(!graph_connect_exec(g, set, 0, loop, 0, 0));
        assert(!graph_connect_exec(g, loop, 0, next, 0, 0));
        assert(!graph_connect_exec(g, loop, 2, ret, 0, 0));
        assert(!graph_connect_data(g, get, 0, ret, 1));
    } else {
        assert(!graph_connect_exec(g, last, 0, ret, 0, 0));
    }
    graph *outer = behavior_asset_add_function(a, "Nested store", false);
    entry = outer->nodes[0];
    ret = outer->nodes[1];
    assert(graph_add_pin(outer, entry, 1, &record) == 1);
    assert(graph_add_pin(outer, ret, 0, &record) == 1);
    node *call = behavior_asset_add_function_call_node_to_graph(a, outer, g);
    assert(!graph_connect_exec(outer, entry, 0, call, 0, 0));
    assert(!graph_connect_exec(outer, call, 0, ret, 0, 0));
    assert(!graph_connect_data(outer, entry, 1, call, 1));
    assert(!graph_connect_data(outer, call, 1, ret, 1));
    return a;
}
void whole_store_smoke()
{
    for (int kind = 0; kind < WHOLE_STORE_COUNT; kind++) {
        behavior_asset *a = whole_store_fixture(kind);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        void *backing = bi->execution.variable_storage[0].data;
        whole_record record = { .tag = 251,
                                .position = { 3, 4 },
                                .weight = 2.75,
                                .active = true,
                                .stamp = INT64_MIN + 7 };
        pin_value arg = { .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = &record };
        graph *g = a->graphs[4];
        flow_options options = { .instance = bi };
        flow_error error = { 0 };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &arg, 1 }, &options, &error);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        assert(run && !flow_run_resume(run) && run->whole_function_executed);
        const whole_record *result = flow_run_input_value(run, g->nodes[1], 1)->ptr;
        float moved = kind == WHOLE_STORE_LOOP ? 1.5f
                    : kind == WHOLE_STORE_MAKE ? .5f
                                               : 0;
        assert(
            result && result != backing && result->position.x == 3 + moved &&
            result->position.y == 4 - moved
        );
        assert(
            result->tag == record.tag && result->stamp == record.stamp &&
            result->active && result->weight == record.weight
        );
        assert(bi->execution.variables[0].ptr == (kind == WHOLE_STORE_SNAPSHOT ? NULL : backing));
        assert(bi->execution.variable_storage[0].data == backing);
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        flow_run_free(run);
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    puts("PASS: whole persistent structs, nested calls, loops, snapshots and reused storage; zero oracle calls");
}
