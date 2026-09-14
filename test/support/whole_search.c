#include "graph/model/registry.h"
#include "graph/nodes/array_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/value_array.h"
#include "test/whole_search_fixture.h"

#include <assert.h>
#include <stdio.h>

static const type_desc kind = { .name = "SearchKind", .size = sizeof(int32_t) };
static const type_desc *const search_types[] = { &kind };
const registry_pack WHOLE_SEARCH_PACK = { .name = "Array search fixture",
                                          .types = search_types,
                                          .type_count = 1 };

behavior_asset *whole_search_fixture(pin_type type, bool contains, bool waits)
{
    behavior_asset *a = behavior_asset_new("Array search");
    const char *name = type == PIN_HANDLE ? "ecs.entity"
                     : type == PIN_ENUM   ? "SearchKind"
                                          : NULL;
    pin_spec values = { .name = "Values",
                        .type = type,
                        .type_name = name,
                        .container = PIN_CONTAINER_ARRAY };
    pin_spec item = { .name = "Item", .type = type, .type_name = name };
    pin_spec result = { .name = "Result", .type = contains ? PIN_BOOL : PIN_I32 };
    assert(
        behavior_asset_add_variable(a, "Result", result.type, NULL, PIN_CONTAINER_SINGLE, NULL, "")
    );
    graph *lookup = behavior_asset_add_function(a, "Lookup", true);
    assert(graph_add_pin(lookup, lookup->nodes[0], 1, &values) == 0);
    assert(graph_add_pin(lookup, lookup->nodes[0], 1, &item) == 1);
    assert(graph_add_pin(lookup, lookup->nodes[1], 0, &result) == 0);
    node *search = graph_add_node(lookup, contains ? &NODE_ArrayContains : &NODE_ArrayFind);
    int connected = graph_connect_data(lookup, lookup->nodes[0], 0, search, 0);
    if (connected) {
        fprintf(stderr, "search fixture array connection failed: type=%d contains=%d error=%d\n", type, contains, connected);
    }
    assert(!connected);
    assert(!graph_connect_data(lookup, lookup->nodes[0], 1, search, 1));
    assert(!graph_connect_data(lookup, search, 0, lookup->nodes[1], 0));
    graph *apply = behavior_asset_add_function(a, "Apply", false);
    assert(graph_add_pin(apply, apply->nodes[0], 1, &values) == 1);
    assert(graph_add_pin(apply, apply->nodes[0], 1, &item) == 2);
    assert(graph_add_pin(apply, apply->nodes[1], 0, &result) == 1);
    node *call = behavior_asset_add_function_call_node_to_graph(a, apply, lookup);
    assert(!graph_connect_data(apply, apply->nodes[0], 1, call, 0));
    assert(!graph_connect_data(apply, apply->nodes[0], 2, call, 1));
    assert(!graph_connect_data(apply, call, 0, apply->nodes[1], 1));
    node *tail = apply->nodes[0];
    if (waits) {
        node *delay = graph_add_node(apply, &NODE_Delay);
        assert(
            !graph_set_pin_default(apply, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(apply, tail, 0, delay, 0, 0));
        tail = delay;
    }
    assert(!graph_connect_exec(apply, tail, 0, apply->nodes[1], 0, 0));
    graph *g = a->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Run"));
    assert(graph_add_pin(g, event, 1, &values) == 1);
    assert(graph_add_pin(g, event, 1, &item) == 2);
    node *invoke = behavior_asset_add_function_call_node(a, apply);
    assert(!graph_connect_exec(g, event, 0, invoke, 0, 0));
    assert(!graph_connect_data(g, event, 1, invoke, 1));
    assert(!graph_connect_data(g, event, 2, invoke, 2));
    node *set = behavior_asset_add_set_node(a, a->variables[0].id);
    assert(!graph_connect_exec(g, invoke, 0, set, 0, 0));
    assert(!graph_connect_data(g, invoke, 1, set, 1));
    return a;
}
behavior_asset *whole_search_loop_fixture()
{
    behavior_asset *a = whole_search_fixture(PIN_I32, false, false);
    graph *apply = a->graphs[2];
    node *first = apply->nodes[2];
    node *second = behavior_asset_add_function_call_node_to_graph(a, apply, a->graphs[1]);
    assert(!graph_connect_data(apply, apply->nodes[0], 1, second, 0));
    assert(!graph_connect_data(apply, first, 0, second, 1));
    assert(!graph_remove_edge(apply, first, 0, apply->nodes[1], 1));
    assert(!graph_connect_data(apply, second, 0, apply->nodes[1], 1));
    graph *g = a->graph;
    node *event = g->nodes[0], *invoke = g->nodes[1];
    node *loop = graph_add_node(g, &NODE_ForLoop);
    assert(!graph_set_pin_default(g, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 4 }));
    assert(!graph_remove_edge(g, event, 0, invoke, 0));
    assert(!graph_remove_edge(g, event, 2, invoke, 2));
    assert(!graph_connect_exec(g, event, 0, loop, 0, 0));
    assert(!graph_connect_exec(g, loop, 0, invoke, 0, 0));
    assert(!graph_connect_data(g, loop, 1, invoke, 2));
    return a;
}
void whole_search_smoke()
{
    const pin_value needles[] = {
        { .type = PIN_I32, .i = 12 },
        { .type = PIN_F64, .d = 12 },
        { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = UINT64_MAX },
        { .type = PIN_STR, .str = "needle" },
        { .type = PIN_NAME, .str = "needle" }
    };
    for (int trial = 0; trial < 10; trial++) {
        bool contains = trial % 2;
        pin_value needle = needles[trial / 2];
        behavior_asset *a = whole_search_fixture(needle.type, contains, true);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        pin_value items[] = { { .type = needle.type, .type_name = needle.type_name },
                              needle,
                              needle };
        pin_array array = { .element_type = needle.type,
                            .type_name = needle.type_name,
                            .items = items,
                            .count = 3,
                            .capacity = 3 };
        pin_value args[] = { { .type = needle.type,
                               .type_name = needle.type_name,
                               .container = PIN_CONTAINER_ARRAY,
                               .ptr = &array },
                             needle };
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ args, 2 }, NULL);
        assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_SUSPENDED);
        items[1] = items[2] = items[0];
        assert(!behavior_instance_tick(bi, .125));
        assert(flow_run_status(run) == FLOW_COMPLETED);
        assert(contains ? bi->variables[0].b : bi->variables[0].i == 1);
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    puts("PASS: whole Contains/Find loops, nested calls and array snapshots across waits; zero oracle calls");
}
