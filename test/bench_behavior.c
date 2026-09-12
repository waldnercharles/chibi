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
static void measure_scopes(const char *label, int (*entry)(behavior_function_context *), int count)
{
    pin_value variables[2] = { { .type = PIN_F32 }, { .type = PIN_F32 } };
    behavior_function_context ctx = { .variables = variables, .budget = 4096 };
    double start = 0, checksum = 0, expected = 0;
    for (int i = -10000; i < count; i++) {
        if (!i) {
            start = seconds();
        }
        float input = (float)(i & 1023) * .001f;
        variables[0].f = input;
        ctx.steps = 0;
        assert(entry(&ctx) == 0 && ctx.steps == 4 && variables[0].f == 99);
        if (i >= 0) {
            checksum += variables[1].f;
            expected += (float)((float)(input + 1) + 100);
        }
    }
    printf("%s calls ns=%.3f checksum=%.12f\n", label, (seconds() - start) * 1e9 / count, checksum);
    assert(checksum == expected);
}
static void compare_control(void *library, bool scopes)
{
    behavior_asset *a = scopes ? whole_scopes_fixture() : whole_function_fixture();
    const behavior_function *f = whole_entry(a);
    assert(f);
    int (*generated)(behavior_function_context *) = (int (*)(
        behavior_function_context *
    ))dlsym(library, scopes ? "generated_scopes" : "generated_control");
    assert(generated);
    for (int budget_case = 0; budget_case < 18; budget_case++) {
        uintptr_t budget = budget_case < 17 ? (uintptr_t)budget_case : UINTPTR_MAX;
        for (int start = 0; start < 3; start++) {
            uintptr_t steps = (uintptr_t[]){ 0, 5, UINTPTR_MAX - 1 }[start];
            for (unsigned depth = 0; depth <= 64; depth += 32) {
                pin_value reference[2] = { { .type = PIN_F32 }, { .type = PIN_F32 } };
                behavior_function_context expected = { .variables = reference,
                                                       .budget = budget,
                                                       .steps = steps,
                                                       .call_depth = depth };
                int result = behavior_function_run(f, &expected, false);
                for (int mode = 0; mode < 2; mode++) {
                    pin_value variables[2] = { { .type = PIN_F32 },
                                               { .type = PIN_F32 } };
                    behavior_function_context actual = { .variables = variables,
                                                         .budget = budget,
                                                         .steps = steps,
                                                         .call_depth = depth };
                    int got = mode ? generated(&actual)
                                   : behavior_function_run(f, &actual, true);
                    assert(got == result && actual.steps == expected.steps);
                    assert(!result || actual.node_id == expected.node_id);
                    assert(
                        !result || (actual.error_frame == expected.error_frame &&
                                    actual.error_steps == expected.error_steps &&
                                    actual.failure == expected.failure)
                    );
                    assert(
                        variables[0].f == reference[0].f &&
                        variables[1].f == reference[1].f
                    );
                }
            }
        }
    }
    printf("%s_parity=SLJIT,BYTECODE,GENERATED_C budgets=0..16 depths=0,32,64\n", scopes ? "scopes" : "control");
    behavior_asset_free(a);
}

static const behavior_function *arguments_entry(behavior_asset *a)
{
    behavior_program_publish(a);
    graph *g = a->graphs[1];
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(a->program, g),
        graph_node_by_id(g, g->entry_id)
    );
    assert(f);
    return f;
}

