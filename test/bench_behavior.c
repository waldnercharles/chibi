/* Release comparison of the same scalar DAG and behavior dispatch. */
#include "graph/compiler/behavior_program.h"
#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/runtime.h"
#include "support/arena.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

__attribute__((noinline)) static double native(double x)
{
    x = x * 1.01 + .125;
    x = x * 1.01 + .125;
    x = x * 1.01 + .125;
    return x * 1.01 + .125;
}

static int native_flow(node *, flow_context *ctx)
{
    behavior_instance *instance = flow_context_instance(ctx);
    instance->variables[0].d = native(instance->variables[0].d);
    return flow_context_continue(ctx, 0);
}
static pin_spec exec_pin = { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC };
static node_desc native_node = { .name = "Native benchmark",
                                 .inputs = &exec_pin,
                                 .input_count = 1,
                                 .outputs = &exec_pin,
                                 .output_count = 1,
                                 .flow_kind = NODE_FLOW_CUSTOM,
                                 .flow = native_flow };

static behavior_asset *fixture(node **sink, bool reference)
{
    behavior_asset *a = behavior_asset_new("ScalarBenchmark");
    behavior_variable *v = behavior_asset_add_variable(a, "Value", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "");
    uint64_t id = v->id;
    if (reference) {
        *sink = graph_add_node(a->graph, &native_node);
        node *event = graph_add_node(a->graph, &NODE_Event);
        assert(graph_set_event_name(a->graph, event, "Tick") == 0);
        assert(graph_connect_exec(a->graph, event, 0, *sink, 0, 0) == 0);
        return a;
    }
    node *previous = behavior_asset_add_get_node(a, id);
    for (int i = 0; i < 8; i++) {
        node *n = graph_add_node(a->graph, i % 2 ? &NODE_Add : &NODE_Multiply);
        assert(graph_set_pin_type(a->graph, n, 0, 0, PIN_F64, "double") == 0);
        assert(graph_set_pin_type(a->graph, n, 0, 1, PIN_F64, "double") == 0);
        assert(graph_set_pin_type(a->graph, n, 1, 0, PIN_F64, "double") == 0);
        assert(
            graph_set_pin_default(a->graph, n, 0, 1, &(pin_value){ .type = PIN_F64, .d = i % 2 ? .125 : 1.01 }) ==
            0
        );
        assert(graph_connect_data(a->graph, previous, 0, n, 0) == 0);
        previous = n;
    }
    *sink = behavior_asset_add_set_node(a, id);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, event, "Tick") == 0);
    assert(graph_connect_exec(a->graph, event, 0, *sink, 0, 0) == 0);
    assert(graph_connect_data(a->graph, previous, 0, *sink, 1) == 0);
    return a;
}

static void measure(behavior_asset *a, node *sink, behavior_backend backend, int count, bool dispatch)
{
    behavior_instance *instance = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(instance, backend) == 0);
    behavior_instance_set_trace_enabled(instance, 0);
    behavior_instance_set_breakpoints_enabled(instance, 0);
    assert(behavior_instance_set_activation_pool(instance, 1) == 0);
    eval_context ctx = { .instance = instance,
                         .runtime = runtime_store_create(a->graph) };
    const behavior_graph_program *program = behavior_program_graph(a->program, a->graph);
    arena storage = { 0 };
    node *source = NULL;
    int source_pin = -1;
    if (!dispatch) {
        assert(graph_input_source(a->graph, sink, 1, &source, &source_pin));
    }
    double checksum = 0, start = 0;
    for (int i = -1000; i < count; i++) {
        if (i == 0) {
            start = seconds();
        }
        double input = (i & 1023) * .001;
        instance->variables[0].d = input;
        pin_value value;
        if (dispatch) {
            flow_error error = { 0 };
            flow_run *run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            assert(run && flow_run_status(run) == FLOW_COMPLETED);
            value = instance->variables[0];
            assert(behavior_instance_release_activation(instance, run) == 0);
        } else {
            runtime_store_reset_memo(ctx.runtime);
            eval_context_reset(&ctx);
            int result = backend == BEHAVIOR_ORACLE
                           ? graph_eval_output(a->graph, source, source_pin, &storage, &value, &ctx)
                           : behavior_program_input(program, sink, 1, &storage, &value, &ctx, backend);
            assert(result == 0);
        }
        if (i < 0) {
            assert(fabs(value.d - native(input)) < 1e-12);
        }
        if (i >= 0) {
            checksum += value.d;
        }
    }
    printf(
        "%s %s ns=%.3f checksum=%.12f\n",
        sink->desc == &native_node ? "C" : behavior_backend_name(backend),
        dispatch ? "dispatch" : "kernel",
        (seconds() - start) * 1e9 / count,
        checksum
    );
    double expected = 0;
    for (int i = 0; i < count; i++) {
        expected += native((i & 1023) * .001);
    }
    assert(checksum == expected);
    arena_free(&storage);
    runtime_store_free(ctx.runtime);
    behavior_instance_destroy(instance);
}

int main(int argc, char **argv)
{
    int count = argc > 1 ? atoi(argv[1]) : 1000000;
    assert(count > 0);
    node *sink;
    behavior_asset *asset = fixture(&sink, false);
    double publication = seconds();
    behavior_program_publish(asset);
    printf("publication_ms=%.3f\n", (seconds() - publication) * 1000);
    assert(behavior_program_backend(asset->program) == BEHAVIOR_SLJIT);
    double warmup = 0;
    for (int i = 0; i < 10000; i++) {
        warmup += native((i & 1023) * .001);
    }
    assert(warmup > 0);
    double checksum = 0, start = seconds();
    for (int i = 0; i < count; i++) {
        checksum += native((i & 1023) * .001);
    }
    printf("C kernel ns=%.3f checksum=%.12f\n", (seconds() - start) * 1e9 / count, checksum);
    for (int i = 0; i < 3; i++) {
        behavior_backend backend = (behavior_backend[]){
            BEHAVIOR_SLJIT,
            BEHAVIOR_BYTECODE,
            BEHAVIOR_ORACLE
        }[(i + (argc > 2 ? atoi(argv[2]) : 0)) % 3];
        measure(asset, sink, backend, count, false);
        measure(asset, sink, backend, count, true);
    }
    behavior_asset_free(asset);
    asset = fixture(&sink, true);
    measure(asset, sink, BEHAVIOR_SLJIT, count, true);
    behavior_asset_free(asset);
}
