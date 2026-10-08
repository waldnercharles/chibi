#ifndef TEST_WHOLE_FUNCTION_FIXTURE_H
#define TEST_WHOLE_FUNCTION_FIXTURE_H

#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"

#include <assert.h>

static behavior_asset *whole_function_fixture()
{
    behavior_asset *a = behavior_asset_new("WholeFunction");
    uint64_t x = behavior_asset_add_variable(a, "X", PIN_F32, "float", PIN_CONTAINER_SINGLE, NULL, "")
                     ->id;
    uint64_t y = behavior_asset_add_variable(a, "Y", PIN_F32, "float", PIN_CONTAINER_SINGLE, NULL, "")
                     ->id;
    graph *g = a->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, event, "Run") == 0);
    node *seq = graph_add_node(g, registry_node_by_name("Sequence"));
    node *init = behavior_asset_add_set_node(a, x);
    node *loop = graph_add_node(g, registry_node_by_name("WhileLoop"));
    node *get = behavior_asset_add_get_node(a, x);
    node *less = graph_add_node(g, registry_node_by_name("Less"));
    node *add = graph_add_node(g, &NODE_Add);
    for (int i = 0; i < 2; i++) {
        assert(graph_set_pin_type(g, add, 0, i, PIN_F32, "float") == 0);
    }
    assert(graph_set_pin_type(g, add, 1, 0, PIN_F32, "float") == 0);
    node *inc = behavior_asset_add_set_node(a, x);
    node *branch = graph_add_node(g, registry_node_by_name("Branch"));
    node *yes = behavior_asset_add_set_node(a, y);
    node *no = behavior_asset_add_set_node(a, y);
    assert(graph_set_pin_default(g, less, 0, 1, &(pin_value){ .type = PIN_F32, .f = 4 }) == 0);
    assert(graph_set_pin_default(g, add, 0, 1, &(pin_value){ .type = PIN_F32, .f = 1 }) == 0);
    assert(graph_set_pin_default(g, yes, 0, 1, &(pin_value){ .type = PIN_F32, .f = -1 }) == 0);
    assert(graph_set_pin_default(g, no, 0, 1, &(pin_value){ .type = PIN_F32, .f = 8 }) == 0);
    assert(graph_connect_data(g, get, 0, less, 0) == 0);
    assert(graph_connect_data(g, less, 0, loop, 1) == 0);
    assert(graph_connect_data(g, less, 0, branch, 1) == 0);
    assert(graph_connect_data(g, get, 0, add, 0) == 0);
    assert(graph_connect_data(g, add, 0, inc, 1) == 0);
    assert(graph_connect_exec(g, event, 0, seq, 0, 0) == 0);
    assert(graph_connect_exec(g, seq, 0, init, 0, 0) == 0);
    assert(graph_connect_exec(g, seq, 1, loop, 0, 0) == 0);
    assert(graph_connect_exec(g, loop, 0, inc, 0, 0) == 0);
    assert(graph_connect_exec(g, loop, 1, branch, 0, 0) == 0);
    assert(graph_connect_exec(g, branch, 0, yes, 0, 0) == 0);
    assert(graph_connect_exec(g, branch, 1, no, 0, 0) == 0);
    return a;
}

/* Mixed-width arguments, three returns, and an early Return inside Sequence.
 * The event calls through a wrapper so the leaf uses a nested activation. */
