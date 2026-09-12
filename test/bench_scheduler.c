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
int main()
{
    benchmark(64);
    benchmark(512);
    benchmark(4096);
}
