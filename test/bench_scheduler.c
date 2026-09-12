#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>
static double seconds()
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void benchmark(int count)
{
    behavior_asset *a = behavior_asset_new("Scheduler");
    node *event = graph_add_node(a->graph, &NODE_Event),
         *delay = graph_add_node(a->graph, &NODE_Delay);
    assert(graph_set_event_name(a->graph, event, "Wait") == 0);
    assert(
        graph_set_pin_default(a->graph, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1 }) == 0
    );
    assert(graph_connect_exec(a->graph, event, 0, delay, 0, 0) == 0);
    behavior_instance *instance = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    for (int i = 0; i < count; i++) {
        flow_error error = { 0 };
        assert(behavior_instance_enqueue(instance, event, (flow_inputs){ 0 }, &error));
    }
    assert(behavior_instance_tick(instance, 0) >= 0);
    double start = seconds();
    for (int i = 0; i < 1000; i++) {
        assert(behavior_instance_tick(instance, .5) >= 0);
    }
    double idle = (seconds() - start) * 1e9 / 1000;
    start = seconds();
    assert(behavior_instance_tick(instance, 2) >= 0);
    double wake = (seconds() - start) * 1e9;
    int completed = 0;
    for (int i = 0; i < acount(instance->activations); i++) {
        completed += flow_run_status(instance->activations[i]) == FLOW_COMPLETED;
    }
    assert(completed == count);
    printf("queue=%d idle_tick_ns=%.3f wake_all_ns=%.3f completed=%d\n", count, idle, wake, completed);
    behavior_instance_destroy(instance);
    behavior_asset_free(a);
}
/* A second authored workload: independent per-entity counters updated through
 * an effectful function. No Arena content, rendering, physics or host services. */
static void tick_calls(int count, behavior_backend backend)
{
    behavior_asset *a = behavior_asset_new("Counter Tick");
    uint64_t id = behavior_asset_add_variable(a, "Value", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "")
                      ->id;
    graph *fn = behavior_asset_add_function(a, "Advance", 0);
    node *get = graph_add_node(fn, &NODE_GetVariable);
    node *set = graph_add_node(fn, &NODE_SetVariable);
    assert(graph_set_node_reference(fn, get, id, "Value") == 0);
    assert(graph_set_node_reference(fn, set, id, "Value") == 0);
    assert(graph_set_pin_type(fn, get, 1, 0, PIN_F64, "double") == 0);
    assert(graph_set_pin_type(fn, set, 0, 1, PIN_F64, "double") == 0);
    assert(graph_set_pin_type(fn, set, 1, 1, PIN_F64, "double") == 0);
    node *add = graph_add_node(fn, &NODE_Add);
    assert(graph_set_pin_type(fn, add, 0, 0, PIN_F64, "double") == 0);
    assert(graph_set_pin_type(fn, add, 0, 1, PIN_F64, "double") == 0);
    assert(graph_set_pin_type(fn, add, 1, 0, PIN_F64, "double") == 0);
    assert(graph_set_pin_default(fn, add, 0, 1, &(pin_value){ .type = PIN_F64, .d = .125 }) == 0);
    assert(graph_connect_data(fn, get, 0, add, 0) == 0);
    assert(graph_connect_data(fn, add, 0, set, 1) == 0);
    assert(graph_connect_exec(fn, graph_node_by_id(fn, fn->entry_id), 0, set, 0, 0) == 0);
    assert(graph_connect_exec(fn, set, 0, graph_node_by_id(fn, fn->return_id), 0, 0) == 0);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, event, "Tick") == 0);
    node *call = behavior_asset_add_function_call_node(a, fn);
    assert(graph_connect_exec(a->graph, event, 0, call, 0, 0) == 0);
    behavior_instance **instances = NULL;
    for (int i = 0; i < count; i++) {
        behavior_instance *instance = behavior_instance_create(a, NULL, NULL, NULL);
        assert(behavior_instance_set_backend(instance, backend) == 0);
        behavior_instance_set_trace_enabled(instance, 0);
        assert(behavior_instance_set_activation_pool(instance, 1) == 0);
        instance->variables[0].d = i;
        apush(instances, instance);
    }
    double start = 0;
    for (int frame = -8; frame < 128; frame++) {
        if (!frame) {
            start = seconds();
        }
        for (int i = 0; i < count; i++) {
            flow_error error = { 0 };
            flow_run *run = behavior_instance_dispatch(instances[i], "Tick", (flow_inputs){ 0 }, &error);
            assert(run && flow_run_status(run) == FLOW_COMPLETED);
            assert(behavior_instance_release_activation(instances[i], run) == 0);
        }
    }
    printf(
        "tick_entities=%d backend=%s frame_ms=%.6f ns_per_entity=%.3f\n",
        count,
        behavior_backend_name(backend),
        (seconds() - start) * 1000 / 128,
        (seconds() - start) * 1e9 / (128 * count)
    );
    for (int i = 0; i < count; i++) {
        assert(instances[i]->variables[0].d == i + 136 * .125);
        behavior_instance_destroy(instances[i]);
    }
    afree(instances);
    behavior_asset_free(a);
}

int main()
{
    benchmark(64);
    benchmark(512);
    benchmark(4096);
    for (int mode = 0; mode < 2; mode++) {
        behavior_backend backend = mode ? BEHAVIOR_BYTECODE : BEHAVIOR_SLJIT;
        tick_calls(64, backend);
        tick_calls(512, backend);
        tick_calls(4096, backend);
    }
}
