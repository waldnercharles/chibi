#ifndef TEST_WHOLE_INTEGER_FIXTURE_H
#define TEST_WHOLE_INTEGER_FIXTURE_H
#include "graph/model/registry.h"

#include <assert.h>

static behavior_asset *whole_integer_fixture(pin_type type, bool remainder_first)
{
    behavior_asset *a = behavior_asset_new("Integer function bodies");
    const char *type_name = type == PIN_I32 ? "int" : "int64";
    assert(
        behavior_asset_add_variable(a, "Started", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
    );
    pin_spec inputs[] = {
        { .name = "A", .kind = PIN_DATA, .type = type, .type_name = type_name },
        { .name = "B", .kind = PIN_DATA, .type = type, .type_name = type_name }
    };
    for (int i = 0; i < 2; i++) {
        inputs[i].default_value.type = type;
        if (type == PIN_I32) {
            inputs[i].default_value.i = i ? 3 : 17;
        } else {
            inputs[i].default_value.i64 = i ? 3 : 17;
        }
    }
    const char *names[] = { "Quotient", "Remainder", "Wrapped" };
    pin_spec outputs[3];
    for (int i = 0; i < 3; i++) {
        outputs[i] = (pin_spec){ .name = names[i],
                                 .kind = PIN_DATA,
                                 .type = type,
                                 .type_name = type_name };
        assert(
            behavior_asset_add_variable(a, names[i], type, type_name, PIN_CONTAINER_SINGLE, NULL, "")
        );
    }
    graph *leaf = behavior_asset_add_function(a, "Divide pair", 1);
    node *entry = graph_node_by_id(leaf, leaf->entry_id),
         *ret = graph_node_by_id(leaf, leaf->return_id);
    for (int i = 0; i < 2; i++) {
        assert(graph_add_pin(leaf, entry, 1, &inputs[i]) >= 0);
    }
    for (int i = 0; i < 3; i++) {
        assert(graph_add_pin(leaf, ret, 0, &outputs[i]) >= 0);
    }
    const node_desc *descs[] = { &NODE_Divide, &NODE_Modulo, &NODE_Multiply, &NODE_Add };
    node *ops[4];
    for (int i = 0; i < 4; i++) {
        ops[i] = graph_add_node(leaf, descs[i]);
        for (int j = 0; j < 2; j++) {
            assert(graph_set_pin_type(leaf, ops[i], 0, j, type, type_name) == 0);
        }
        assert(graph_set_pin_type(leaf, ops[i], 1, 0, type, type_name) == 0);
    }
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            assert(graph_connect_data(leaf, entry, j, ops[i], j) == 0);
        }
        assert(graph_connect_data(leaf, ops[i], 0, ret, remainder_first ? 1 - i : i) == 0);
    }
    assert(graph_connect_data(leaf, entry, 0, ops[2], 0) == 0);
    assert(graph_connect_data(leaf, entry, 0, ops[2], 1) == 0);
    assert(graph_connect_data(leaf, ops[2], 0, ops[3], 0) == 0);
    assert(graph_connect_data(leaf, entry, 1, ops[3], 1) == 0);
    assert(graph_connect_data(leaf, ops[3], 0, ret, 2) == 0);
    graph *wrapper = behavior_asset_add_function(a, "Return integers", 0);
    node *we = graph_node_by_id(wrapper, wrapper->entry_id),
         *wr = graph_node_by_id(wrapper, wrapper->return_id);
    for (int i = 0; i < 2; i++) {
        assert(graph_add_pin(wrapper, we, 1, &inputs[i]) >= 0);
    }
    for (int i = 0; i < 3; i++) {
        assert(graph_add_pin(wrapper, wr, 0, &outputs[i]) >= 0);
    }
    node *inner = behavior_asset_add_function_call_node_to_graph(a, wrapper, leaf);
    for (int i = 0; i < 2; i++) {
        assert(graph_connect_data(wrapper, we, i + 1, inner, i) == 0);
    }
    for (int i = 0; i < 3; i++) {
        assert(graph_connect_data(wrapper, inner, i, wr, i + 1) == 0);
    }
    assert(graph_connect_exec(wrapper, we, 0, wr, 0, 0) == 0);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, event, "Run") == 0);
    for (int i = 0; i < 2; i++) {
        assert(graph_add_pin(a->graph, event, 1, &inputs[i]) >= 0);
    }
    node *started = behavior_asset_add_set_node(a, a->variables[0].id);
    assert(
        graph_set_pin_default(a->graph, started, 0, 1, &(pin_value){ .type = PIN_I32, .i = 1 }) == 0
    );
    node *call = behavior_asset_add_function_call_node(a, wrapper);
    assert(graph_connect_exec(a->graph, event, 0, started, 0, 0) == 0);
    assert(graph_connect_exec(a->graph, started, 0, call, 0, 0) == 0);
    for (int i = 0; i < 2; i++) {
        assert(graph_connect_data(a->graph, event, i + 1, call, i + 1) == 0);
    }
    node *previous = call;
    for (int i = 0; i < 3; i++) {
        node *set = behavior_asset_add_set_node(a, a->variables[i + 1].id);
        assert(graph_connect_data(a->graph, call, i + 1, set, 1) == 0);
        assert(graph_connect_exec(a->graph, previous, 0, set, 0, 0) == 0);
        previous = set;
    }
    return a;
}
/* A pure body called through the general compiled evaluator. */
static behavior_asset *whole_integer_operation(const node_desc *desc, pin_type from, pin_type to)
{
    behavior_asset *a = behavior_asset_new(desc->name);
    graph *f = behavior_asset_add_function(a, "Operation", 1);
    node *entry = graph_node_by_id(f, f->entry_id),
         *ret = graph_node_by_id(f, f->return_id), *op = graph_add_node(f, desc);
    for (int i = 0; i < op->input_count; i++) {
        pin_spec p = { .name = (const char *[]){ "A", "B", "C" }[i],
                       .kind = PIN_DATA,
                       .type = from,
                       .type_name = pin_type_name(from) };
        assert(graph_add_pin(f, entry, 1, &p) >= 0);
        assert(graph_set_pin_type(f, op, 0, i, from, p.type_name) == 0);
        assert(graph_connect_data(f, entry, i, op, i) == 0);
        assert(
            behavior_asset_add_variable(a, p.name, from, p.type_name, PIN_CONTAINER_SINGLE, NULL, "")
        );
    }
    assert(graph_set_pin_type(f, op, 1, 0, to, pin_type_name(to)) == 0);
    pin_spec output = { .name = "Value",
                        .kind = PIN_DATA,
                        .type = to,
                        .type_name = pin_type_name(to) };
    assert(graph_add_pin(f, ret, 0, &output) >= 0);
    assert(graph_connect_data(f, op, 0, ret, 0) == 0);
    node *call = behavior_asset_add_function_call_node(a, f);
    for (int i = 0; i < op->input_count; i++) {
        node *get = behavior_asset_add_get_node(a, a->variables[i].id);
        assert(graph_connect_data(a->graph, get, 0, call, i) == 0);
    }
    return a;
}

