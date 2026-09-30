#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/support/report.h"
#include "test/whole_switch_fixture.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

static node *variable(behavior_asset *asset, graph *g, int index, bool set)
{
    const behavior_variable *v = &asset->variables[index];
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, v->id, v->name));
    assert(!graph_set_pin_type(g, n, true, set ? 1 : 0, PIN_I32, "int"));
    if (set) {
        assert(!graph_set_pin_type(g, n, false, 1, PIN_I32, "int"));
    }
    return n;
}

behavior_asset *whole_switch_fixture(int kind)
{
    const node_desc *descs[] = { &NODE_SwitchInt,
                                 &NODE_SwitchString,
                                 &NODE_SwitchName,
                                 &NODE_SwitchEnum };
    const node_desc *desc = descs[kind & 3];
    behavior_asset *asset = behavior_asset_new("Compiled switch");
    behavior_asset_add_variable(asset, "Choice", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
    behavior_asset_add_variable(asset, "Count", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
    graph *g = behavior_asset_add_function(asset, "Choose", false);
    pin_spec selection = desc->inputs[1];
    assert(graph_add_pin(g, g->nodes[0], true, &selection) == 1);
    pin_spec result = { .name = "Choice", .type = PIN_I32, .type_name = "int" };
    assert(graph_add_pin(g, g->nodes[1], false, &result) == 1);
    node *choose = graph_add_node(g, desc);
    assert(!graph_set_pin_name(g, choose, true, 0, "Case:"));
    assert(!graph_set_pin_name(g, choose, true, 1, "Case:alpha"));
    assert(!graph_set_pin_name(g, choose, true, 2, "Case:alpha"));
    pin_spec extra = { .name = "ignored", .type = PIN_FLOW, .kind = PIN_EXEC };
    assert(graph_add_pin(g, choose, true, &extra) == 3);
    extra.name = "Default";
    assert(graph_add_pin(g, choose, true, &extra) == 4);
    assert(!graph_connect_data(g, g->nodes[0], 1, choose, 1));
    node *count = variable(asset, g, 1, false),
         *increment = variable(asset, g, 1, true);
    node *add = graph_add_node(g, &NODE_Add);
    assert(!graph_set_pin_type(g, add, false, 0, PIN_I32, "int"));
    assert(!graph_set_pin_type(g, add, false, 1, PIN_I32, "int"));
    assert(!graph_set_pin_type(g, add, true, 0, PIN_I32, "int"));
    assert(!graph_set_pin_default(g, add, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 }));
    assert(!graph_connect_data(g, count, 0, add, 0));
    assert(!graph_connect_data(g, add, 0, increment, 1));
    for (int i = 0; i < choose->output_count; i++) {
        node *store = variable(asset, g, 0, true);
        assert(
            !graph_set_pin_default(g, store, false, 1, &(pin_value){ .type = PIN_I32, .i = i })
        );
        assert(!graph_connect_exec(g, choose, i, store, 0, 0));
        assert(!graph_connect_exec(g, store, 0, increment, 0, 0));
    }
    node *tail = increment;
    if (kind & WHOLE_SWITCH_WAIT) {
        tail = graph_add_node(g, &NODE_Delay);
        assert(
            !graph_set_pin_default(g, tail, false, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(g, increment, 0, tail, 0, 0));
    }
    node *first = choose;
    if (kind & WHOLE_SWITCH_LOOP) {
        first = graph_add_node(g, &NODE_ForLoop);
        assert(
            !graph_set_pin_default(g, first, false, 2, &(pin_value){ .type = PIN_I32, .i = 2 })
        );
        assert(!graph_connect_exec(g, first, 0, choose, 0, 0));
        assert(!graph_connect_exec(g, first, 2, g->nodes[1], 0, 0));
    } else {
        assert(!graph_connect_exec(g, tail, 0, g->nodes[1], 0, 0));
    }
    assert(!graph_connect_exec(g, g->nodes[0], 0, first, 0, 0));
    node *value = variable(asset, g, 0, false);
    assert(!graph_connect_data(g, value, 0, g->nodes[1], 1));
    if (kind & WHOLE_SWITCH_CALL) {
        graph *outer = behavior_asset_add_function(asset, "Outer", false);
        assert(graph_add_pin(outer, outer->nodes[0], true, &selection) == 1);
        assert(graph_add_pin(outer, outer->nodes[1], false, &result) == 1);
        node *call = behavior_asset_add_function_call_node_to_graph(asset, outer, g);
        assert(!graph_connect_data(outer, outer->nodes[0], 1, call, 1));
        assert(!graph_connect_data(outer, call, 1, outer->nodes[1], 1));
        assert(!graph_connect_exec(outer, outer->nodes[0], 0, call, 0, 0));
        assert(!graph_connect_exec(outer, call, 0, outer->nodes[1], 0, 0));
    }
    return asset;
}
graph *whole_switch_root(behavior_asset *asset)
{
    return asset->graphs[acount(asset->graphs) - 1];
}
pin_value whole_switch_argument(int kind, int sample)
{
    const char *texts[] = { NULL,         "",        "alpha", "ALPHA",
                            "Case:alpha", "ignored", "雪",    "alpha" };
    const int numbers[] = { -1, 0, 1, 2, 3, 4, INT_MIN, INT_MAX };
    if ((kind & 3) == 0 || (kind & 3) == 3) {
        return (pin_value){ .type = PIN_I32, .type_name = "int", .i = numbers[sample] };
    }
    return (pin_value){ .type = (kind & 3) == 1 ? PIN_STR : PIN_NAME,
                        .type_name = (kind & 3) == 1 ? "const char *" : "name",
                        .str = texts[sample] };
}
int whole_switch_expected(int kind, int sample)
{
    if ((kind & 3) == 0 || (kind & 3) == 3) {
        int number = whole_switch_argument(kind, sample).i;
        return number >= 0 && number < 4 ? number : 4;
    }
    return sample < 2 ? 0 : sample == 2 || sample == 7 ? 1 : 4;
}
void whole_switch_smoke()
{
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int kind = 0; kind < WHOLE_SWITCH_COUNT; kind++) {
        behavior_asset *asset = whole_switch_fixture(kind);
        graph *g = whole_switch_root(asset);
        for (int sample = 0; sample < 8; sample++) {
            behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
            behavior_instance_set_trace_enabled(bi, false);
            pin_value argument = whole_switch_argument(kind, sample);
            flow_options options = { .instance = bi };
            flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &argument, 1 }, &options, NULL);
            int result = flow_run_resume(run);
            for (int waits = 0; result == FLOW_SUSPENDED_RESULT; waits++) {
                assert(waits < 3);
                result = flow_run_tick_at(run, flow_run_wake_time(run));
            }
            assert(!result && run->whole_function_executed && !acount(run->children));
            assert(bi->variables[0].i == whole_switch_expected(kind, sample));
            assert(bi->variables[1].i == (kind & WHOLE_SWITCH_LOOP ? 3 : 1));
            flow_run_free(run);
            behavior_instance_destroy(bi);
        }
        behavior_asset_free(asset);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    test_note(
        "PASS: %s whole Int/Enum/String/Name switches, dynamic cases, loops "
        "and nested waits; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
