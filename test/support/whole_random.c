#include "graph/model/registry.h"
#include "graph/nodes/math_nodes.h"
#include "graph/nodes/vector_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/random.h"
#include "test/whole_random_fixture.h"

#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

behavior_asset *whole_random_fixture(int kind)
{
    assert(kind >= 0 && kind < WHOLE_RANDOM_COUNT);
    behavior_asset *asset = behavior_asset_new("Random bodies");
    graph *g = behavior_asset_add_function(asset, "Sample", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    node_desc *desc = kind == WHOLE_RANDOM_UNIT  ? &NODE_RandomReal
                    : kind == WHOLE_RANDOM_RANGE ? &NODE_RandomRangeReal
                                                 : &NODE_Random_Range_Vector;
    node *random = graph_add_node(g, desc);
    for (int i = 0; i < desc->input_count; i++) {
        assert(graph_add_pin(g, entry, 1, &desc->inputs[i]) == i + 1);
        assert(!graph_connect_data(g, entry, i + 1, random, i));
    }
    pin_spec result = desc->outputs[0];
    int output = 0;
    if (kind == WHOLE_RANDOM_PROJECT) {
        assert(!graph_expose_output_fields(g, random, 0));
        output = 1;
        result = (pin_spec){ .name = "X", .type = PIN_F32, .type_name = "float" };
    }
    assert(graph_add_pin(g, ret, 0, &result) == 1);
    assert(!graph_connect_data(g, random, output, ret, 1));
    /* A second use of the same node shares a draw; another node consumes the
     * next draw. Projecting X must still consume the unused Y/Z draws. */
    assert(graph_add_pin(g, ret, 0, &result) == 2);
    assert(!graph_connect_data(g, random, output, ret, 2));
    node *next = graph_add_node(g, &NODE_RandomReal);
    assert(graph_add_pin(g, ret, 0, &NODE_RandomReal.outputs[0]) == 3);
    assert(!graph_connect_data(g, next, 0, ret, 3));
    assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));

    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(!graph_set_event_name(asset->graph, event, "Tick"));
    node *call = behavior_asset_add_function_call_node(asset, g);
    assert(!graph_connect_exec(asset->graph, event, 0, call, 0, 0));
    for (int i = 0; i < desc->input_count; i++) {
        assert(graph_add_pin(asset->graph, event, 1, &desc->inputs[i]) == i + 1);
        assert(!graph_connect_data(asset->graph, event, i + 1, call, i + 1));
    }
    node *tail = call;
    for (int i = 1; i <= 3; i++) {
        const pin_spec *p = &ret->input_pins[i];
        char name[24];
        snprintf(name, sizeof name, "Value%d", i);
        uint64_t id = behavior_asset_add_variable(asset, name, p->type, p->type_name, PIN_CONTAINER_SINGLE, NULL, "")
                          ->id;
        node *set = behavior_asset_add_set_node(asset, id);
        assert(!graph_connect_data(asset->graph, call, i, set, 1));
        assert(!graph_connect_exec(asset->graph, tail, 0, set, 0, 0));
        tail = set;
    }
    return asset;
}

static void smoke_case(int kind, double first, double last)
{
    behavior_asset *asset = whole_random_fixture(kind);
    node *event = asset->graph->nodes[0];
    pin_value args[2];
    float inputs[2][3];
    int count = event->output_count - 1;
    for (int i = 0; i < count; i++) {
        args[i] = event->output_pins[i + 1].default_value;
        double value = i ? last : first;
        if (args[i].type == PIN_STRUCT) {
            for (int j = 0; j < 3; j++) {
                inputs[i][j] = (float)value;
            }
            args[i].ptr = inputs[i];
        } else {
            args[i].d = value;
        }
    }
    pin_value expected[3];
    float vector[2][3];
    int next = 0;
    for (int mode = 0; mode < 2; mode++) {
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(
            !behavior_instance_set_backend(bi, mode ? behavior_default_backend() : BEHAVIOR_ORACLE)
        );
        behavior_instance_set_trace_enabled(bi, false);
        behavior_srand(937);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        flow_run *run = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ args, count }, NULL);
        assert(run && flow_run_status(run) == FLOW_COMPLETED);
        if (!mode) {
            for (int i = 0; i < 3; i++) {
                expected[i] = bi->variables[i];
                if (expected[i].type == PIN_STRUCT) {
                    memcpy(vector[i], expected[i].ptr, sizeof vector[i]);
                    expected[i].ptr = vector[i];
                }
            }
            next = behavior_rand();
        } else {
            assert(run->whole_function_executed && behavior_rand() == next);
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            for (int i = 0; i < 3; i++) {
                pin_value actual = bi->variables[i];
                assert(actual.type == expected[i].type);
                if (actual.type == PIN_STRUCT) {
                    assert(!memcmp(actual.ptr, expected[i].ptr, sizeof vector[0]));
                } else if (actual.type == PIN_F32) {
                    assert(!memcmp(&actual.f, &expected[i].f, sizeof(float)));
                } else {
                    assert(!memcmp(&actual.d, &expected[i].d, sizeof(double)));
                }
            }
        }
        behavior_instance_destroy(bi);
    }
    behavior_asset_free(asset);
}
void whole_random_smoke()
{
    for (int kind = 0; kind < WHOLE_RANDOM_COUNT; kind++) {
        smoke_case(kind, 0, 1);
        if (kind == WHOLE_RANDOM_UNIT) {
            continue;
        }
        double limit = kind == WHOLE_RANDOM_RANGE ? DBL_MAX : FLT_MAX;
        smoke_case(kind, 7.5, -13);
        smoke_case(kind, -limit, limit);
        smoke_case(kind, limit, limit);
    }
    printf(
        "PASS: %s whole random scalar/vector values, shared draws, projections "
        "and RNG sequence; zero compiled oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
