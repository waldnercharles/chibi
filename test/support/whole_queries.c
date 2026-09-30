#include "graph/model/registry.h"
#include "graph/nodes/array_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "support/array.h"
#include "test/support/report.h"
#include "test/whole_query_fixture.h"

#include <stdlib.h>

static node *variable(behavior_asset *a, graph *g, int index, bool set)
{
    const behavior_variable *v = &a->variables[index];
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, v->id, v->name));
    assert(!graph_set_pin_type(g, n, 1, set ? 1 : 0, v->type, v->type_name));
    if (set) {
        assert(!graph_set_pin_type(g, n, 0, 1, v->type, v->type_name));
    }
    return n;
}
behavior_asset *whole_query_fixture(const node_desc *query, const char *key, int kind)
{
    behavior_asset *a = behavior_asset_new("Query body");
    assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    behavior_asset_add_variable(a, "Count", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
    behavior_asset_add_variable(a, "Last", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE, NULL, "");
    graph *g = behavior_asset_add_function(a, "Visit", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    pin_spec count = { .name = "Count", .type = PIN_I32, .type_name = "int" };
    pin_spec handle = { .name = "Last", .type = PIN_HANDLE, .type_name = "ecs.entity" };
    assert(graph_add_pin(g, ret, 0, &count) == 1);
    assert(graph_add_pin(g, ret, 0, &handle) == 2);
    node *find = graph_add_node(g, query);
    assert(!graph_set_pin_default(g, find, 0, 0, &(pin_value){ .type = PIN_NAME, .str = key }));
    node *each = graph_add_node(g, &NODE_ForEach);
    assert(!graph_connect_data(g, find, 0, each, 1));
    assert(!graph_connect_exec(g, entry, 0, each, 0, 0));
    assert(!graph_connect_exec(g, each, 3, ret, 0, 0));
    node *add = graph_add_node(g, registry_node_by_name("Add"));
    for (int i = 0; i < 2; i++) {
        assert(!graph_set_pin_type(g, add, 0, i, PIN_I32, "int"));
    }
    assert(!graph_set_pin_type(g, add, 1, 0, PIN_I32, "int"));
    node *read = variable(a, g, 0, false), *set = variable(a, g, 0, true);
    node *last = variable(a, g, 1, true);
    assert(!graph_connect_data(g, read, 0, add, 0));
    assert(!graph_set_pin_default(g, add, 0, 1, &(pin_value){ .type = PIN_I32, .i = 1 }));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, each, 0, set, 0, 0));
    assert(!graph_connect_exec(g, set, 0, last, 0, 0));
    assert(!graph_connect_data(g, each, 1, last, 1));
    assert(!graph_connect_data(g, read, 0, ret, 1));
    assert(!graph_connect_data(g, variable(a, g, 1, false), 0, ret, 2));
    if (kind == WHOLE_QUERY_BREAK) {
        node *stop = graph_add_node(g, &NODE_Break);
        assert(!graph_connect_exec(g, last, 0, stop, 0, 0));
    }
    if (kind == WHOLE_QUERY_RETURN) {
        assert(!graph_connect_exec(g, last, 0, ret, 0, 0));
    }
    if (kind == WHOLE_QUERY_REPEAT) {
        node *outer = graph_add_node(g, &NODE_ForLoop);
        assert(!graph_set_pin_default(g, outer, 0, 2, &(pin_value){ .type = PIN_I32, .i = 2 }));
        assert(!graph_remove_edge(g, entry, 0, each, 0));
        assert(!graph_remove_edge(g, each, 3, ret, 0));
        assert(!graph_connect_exec(g, entry, 0, outer, 0, 0));
        assert(!graph_connect_exec(g, outer, 0, each, 0, 0));
        assert(!graph_connect_exec(g, outer, 2, ret, 0, 0));
    }
    if (kind == WHOLE_QUERY_NESTED) {
        node *inner = graph_add_node(g, &NODE_ForEach);
        assert(!graph_connect_data(g, find, 0, inner, 1));
        assert(!graph_remove_edge(g, each, 0, set, 0));
        assert(!graph_connect_exec(g, each, 0, inner, 0, 0));
        assert(!graph_connect_exec(g, inner, 0, set, 0, 0));
    }
    if (kind == WHOLE_QUERY_CALL) {
        graph *wrapper = behavior_asset_add_function(a, "Call query", false);
        a->graphs[1] = wrapper;
        a->graphs[2] = g;
        assert(graph_add_pin(wrapper, wrapper->nodes[1], 0, &count) == 1);
        assert(graph_add_pin(wrapper, wrapper->nodes[1], 0, &handle) == 2);
        node *call = behavior_asset_add_function_call_node_to_graph(a, wrapper, g);
        assert(!graph_connect_exec(wrapper, wrapper->nodes[0], 0, call, 0, 0));
        assert(!graph_connect_exec(wrapper, call, 0, wrapper->nodes[1], 0, 0));
        assert(!graph_connect_data(wrapper, call, 1, wrapper->nodes[1], 1));
        assert(!graph_connect_data(wrapper, call, 2, wrapper->nodes[1], 2));
    }
    if (kind == WHOLE_QUERY_PURE) {
        graph *pure = behavior_asset_add_function(a, "Query size", true);
        node *size = graph_add_node(pure, &NODE_ArrayLength);
        node *producer = graph_add_node(pure, query);
        assert(
            !graph_set_pin_default(pure, producer, 0, 0, &(pin_value){ .type = PIN_NAME, .str = key })
        );
        assert(graph_add_pin(pure, pure->nodes[1], 0, &count) == 0);
        assert(!graph_connect_data(pure, producer, 0, size, 0));
        assert(!graph_connect_data(pure, size, 0, pure->nodes[1], 0));
        node *call = behavior_asset_add_function_call_node_to_graph(a, g, pure);
        assert(!graph_connect_data(g, call, 0, ret, 1));
        assert(!graph_remove_edge(g, entry, 0, each, 0));
        assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));
    }
    return a;
}
static const char *snapshot(void *, const char *, pin_array *out)
{
    assert(!pin_array_reserve(out, 3));
    out->count = 3;
    for (int i = 0; i < 3; i++) {
        out->items[i] = (pin_value){ .type = PIN_HANDLE,
                                     .type_name = "ecs.entity",
                                     .u64 = 123 + i };
    }
    return NULL;
}
static const char *read_snapshot(const eval_context *, const pin_value *, arena *a, pin_value *out)
{
    pin_array values = { .element_type = PIN_HANDLE, .type_name = "ecs.entity" };
    snapshot(NULL, NULL, &values);
    *out = (pin_value){ .type = PIN_HANDLE,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = "ecs.entity",
                        .ptr = pin_array_clone_to_arena(a, &values) };
    free(values.items);
    return NULL;
}
void whole_query_smoke()
{
    pin_spec inputs[] = { { .name = "Component", .type = PIN_NAME } };
    pin_spec outputs[] = { { .name = "Entities",
                             .type = PIN_HANDLE,
                             .type_name = "ecs.entity",
                             .container = PIN_CONTAINER_ARRAY } };
    node_desc query = { .name = "Portable query",
                        .input_count = 1,
                        .inputs = inputs,
                        .output_count = 1,
                        .outputs = outputs,
                        .pure_kind = NODE_PURE_READ,
                        .read_inputs = read_snapshot,
                        .query_service = "test.query",
                        .query = snapshot };
    behavior_asset *a = whole_query_fixture(&query, "test", WHOLE_QUERY_NESTED);
    graph *g = a->graphs[1];
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    flow_error error = { 0 };
    flow_options options = { .instance = bi };
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ 0 }, &options, &error);
    assert(run && !flow_run_resume(run) && run->whole_function_executed);
    assert(bi->variables[0].i == 9 && bi->variables[1].u64 == 125);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    flow_run_free(run);
    test_note(
        "%s: nested whole query snapshots; zero oracle calls\n",
        behavior_backend_name(behavior_instance_backend(bi))
    );
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}