static behavior_asset *whole_integer_loop()
{
    behavior_asset *a = behavior_asset_new("Integer loop");
    uint64_t x = behavior_asset_add_variable(a, "Count", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                     ->id;
    graph *g = a->graph;
    node *event = graph_add_node(g, &NODE_Event),
         *loop = graph_add_node(g, registry_node_by_name("WhileLoop")),
         *less = graph_add_node(g, registry_node_by_name("Integer Less")),
         *get = behavior_asset_add_get_node(a, x),
         *add = graph_add_node(g, &NODE_Add),
         *set = behavior_asset_add_set_node(a, x);
    assert(graph_set_event_name(g, event, "Run") == 0);
    for (int i = 0; i < 2; i++) {
        assert(graph_set_pin_type(g, add, 0, i, PIN_I32, "int") == 0);
    }
    assert(graph_set_pin_type(g, add, 1, 0, PIN_I32, "int") == 0);
    assert(graph_set_pin_default(g, less, 0, 1, &(pin_value){ .type = PIN_I32, .i = 4 }) == 0);
    assert(graph_set_pin_default(g, add, 0, 1, &(pin_value){ .type = PIN_I32, .i = 1 }) == 0);
    assert(graph_connect_data(g, get, 0, less, 0) == 0);
    assert(graph_connect_data(g, less, 0, loop, 1) == 0);
    assert(graph_connect_data(g, get, 0, add, 0) == 0);
    assert(graph_connect_data(g, add, 0, set, 1) == 0);
    assert(graph_connect_exec(g, event, 0, loop, 0, 0) == 0);
    assert(graph_connect_exec(g, loop, 0, set, 0, 0) == 0);
    return a;
}
#endif
