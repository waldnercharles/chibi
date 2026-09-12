/* Release comparison of the same scalar DAG and behavior dispatch. */
#include "graph/compiler/behavior_program.h"
#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/runtime.h"
#include "support/arena.h"
#include "support/array.h"
#include "whole_function_fixture.h"

#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void measure_function(const char *label, int (*entry)(behavior_function_context *), int count)
{
    pin_value variable = { .type = PIN_F64 };
    behavior_function_context ctx = { .variables = &variable, .budget = 4096 };
    double start = 0, checksum = 0;
    for (int i = -10000; i < count; i++) {
        if (i == 0) {
            start = seconds();
        }
        variable.d = (i & 1023) * .001;
        ctx.steps = 0;
        assert(entry(&ctx) == 0 && ctx.steps == 2);
        if (i >= 0) {
            checksum += variable.d;
        }
    }
    printf("%s whole_function ns=%.3f checksum=%.12f\n", label, (seconds() - start) * 1e9 / count, checksum);
    double expected = 0;
    for (int i = 0; i < count; i++) {
        expected += native((i & 1023) * .001);
    }
    assert(checksum == expected);
}

static const behavior_function *whole_entry(behavior_asset *a)
{
    behavior_program_publish(a);
    for (int i = 0; i < acount(a->graph->nodes); i++) {
        node *n = a->graph->nodes[i];
        if (n->desc == &NODE_Event) {
            return behavior_program_function(behavior_program_graph(a->program, a->graph), n);
        }
    }
    return NULL;
}
static void compare_control(void *library)
{
    behavior_asset *a = whole_function_fixture();
    const behavior_function *f = whole_entry(a);
    assert(f);
    int (*generated)(
        behavior_function_context *
    ) = (int (*)(behavior_function_context *))dlsym(library, "generated_control");
    assert(generated);
    for (unsigned long budget = 0; budget < 17; budget++) {
        pin_value reference[2] = { { .type = PIN_F32 }, { .type = PIN_F32 } };
        behavior_function_context expected = { .variables = reference, .budget = budget };
        int result = behavior_function_run(f, &expected, false);
        for (int mode = 0; mode < 2; mode++) {
            pin_value variables[2] = { { .type = PIN_F32 }, { .type = PIN_F32 } };
            behavior_function_context actual = { .variables = variables,
                                                 .budget = budget };
            int got = mode ? generated(&actual)
                           : behavior_function_run(f, &actual, true);
            assert(got == result && actual.steps == expected.steps);
            assert(!result || actual.node_id == expected.node_id);
            assert(
                variables[0].f == reference[0].f &&
                variables[1].f == reference[1].f
            );
        }
    }
    puts("control_parity=SLJIT,BYTECODE,GENERATED_C budgets=0..16");
    behavior_asset_free(a);
}

int main(int argc, char **argv)
{
    bool export_c = argc > 1 && !strcmp(argv[1], "--emit-c");
    int count = !export_c && argc > 1 ? atoi(argv[1]) : 1000000;
    assert(count > 0);
    node *sink;
    behavior_asset *asset = fixture(&sink, false);
    double publication = seconds();
    behavior_program_publish(asset);
    printf("publication_ms=%.3f\n", (seconds() - publication) * 1000);
    assert(behavior_program_backend(asset->program) == BEHAVIOR_SLJIT);
    node *event = asset->graph->nodes[0];
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        if (asset->graph->nodes[i]->desc == &NODE_Event) {
            event = asset->graph->nodes[i];
        }
    }
    const behavior_function *function = behavior_program_function(
        behavior_program_graph(asset->program, asset->graph),
        event
    );
    assert(function && behavior_function_entry(function));
    if (export_c) {
        assert(argc == 3);
        FILE *out = fopen(argv[2], "w");
        assert(out);
        behavior_function_write_c(function, out, "generated_behavior");
        behavior_asset *control = whole_function_fixture();
        const behavior_function *cf = whole_entry(control);
        assert(cf);
        behavior_function_write_c(cf, out, "generated_control");
        behavior_asset_free(control);
        assert(fclose(out) == 0);
        behavior_asset_free(asset);
        return 0;
    }
    int rotation = argc > 2 ? atoi(argv[2]) : 0;
    assert(rotation >= 0 && rotation < 3);
    void *native_entry = behavior_function_entry(function);
    if (argc > 3) {
        void *library = dlopen(argv[3], RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            fprintf(stderr, "%s\n", dlerror());
            return 1;
        }
        compare_control(library);
        void *entry = dlsym(library, "generated_behavior");
        assert(entry);
        for (int i = 0; i < 2; i++) {
            bool generated = (i + rotation) % 2;
            measure_function(
                generated ? "GENERATED_C" : "SLJIT",
                (int (*)(behavior_function_context *))(generated ? entry : native_entry),
                count
            );
        }
        dlclose(library);
    } else {
        measure_function("SLJIT", (int (*)(behavior_function_context *))native_entry, count);
    }
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