static behavior_asset *whole_arguments_fixture()
{
    behavior_asset *a = behavior_asset_new("Whole arguments");
    pin_spec pins[] = {
        { .name = "Wide",
          .kind = PIN_DATA,
          .type = PIN_F64,
          .type_name = "double",
          .default_value = { .type = PIN_F64, .d = 3.25 } },
        { .name = "Narrow",
          .kind = PIN_DATA,
          .type = PIN_F32,
          .type_name = "float",
          .default_value = { .type = PIN_F32, .f = -2.5f } },
        { .name = "Flag",
          .kind = PIN_DATA,
          .type = PIN_BOOL,
          .type_name = "bool",
          .default_value = { .type = PIN_BOOL, .b = true } },
    };
    for (int i = 0; i < 3; i++) {
        assert(
            behavior_asset_add_variable(a, pins[i].name, pins[i].type, pins[i].type_name, PIN_CONTAINER_SINGLE, NULL, "")
        );
    }
    graph *f = behavior_asset_add_function(a, "Bounds", 0);
    node *entry = graph_node_by_id(f, f->entry_id);
    node *ret = graph_node_by_id(f, f->return_id);
    for (int i = 0; i < 3; i++) {
        assert(graph_add_pin(f, entry, 1, &pins[i]) >= 0);
        assert(graph_add_pin(f, ret, 0, &pins[i]) >= 0);
    }
    node *min = graph_add_node(f, registry_node_by_name("Min"));
    node *max = graph_add_node(f, registry_node_by_name("Max"));
    for (int i = 0; i < 2; i++) {
        assert(graph_set_pin_type(f, min, 0, i, PIN_F32, "float") == 0);
        assert(graph_set_pin_type(f, max, 0, i, PIN_F64, "double") == 0);
    }
    assert(graph_set_pin_type(f, min, 1, 0, PIN_F32, "float") == 0);
    assert(graph_set_pin_type(f, max, 1, 0, PIN_F64, "double") == 0);
    assert(graph_connect_data(f, entry, 1, min, 0) == 0);
    assert(graph_connect_data(f, entry, 2, min, 1) == 0);
    assert(graph_connect_data(f, entry, 2, max, 0) == 0);
    assert(graph_connect_data(f, entry, 1, max, 1) == 0);
    assert(graph_connect_data(f, max, 0, ret, 1) == 0);
    assert(graph_connect_data(f, min, 0, ret, 2) == 0);
    assert(graph_connect_data(f, entry, 3, ret, 3) == 0);
    node *seq = graph_add_node(f, registry_node_by_name("Sequence"));
    node *loop = graph_add_node(f, registry_node_by_name("WhileLoop"));
    assert(graph_set_pin_default(f, loop, 0, 1, &(pin_value){ .type = PIN_BOOL, .b = true }) == 0);
    assert(graph_connect_exec(f, entry, 0, seq, 0, 0) == 0);
    assert(graph_connect_exec(f, seq, 0, ret, 0, 0) == 0);
    assert(graph_connect_exec(f, seq, 1, loop, 0, 0) == 0);
    /* If Return follows the Sequence continuation, this loop exhausts its budget. */
    graph *wrapper = behavior_asset_add_function(a, "Wrapped bounds", 0);
    node *wentry = graph_node_by_id(wrapper, wrapper->entry_id);
    node *wret = graph_node_by_id(wrapper, wrapper->return_id);
    for (int i = 0; i < 3; i++) {
        assert(graph_add_pin(wrapper, wentry, 1, &pins[i]) >= 0);
        assert(graph_add_pin(wrapper, wret, 0, &pins[i]) >= 0);
    }
    node *inner = behavior_asset_add_function_call_node_to_graph(a, wrapper, f);
    assert(graph_connect_exec(wrapper, wentry, 0, inner, 0, 0) == 0);
    assert(graph_connect_exec(wrapper, inner, 0, wret, 0, 0) == 0);
    for (int i = 1; i <= 3; i++) {
        assert(graph_connect_data(wrapper, wentry, i, inner, i) == 0);
        assert(graph_connect_data(wrapper, inner, i, wret, i) == 0);
    }
    graph *g = a->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, event, "Run") == 0);
    for (int i = 0; i < 3; i++) {
        assert(graph_add_pin(g, event, 1, &pins[i]) >= 0);
    }
    node *call = behavior_asset_add_function_call_node(a, wrapper);
    assert(graph_connect_exec(g, event, 0, call, 0, 0) == 0);
    node *previous = call;
    for (int i = 0; i < 3; i++) {
        node *set = behavior_asset_add_set_node(a, a->variables[i].id);
        assert(graph_connect_data(g, event, i + 1, call, i + 1) == 0);
        assert(graph_connect_data(g, call, i + 1, set, 1) == 0);
        assert(graph_connect_exec(g, previous, 0, set, 0, 0) == 0);
        previous = set;
    }
    return a;
}
/* Two calls to the same function must retain distinct results. Return reads a
 * Set Variable output after that variable has been overwritten. */