static void compare_arguments(void *library, int count, int rotation)
{
    behavior_asset *a = whole_arguments_fixture();
    const behavior_function *f = arguments_entry(a);
    int (*entries[])(behavior_function_context *) = {
        (int (*)(behavior_function_context *))behavior_function_entry(f),
        (int (*)(behavior_function_context *))dlsym(library, "generated_arguments")
    };
    assert(entries[0] && entries[1]);
    double cases[] = { NAN, INFINITY, -INFINITY, -0., 0., 1.e-40, 16777217. };
    for (int x = 0; x < 7; x++) {
        for (int y = 0; y < 7; y++) {
            for (unsigned long budget = 0; budget <= 4; budget++) {
                /* Reverse the declared widths to exercise argument coercion. */
                pin_value args[] = { { .type = PIN_F32, .f = (float)cases[x] },
                                     { .type = PIN_F64, .d = cases[y] },
                                     { .type = PIN_BOOL, .b = x % 2 } };
                pin_value reference[4] = { 0 };
                behavior_function_context expected = { .arguments = args,
                                                       .argument_count = x % 4,
                                                       .results = reference,
                                                       .budget = budget };
                int result = behavior_function_run(f, &expected, false);
                for (int mode = 0; mode < 2; mode++) {
                    pin_value values[4] = { 0 };
                    behavior_function_context actual = { .arguments = args,
                                                         .argument_count = x % 4,
                                                         .results = values,
                                                         .budget = budget };
                    assert(
                        entries[mode](&actual) == result &&
                        actual.steps == expected.steps
                    );
                    assert(actual.returned_node_id == expected.returned_node_id);
                    assert(!result || actual.node_id == expected.node_id);
                    assert(
                        isnan(reference[1].d) ? isnan(values[1].d)
                                              : values[1].d == reference[1].d
                    );
                    assert(
                        isnan(reference[2].f) ? isnan(values[2].f)
                                              : values[2].f == reference[2].f
                    );
                    assert(values[3].b == reference[3].b);
                    assert(
                        reference[1].d != 0 ||
                        signbit(reference[1].d) == signbit(values[1].d)
                    );
                    assert(
                        reference[2].f != 0 ||
                        signbit(reference[2].f) == signbit(values[2].f)
                    );
                }
            }
        }
    }
    puts("arguments_parity=SLJIT,BYTECODE,GENERATED_C defaults,coercions,NaNs,zeros,returns,budgets");
    for (int pass = 0; pass < 2; pass++) {
        int mode = (pass + rotation) % 2;
        pin_value args[] = { { .type = PIN_F64 },
                             { .type = PIN_F32, .f = -.5f },
                             { .type = PIN_BOOL, .b = true } };
        pin_value results[4] = { 0 };
        behavior_function_context ctx = { .arguments = args,
                                          .argument_count = 3,
                                          .results = results,
                                          .budget = 3 };
        double start = 0, checksum = 0, expected = 0;
        for (int i = -10000; i < count; i++) {
            if (!i) {
                start = seconds();
            }
            args[0].d = (i & 1023) * .001;
            ctx.steps = 0;
            assert(entries[mode](&ctx) == 0 && ctx.steps == 3);
            if (i >= 0) {
                checksum += results[1].d;
                expected += args[0].d;
            }
        }
        assert(checksum == expected);
        printf(
            "%s arguments ns=%.3f checksum=%.12f\n",
            mode ? "GENERATED_C" : "SLJIT",
            (seconds() - start) * 1e9 / count,
            checksum
        );
    }
    behavior_asset_free(a);
}

static const behavior_function *pure_entry(behavior_asset *a)
{
    behavior_program_publish(a);
    graph *g = a->graphs[2];
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(a->program, g),
        graph_node_by_id(g, g->entry_id)
    );
    assert(f);
    return f;
}

static void compare_pure(void *library, int count, int rotation)
{
    behavior_asset *a = whole_pure_fixture();
    const behavior_function *f = pure_entry(a);
    int (*entries[])(behavior_function_context *) = {
        (int (*)(behavior_function_context *))behavior_function_entry(f),
        (int (*)(behavior_function_context *))dlsym(library, "generated_pure")
    };
    assert(entries[0] && entries[1]);
    double cases[] = { NAN, INFINITY, -INFINITY, -0., 0., 1.e-40, 16777217. };
    for (int k = 0; k < 7; k++) {
        for (int present = 0; present < 2; present++) {
            pin_value argument = { .type = PIN_F32, .f = (float)cases[k] };
            pin_value reference[2] = { 0 };
            behavior_function_context expected = { .arguments = &argument,
                                                   .argument_count = present,
                                                   .results = reference,
                                                   .steps = 123 };
            assert(!behavior_function_run(f, &expected, false) && expected.steps == 123);
            for (int mode = 0; mode < 2; mode++) {
                pin_value values[2] = { 0 };
                behavior_function_context actual = { .arguments = &argument,
                                                     .argument_count = present,
                                                     .results = values,
                                                     .steps = 123 };
                assert(!entries[mode](&actual) && actual.steps == 123);
                assert(actual.returned_node_id == expected.returned_node_id);
                for (int i = 0; i < 2; i++) {
                    assert(
                        isnan(reference[i].d) ? isnan(values[i].d)
                                              : values[i].d == reference[i].d
                    );
                    assert(
                        reference[i].d != 0 ||
                        signbit(reference[i].d) == signbit(values[i].d)
                    );
                }
            }
        }
    }
    puts("pure_parity=SLJIT,BYTECODE,GENERATED_C defaults,coercions,NaNs,zeros,all-returns,no-flow-steps");
    for (int pass = 0; pass < 2; pass++) {
        int mode = (pass + rotation) % 2;
        pin_value arg = { .type = PIN_F64 }, results[2] = { 0 };
        behavior_function_context ctx = { .arguments = &arg,
                                          .argument_count = 1,
                                          .results = results };
        double checksum = 0, expected = 0, start = 0;
        for (int i = -10000; i < count; i++) {
            if (!i) {
                start = seconds();
            }
            arg.d = (i & 1023) * .001;
            assert(!entries[mode](&ctx));
            if (i >= 0) {
                checksum += results[1].d;
                expected += (arg.d + .25) * (arg.d * 1.25);
            }
        }
        assert(checksum == expected);
        printf(
            "%s pure_body ns=%.3f checksum=%.12f\n",
            mode ? "GENERATED_C" : "SLJIT",
            (seconds() - start) * 1e9 / count,
            checksum
        );
    }
    behavior_asset_free(a);
}

