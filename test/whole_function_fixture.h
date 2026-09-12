#ifndef TEST_WHOLE_FUNCTION_FIXTURE_H
#define TEST_WHOLE_FUNCTION_FIXTURE_H
#include "graph/model/registry.h"

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
#endif