static behavior_asset *whole_scopes_fixture()
{
    behavior_asset *a = behavior_asset_new("Stored function outputs");
    uint64_t x = behavior_asset_add_variable(a, "X", PIN_F32, "float", PIN_CONTAINER_SINGLE, NULL, "")
                     ->id;
    uint64_t result = behavior_asset_add_variable(a, "Result", PIN_F32, "float", PIN_CONTAINER_SINGLE, NULL, "")
                          ->id;
    graph *f = behavior_asset_add_function(a, "Snapshot", 0);
    node *entry = graph_node_by_id(f, f->entry_id),
         *ret = graph_node_by_id(f, f->return_id);
    pin_spec spec = { .name = "Value", .type = PIN_F32, .type_name = "float", .kind = PIN_DATA };
    assert(graph_add_pin(f, ret, 0, &spec) >= 0);
    node *get = graph_add_node(f, &NODE_GetVariable);
    node *set = graph_add_node(f, &NODE_SetVariable);
    node *overwrite = graph_add_node(f, &NODE_SetVariable);
    assert(graph_set_node_reference(f, get, x, "X") == 0);
    assert(graph_set_pin_type(f, get, 1, 0, PIN_F32, "float") == 0);
    node *writes[] = { set, overwrite };
    for (int i = 0; i < 2; i++) {
        assert(graph_set_node_reference(f, writes[i], x, "X") == 0);
        assert(graph_set_pin_type(f, writes[i], 0, 1, PIN_F32, "float") == 0);
        assert(graph_set_pin_type(f, writes[i], 1, 1, PIN_F32, "float") == 0);
    }
    node *add = graph_add_node(f, &NODE_Add);
    for (int i = 0; i < 2; i++) {
        assert(graph_set_pin_type(f, add, 0, i, PIN_F32, "float") == 0);
    }
    assert(graph_set_pin_type(f, add, 1, 0, PIN_F32, "float") == 0);
    assert(graph_set_pin_default(f, add, 0, 1, &(pin_value){ .type = PIN_F32, .f = 1 }) == 0);
    assert(
        graph_set_pin_default(f, overwrite, 0, 1, &(pin_value){ .type = PIN_F32, .f = 99 }) == 0
    );
    assert(graph_connect_data(f, get, 0, add, 0) == 0);
    assert(graph_connect_data(f, add, 0, set, 1) == 0);
    assert(graph_connect_data(f, set, 1, ret, 1) == 0);
    assert(graph_connect_exec(f, entry, 0, set, 0, 0) == 0);
    assert(graph_connect_exec(f, set, 0, overwrite, 0, 0) == 0);
    assert(graph_connect_exec(f, overwrite, 0, ret, 0, 0) == 0);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, event, "Run") == 0);
    node *first = behavior_asset_add_function_call_node(a, f);
    node *second = behavior_asset_add_function_call_node(a, f);
    node *sum = graph_add_node(a->graph, &NODE_Add);
    node *save = behavior_asset_add_set_node(a, result);
    for (int i = 0; i < 2; i++) {
        assert(graph_set_pin_type(a->graph, sum, 0, i, PIN_F32, "float") == 0);
    }
    assert(graph_set_pin_type(a->graph, sum, 1, 0, PIN_F32, "float") == 0);
    assert(graph_connect_exec(a->graph, event, 0, first, 0, 0) == 0);
    assert(graph_connect_exec(a->graph, first, 0, second, 0, 0) == 0);
    assert(graph_connect_exec(a->graph, second, 0, save, 0, 0) == 0);
    assert(graph_connect_data(a->graph, first, 1, sum, 0) == 0);
    assert(graph_connect_data(a->graph, second, 1, sum, 1) == 0);
    assert(graph_connect_data(a->graph, sum, 0, save, 1) == 0);
    return a;
}

/* Pull calls stay on the general event runtime when the containing event has
 * host operations. Compile their complete pure body independently. */
static behavior_asset *whole_pure_fixture()
{
    behavior_asset *a = behavior_asset_new("Pure function bodies");
    uint64_t x = behavior_asset_add_variable(a, "X", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "")
                     ->id;
    pin_spec parameter = { .name = "X", .kind = PIN_DATA, .type = PIN_F64, .type_name = "double" };
    graph *leaf = behavior_asset_add_function(a, "Pair", 1);
    node *entry = graph_node_by_id(leaf, leaf->entry_id),
         *ret = graph_node_by_id(leaf, leaf->return_id);
    assert(graph_add_pin(leaf, entry, 1, &parameter) >= 0);
    const node_desc *ops[] = { &NODE_Add, &NODE_Multiply };
    for (int i = 0; i < 2; i++) {
        pin_spec output = parameter;
        output.name = i ? "Scaled" : "Shifted";
        assert(graph_add_pin(leaf, ret, 0, &output) >= 0);
        node *op = graph_add_node(leaf, ops[i]);
        for (int j = 0; j < 2; j++) {
            assert(graph_set_pin_type(leaf, op, 0, j, PIN_F64, "double") == 0);
        }
        assert(graph_set_pin_type(leaf, op, 1, 0, PIN_F64, "double") == 0);
        assert(
            graph_set_pin_default(leaf, op, 0, 1, &(pin_value){ .type = PIN_F64, .d = i ? 1.25 : .25 }) == 0
        );
        assert(graph_connect_data(leaf, entry, 0, op, 0) == 0);
        assert(graph_connect_data(leaf, op, 0, ret, i) == 0);
    }
    graph *wrapper = behavior_asset_add_function(a, "Combine pair", 1);
    entry = graph_node_by_id(wrapper, wrapper->entry_id);
    ret = graph_node_by_id(wrapper, wrapper->return_id);
    assert(graph_add_pin(wrapper, entry, 1, &parameter) >= 0);
    node *inner = behavior_asset_add_function_call_node_to_graph(a, wrapper, leaf);
    assert(graph_connect_data(wrapper, entry, 0, inner, 0) == 0);
    for (int i = 0; i < 2; i++) {
        pin_spec output = parameter;
        output.name = i ? "Product" : "Sum";
        assert(graph_add_pin(wrapper, ret, 0, &output) >= 0);
        node *op = graph_add_node(wrapper, ops[i]);
        for (int j = 0; j < 2; j++) {
            assert(graph_set_pin_type(wrapper, op, 0, j, PIN_F64, "double") == 0);
            assert(graph_connect_data(wrapper, inner, j, op, j) == 0);
        }
        assert(graph_set_pin_type(wrapper, op, 1, 0, PIN_F64, "double") == 0);
        assert(graph_connect_data(wrapper, op, 0, ret, i) == 0);
    }
    node *call = behavior_asset_add_function_call_node(a, wrapper);
    node *get = behavior_asset_add_get_node(a, x);
    assert(graph_connect_data(a->graph, get, 0, call, 0) == 0);
    return a;
}
#endif
