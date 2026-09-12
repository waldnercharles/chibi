#ifndef TEST_WHOLE_MATH_FIXTURE_H
#define TEST_WHOLE_MATH_FIXTURE_H
#include "graph/model/registry.h"

#include <assert.h>

static node *math_binary(graph *g, const node_desc *desc, node *left, int lp, node *right, int rp)
{
    node *n = graph_add_node(g, desc);
    for (int i = 0; i < 2; i++) {
        assert(graph_set_pin_type(g, n, 0, i, PIN_F64, "double") == 0);
    }
    assert(graph_set_pin_type(g, n, 1, 0, PIN_F64, "double") == 0);
    assert(graph_connect_data(g, left, lp, n, 0) == 0);
    assert(graph_connect_data(g, right, rp, n, 1) == 0);
    return n;
}

/* Six sine results remain live across subsequent C calls. This exceeds the
 * saved FP register pool and checks both register and stack preservation. */
static behavior_asset *whole_math_fixture()
{
    behavior_asset *a = behavior_asset_new("Native math calls");
    pin_spec args[8];
    for (int i = 0; i < 8; i++) {
        args[i] = (pin_spec){
            .name = (const char *[]){ "A", "B", "C", "D", "E", "F", "Narrow", "Flag" }[i],
            .kind = PIN_DATA,
            .type = i < 6  ? PIN_F64
                  : i == 6 ? PIN_F32
                           : PIN_BOOL,
            .type_name = i < 6  ? "double"
                       : i == 6 ? "float"
                                : "bool"
        };
        args[i].default_value.type = args[i].type;
        if (i < 6) {
            args[i].default_value.d = .1 + i * .2;
        }
        if (i == 6) {
            args[i].default_value.f = -.5f;
        }
        if (i == 7) {
            args[i].default_value.b = true;
        }
    }
    for (int i = 0; i < 3; i++) {
        const pin_spec *p = &args[i ? i + 5 : 0];
        assert(
            behavior_asset_add_variable(a, p->name, p->type, p->type_name, PIN_CONTAINER_SINGLE, NULL, "")
        );
    }
    graph *f = behavior_asset_add_function(a, "Trigonometric sum", 1);
    node *entry = graph_node_by_id(f, f->entry_id),
         *ret = graph_node_by_id(f, f->return_id);
    for (int i = 0; i < 8; i++) {
        assert(graph_add_pin(f, entry, 1, &args[i]) >= 0);
    }
    node *sines[6], *cosines[6];
    for (int i = 0; i < 6; i++) {
        sines[i] = graph_add_node(f, registry_node_by_name("Sine"));
        cosines[i] = graph_add_node(f, registry_node_by_name("Cosine"));
        assert(graph_connect_data(f, entry, i, sines[i], 0) == 0);
        assert(graph_connect_data(f, sines[i], 0, cosines[i], 0) == 0);
    }
    node *sine_sum = sines[0], *cosine_sum = cosines[0];
    for (int i = 1; i < 6; i++) {
        sine_sum = math_binary(f, &NODE_Add, sine_sum, 0, sines[i], 0);
        cosine_sum = math_binary(f, &NODE_Add, cosine_sum, 0, cosines[i], 0);
    }
    node *total = math_binary(f, &NODE_Add, sine_sum, 0, cosine_sum, 0);
    node *bias = graph_add_node(f, &NODE_GetVariable);
    assert(graph_set_node_reference(f, bias, a->variables[0].id, a->variables[0].name) == 0);
    assert(graph_set_pin_type(f, bias, 1, 0, PIN_F64, "double") == 0);
    total = math_binary(f, &NODE_Add, total, 0, bias, 0);
    for (int i = 0; i < 3; i++) {
        assert(graph_add_pin(f, ret, 0, &args[i ? i + 5 : 0]) >= 0);
        assert(graph_connect_data(f, i ? entry : total, i ? i + 5 : 0, ret, i) == 0);
    }
    graph *wrapper = behavior_asset_add_function(a, "Return math", 0);
    node *we = graph_node_by_id(wrapper, wrapper->entry_id),
         *wr = graph_node_by_id(wrapper, wrapper->return_id);
    for (int i = 0; i < 8; i++) {
        assert(graph_add_pin(wrapper, we, 1, &args[i]) >= 0);
    }
    for (int i = 0; i < 3; i++) {
        assert(graph_add_pin(wrapper, wr, 0, &args[i ? i + 5 : 0]) >= 0);
    }
    node *inner = behavior_asset_add_function_call_node_to_graph(a, wrapper, f);
    for (int i = 0; i < 8; i++) {
        assert(graph_connect_data(wrapper, we, i + 1, inner, i) == 0);
    }
    for (int i = 0; i < 3; i++) {
        assert(graph_connect_data(wrapper, inner, i, wr, i + 1) == 0);
    }
    assert(graph_connect_exec(wrapper, we, 0, wr, 0, 0) == 0);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, event, "Run") == 0);
    for (int i = 0; i < 8; i++) {
        assert(graph_add_pin(a->graph, event, 1, &args[i]) >= 0);
    }
    node *call = behavior_asset_add_function_call_node(a, wrapper);
    assert(graph_connect_exec(a->graph, event, 0, call, 0, 0) == 0);
    for (int i = 0; i < 8; i++) {
        assert(graph_connect_data(a->graph, event, i + 1, call, i + 1) == 0);
    }
    node *previous = call;
    for (int i = 0; i < 3; i++) {
        node *set = behavior_asset_add_set_node(a, a->variables[i].id);
        assert(graph_connect_data(a->graph, call, i + 1, set, 1) == 0);
        assert(graph_connect_exec(a->graph, previous, 0, set, 0, 0) == 0);
        previous = set;
    }
    return a;
}
#endif