static void measure_pure(behavior_backend backend, int count)
{
    behavior_asset *a = whole_pure_fixture();
    behavior_instance *instance = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, backend));
    behavior_instance_set_trace_enabled(instance, 0);
    eval_context ctx = { .instance = instance,
                         .runtime = runtime_store_create(a->graph) };
    const behavior_graph_program *p = behavior_program_graph(a->program, a->graph);
    arena storage = { 0 };
    double start = 0, checksum = 0, expected = 0;
    uint64_t allocations = 0, oracle = eval_oracle_calls();
    for (int i = -1000; i < count; i++) {
        if (!i) {
            start = seconds();
            allocations = runtime_store_creations();
        }
        double x = (i & 1023) * .001;
        instance->variables[0].d = x;
        runtime_store_reset_memo(ctx.runtime);
        pin_value value;
        int result = backend == BEHAVIOR_ORACLE
                       ? graph_eval_output(a->graph, a->graph->nodes[0], 1, &storage, &value, &ctx)
                       : behavior_program_output(p, a->graph->nodes[0], 1, &storage, &value, &ctx, backend);
        assert(!result);
        if (i >= 0) {
            checksum += value.d;
            expected += (x + .25) * (x * 1.25);
        }
    }
    double elapsed = seconds() - start;
    allocations = runtime_store_creations() - allocations;
    assert(checksum == expected);
    assert(allocations == (backend == BEHAVIOR_ORACLE ? 2 * (uint64_t)count : 0));
    assert(backend == BEHAVIOR_ORACLE || oracle == eval_oracle_calls());
    printf(
        "%s pure_call ns=%.3f checksum=%.12f callee_stores=%llu\n",
        behavior_backend_name(backend),
        elapsed * 1e9 / count,
        checksum,
        (unsigned long long)allocations
    );
    runtime_store_free(ctx.runtime);
    arena_free(&storage);
    behavior_instance_destroy(instance);
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
        behavior_asset *arguments = whole_arguments_fixture();
        behavior_function_write_c(arguments_entry(arguments), out, "generated_arguments");
        behavior_asset_free(arguments);
        behavior_asset *scopes = whole_scopes_fixture();
        behavior_function_write_c(whole_entry(scopes), out, "generated_scopes");
        behavior_asset_free(scopes);
        behavior_asset *pure = whole_pure_fixture();
        behavior_function_write_c(pure_entry(pure), out, "generated_pure");
        behavior_asset_free(pure);
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
        compare_control(library, false);
        compare_control(library, true);
        behavior_asset *scopes = whole_scopes_fixture();
        const behavior_function *scope_function = whole_entry(scopes);
        void *scope_c = dlsym(library, "generated_scopes");
        assert(scope_c);
        for (int i = 0; i < 2; i++) {
            bool generated = (i + rotation) % 2;
            measure_scopes(
                generated ? "GENERATED_C" : "SLJIT",
                (int (*)(behavior_function_context *))(
                    generated ? scope_c : behavior_function_entry(scope_function)
                ),
                count
            );
        }
        behavior_asset_free(scopes);
        compare_arguments(library, count, rotation);
        compare_pure(library, count, rotation);
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
        measure_pure(backend, count);
        measure(asset, sink, backend, count, false);
        measure(asset, sink, backend, count, true);
    }
    behavior_asset_free(asset);
    asset = fixture(&sink, true);
    measure(asset, sink, BEHAVIOR_SLJIT, count, true);
    behavior_asset_free(asset);
}
