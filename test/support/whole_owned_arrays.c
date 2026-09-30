#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/support/report.h"
#include "test/whole_managed_fixture.h"
#include "test/whole_owned_array_fixture.h"

#include <assert.h>
#include <stdio.h>

static node *variable(behavior_asset *a, graph *g, int index, bool set)
{
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    const behavior_variable *v = &a->variables[index];
    assert(!graph_set_node_reference(g, n, v->id, v->name));
    assert(!graph_set_pin_type(g, n, true, set ? 1 : 0, v->type, v->type_name));
    assert(!graph_set_pin_container(g, n, true, set ? 1 : 0, v->container));
    if (set) {
        assert(!graph_set_pin_type(g, n, false, 1, v->type, v->type_name));
        assert(!graph_set_pin_container(g, n, false, 1, v->container));
    }
    return n;
}
behavior_asset *whole_owned_array_fixture(int kind, bool waits)
{
    pin_type type = (pin_type[]){ PIN_I32, PIN_STR, PIN_STRUCT }[kind];
    const char *name = (const char *[]){ "int", "const char *", "ManagedRecord" }[kind];
    behavior_asset *a = behavior_asset_new("Owned array snapshots");
    const char *variables[] = { "Source", "Returned", "Snapshot", "Last", "Count" };
    for (int i = 0; i < 5; i++) {
        assert(behavior_asset_add_variable(
            a,
            variables[i],
            i == 4 ? PIN_I32 : type,
            i == 4 ? "int" : name,
            i < 3 ? PIN_CONTAINER_ARRAY : PIN_CONTAINER_SINGLE,
            NULL,
            ""
        ));
    }
    whole_managed records[] = { { "one", { "first", 1.25f }, 1 },
                                { "two", { "second", 2.5f }, 2 },
                                { "three", { "third", 3.75f }, 3 } };
    pin_value items[3];
    for (int i = 0; i < 3; i++) {
        items[i] = (pin_value){ .type = type, .type_name = name };
        if (kind == OWNED_INTS) {
            items[i].i = i + 1;
        } else if (kind == OWNED_STRINGS) {
            items[i].str = records[i].label;
        } else {
            items[i].ptr = &records[i];
        }
    }
    pin_array defaults = { .element_type = type,
                           .type_name = name,
                           .count = 3,
                           .capacity = 3,
                           .items = items };
    pin_spec array = { .name = "Values",
                       .type = type,
                       .type_name = name,
                       .container = PIN_CONTAINER_ARRAY,
                       .default_value = { .type = type,
                                          .type_name = name,
                                          .container = PIN_CONTAINER_ARRAY } };
    pin_value value = array.default_value;
    value.ptr = &defaults;
    graph *hold = behavior_asset_add_function(a, "Hold array", false);
    node *input = hold->nodes[0], *ret = hold->nodes[1];
    assert(graph_add_pin(hold, input, true, &array) == 1);
    assert(graph_add_pin(hold, ret, false, &array) == 1);
    assert(!graph_set_pin_default(hold, input, true, 1, &value));
    assert(!graph_set_pin_default(hold, ret, false, 1, &value));
    assert(!graph_connect_data(hold, input, 1, ret, 1));
    if (waits) {
        node *delay = graph_add_node(hold, &NODE_Delay);
        assert(
            !graph_set_pin_default(hold, delay, false, 1, &(pin_value){ .type = PIN_F64, .d = .25 })
        );
        assert(!graph_connect_exec(hold, input, 0, delay, 0, 0));
        assert(!graph_connect_exec(hold, delay, 0, ret, 0, 0));
    } else {
        assert(!graph_connect_exec(hold, input, 0, ret, 0, 0));
    }
    graph *g = a->graph;
    node *entry = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, entry, "Run"));
    assert(graph_add_pin(g, entry, true, &array) == 1);
    assert(!graph_set_pin_default(g, entry, true, 1, &value));
    pin_spec call_enabled = { .name = "Call",
                              .type = PIN_BOOL,
                              .type_name = "bool",
                              .default_value = { .type = PIN_BOOL, .b = true } };
    assert(graph_add_pin(g, entry, true, &call_enabled) == 2);
    node *set = variable(a, g, 0, true), *get = variable(a, g, 0, false);
    node *branch = graph_add_node(g, &NODE_Branch);
    node *call = behavior_asset_add_function_call_node(a, hold);
    node *returned = variable(a, g, 1, true), *snapshot = variable(a, g, 2, true);
    assert(!graph_set_pin_default(g, call, true, 1, &value));
    assert(!graph_connect_data(g, entry, 1, set, 1));
    assert(!graph_connect_data(g, entry, 2, branch, 1));
    assert(!graph_connect_data(g, get, 0, call, 1));
    assert(!graph_connect_data(g, call, 1, returned, 1));
    assert(!graph_connect_data(g, returned, 1, snapshot, 1));
    assert(!graph_connect_exec(g, entry, 0, set, 0, 0));
    assert(!graph_connect_exec(g, set, 0, branch, 0, 0));
    assert(!graph_connect_exec(g, branch, 0, call, 0, 0));
    assert(!graph_connect_exec(g, branch, 1, returned, 0, 0));
    assert(!graph_connect_exec(g, call, 0, returned, 0, 0));
    assert(!graph_connect_exec(g, returned, 0, snapshot, 0, 0));
    node *loop = graph_add_node(g, &NODE_ForEach), *read = variable(a, g, 1, false);
    node *clear = variable(a, g, 1, true), *last = variable(a, g, 3, true);
    node *count = variable(a, g, 4, false), *increment = graph_add_node(g, &NODE_Add);
    node *store_count = variable(a, g, 4, true);
    for (int p = 0; p < 2; p++) {
        assert(!graph_set_pin_type(g, increment, false, p, PIN_I32, "int"));
    }
    assert(!graph_set_pin_type(g, increment, true, 0, PIN_I32, "int"));
    assert(
        !graph_set_pin_default(g, increment, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 })
    );
    assert(!graph_connect_data(g, read, 0, loop, 1));
    assert(!graph_connect_data(g, loop, 1, last, 1));
    assert(!graph_connect_data(g, count, 0, increment, 0));
    assert(!graph_connect_data(g, increment, 0, store_count, 1));
    assert(!graph_connect_exec(g, snapshot, 0, loop, 0, 0));
    assert(!graph_connect_exec(g, loop, 0, clear, 0, 0));
    assert(!graph_connect_exec(g, clear, 0, last, 0, 0));
    assert(!graph_connect_exec(g, last, 0, store_count, 0, 0));
    return a;
}
void whole_owned_array_smoke()
{
    behavior_asset *a = whole_owned_array_fixture(OWNED_STRINGS, true);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, NULL);
    assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_SUSPENDED);
    pin_value empty = { .type = PIN_STR,
                        .type_name = "const char *",
                        .container = PIN_CONTAINER_ARRAY };
    assert(!behavior_instance_set_variable(bi, a->variables[0].id, &empty));
    assert(!flow_run_tick_at(run, .25));
    assert(flow_run_status(run) == FLOW_COMPLETED && bi->variables[4].i == 3);
    assert(((pin_array *)bi->variables[2].ptr)->count == 3);
    assert(!bi->variables[1].ptr);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    test_note("PASS: whole owned arrays, defaults, retained calls, writes and iteration after replacement; zero oracle calls\n");
}
