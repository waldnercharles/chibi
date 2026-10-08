#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/whole_name_fixture.h"

#include <assert.h>
#include <string.h>

behavior_asset *whole_name_fixture(bool edge, bool pure, bool waits)
{
    behavior_asset *a = behavior_asset_new("Name conversion");
    assert(behavior_asset_add_variable(a, "Source", PIN_STR, NULL, PIN_CONTAINER_SINGLE, NULL, ""));
    assert(
        behavior_asset_add_variable(a, "Result", PIN_NAME, NULL, PIN_CONTAINER_SINGLE, NULL, "")
    );
    pin_spec input = { .name = "Text", .type = PIN_STR },
             output = { .name = "Name", .type = PIN_NAME };
    graph *convert = behavior_asset_add_function(a, "Convert", pure);
    int pin = pure ? 0 : 1;
    assert(graph_add_pin(convert, convert->nodes[0], 1, &input) == pin);
    assert(graph_add_pin(convert, convert->nodes[1], 0, &output) == pin);
    if (edge) {
        assert(!graph_connect_data_conversion(
            convert,
            convert->nodes[0],
            pin,
            convert->nodes[1],
            pin,
            &NODE_To_Name
        ));
    } else {
        node *n = graph_add_node(convert, &NODE_To_Name);
        assert(!graph_connect_data(convert, convert->nodes[0], pin, n, 0));
        assert(!graph_connect_data(convert, n, 0, convert->nodes[1], pin));
    }
    if (!pure) {
        assert(!graph_connect_exec(convert, convert->nodes[0], 0, convert->nodes[1], 0, 0));
    }
    graph *hold = behavior_asset_add_function(a, "Hold", false);
    assert(graph_add_pin(hold, hold->nodes[0], 1, &output) == 1);
    assert(graph_add_pin(hold, hold->nodes[1], 0, &output) == 1);
    node *tail = hold->nodes[0];
    if (waits) {
        node *delay = graph_add_node(hold, &NODE_Delay);
        assert(
            !graph_set_pin_default(hold, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(hold, tail, 0, delay, 0, 0));
        tail = delay;
    }
    assert(!graph_connect_exec(hold, tail, 0, hold->nodes[1], 0, 0));
    assert(!graph_connect_data(hold, hold->nodes[0], 1, hold->nodes[1], 1));
    graph *g = a->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Run"));
    assert(graph_add_pin(g, event, 1, &input) == 1);
    node *set = behavior_asset_add_set_node(a, a->variables[0].id);
    node *get = behavior_asset_add_get_node(a, a->variables[0].id);
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));
    assert(!graph_connect_data(g, event, 1, set, 1));
    node *call = behavior_asset_add_function_call_node(a, convert);
    assert(!graph_connect_data(g, get, 0, call, pin));
    tail = set;
    if (!pure) {
        assert(!graph_connect_exec(g, tail, 0, call, 0, 0));
        tail = call;
    }
    node *retain = behavior_asset_add_function_call_node(a, hold);
    assert(!graph_connect_exec(g, tail, 0, retain, 0, 0));
    assert(!graph_connect_data(g, call, pin, retain, 1));
    node *result = behavior_asset_add_set_node(a, a->variables[1].id);
    assert(!graph_connect_exec(g, retain, 0, result, 0, 0));
    assert(!graph_connect_data(g, retain, 1, result, 1));
    return a;
}
void whole_name_smoke()
{
    for (int edge = 0; edge < 2; edge++) {
        for (int pure = 0; pure < 2; pure++) {
            behavior_asset *a = whole_name_fixture(edge, pure, true);
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            behavior_instance_set_trace_enabled(bi, false);
            pin_value input = { .type = PIN_STR, .str = edge ? NULL : "retained" };
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
            flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ &input, 1 }, NULL);
            assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_SUSPENDED);
            input.str = "replaced";
            assert(!behavior_instance_set_variable(bi, a->variables[0].id, &input));
            assert(!behavior_instance_tick(bi, .125));
            assert(flow_run_status(run) == FLOW_COMPLETED);
            assert(bi->execution.variables[1].type == PIN_NAME);
            assert(!strcmp(bi->execution.variables[1].str, edge ? "" : "retained"));
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            behavior_instance_destroy(bi);
            behavior_asset_free(a);
        }
    }
    puts("PASS: whole explicit/edge To Name, pure/effectful calls, null normalization and wait retention; zero oracle calls");
}
