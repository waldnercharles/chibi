#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_motion_fixture.h"

#include <dlfcn.h>
#include <float.h>
#include <time.h>

static behavior_function *lower(behavior_asset *asset)
{
    graph *g = asset->graphs[1];
    behavior_function_diagnostic diagnostic;
    behavior_function *f = behavior_function_lower(asset, g, g->nodes[0], &diagnostic);
    if (!f) {
        fprintf(stderr, "%s: %s\n", asset->name, diagnostic.reason);
    }
    assert(f && behavior_function_compile(f));
    return f;
}
static behavior_asset *pressure_fixture()
{
    behavior_asset *asset = whole_motion_fixture(WHOLE_MOTION_ROUND);
    graph *g = asset->graphs[1];
    node *ret = g->nodes[1], *first = g->nodes[2];
    assert(!graph_remove_edge(g, first, 0, ret, 1));
    const char *names[] = { "Square Root", "Floor", "Ceiling", "Round", "Sine" };
    node *values[10];
    for (int i = 0; i < 10; i++) {
        node *offset = graph_add_node(g, &NODE_Add);
        for (int pin = 0; pin < 2; pin++) {
            assert(!graph_set_pin_type(g, offset, 0, pin, PIN_F64, "double"));
        }
        assert(!graph_set_pin_type(g, offset, 1, 0, PIN_F64, "double"));
        assert(
            !graph_set_pin_default(g, offset, 0, 1, &(pin_value){ .type = PIN_F64, .d = i * .13 })
        );
        assert(!graph_connect_data(g, first, 0, offset, 0));
        values[i] = graph_add_node(g, registry_node_by_name(names[i % 5]));
        assert(!graph_connect_data(g, offset, 0, values[i], 0));
    }
    /* Every result remains live for the second sum, across both hardware
     * operations and libm calls, exceeding the native saved-register pool. */
    node *sum = first;
    for (int i = 0; i < 20; i++) {
        node *add = graph_add_node(g, &NODE_Add);
        for (int pin = 0; pin < 2; pin++) {
            assert(!graph_set_pin_type(g, add, 0, pin, PIN_F64, "double"));
        }
        assert(!graph_set_pin_type(g, add, 1, 0, PIN_F64, "double"));
        assert(!graph_connect_data(g, sum, 0, add, 0));
        assert(!graph_connect_data(g, values[i % 10], 0, add, 1));
        sum = add;
    }
    assert(!graph_connect_data(g, sum, 0, ret, 1));
    return asset;
}
static void same_value(pin_value actual, pin_value expected)
{
    assert(actual.type == expected.type);
    assert(actual.container == expected.container);
    assert((actual.type_name == NULL) == (expected.type_name == NULL));
    assert(!actual.type_name || !strcmp(actual.type_name, expected.type_name));
    double a = actual.type == PIN_F32 ? actual.f : actual.d;
    double b = expected.type == PIN_F32 ? expected.f : expected.d;
    assert(isnan(b) ? isnan(a) : a == b);
    assert(b != 0 || signbit(a) == signbit(b));
}
static void sample(
    behavior_asset *asset,
    behavior_function *f,
    pin_value *args,
    int budget,
    int (*generated)(behavior_function_context *)
)
{
    graph *g = asset->graphs[1];
    node *entry = g->nodes[0], *ret = g->nodes[1];
    int count = entry->output_count - 1;
    flow_options options = { .step_budget = budget };
    flow_run *oracle = flow_run_start(g, entry, (flow_inputs){ args, count }, &options, NULL);
    assert(oracle);
    int expected = flow_run_resume(oracle);
    const flow_error *error = flow_run_error(oracle);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int mode = 0; mode < (generated ? 3 : 2); mode++) {
        pin_value *results = NULL;
        asetlen(results, ret->input_count);
        for (int i = 0; i < ret->input_count; i++) {
            results[i] = ret->input_pins[i].default_value;
        }
        behavior_function_context ctx = { .arguments = args,
                                          .argument_count = count,
                                          .results = results,
                                          .budget = budget };
        int result = mode == 2 ? generated(&ctx)
                               : behavior_function_run(f, &ctx, mode == 1);
        assert(result == expected && ctx.steps == flow_run_steps(oracle));
        if (result) {
            assert(error->node && error->node->id == ctx.node_id);
            assert(ctx.error_steps == error->steps);
            if (result != FLOW_ERR_STEP_BUDGET) {
                assert(ctx.error_pin == error->pin);
                assert(ctx.error_message && !strcmp(ctx.error_message, error->message));
            }
        } else {
            for (int i = 1; i < ret->input_count; i++) {
                same_value(results[i], *flow_run_input_value(oracle, ret, i));
            }
        }
        afree(results);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    flow_run_free(oracle);
}

static void inspection(int kind)
{
    behavior_asset *asset = whole_motion_fixture(kind);
    graph *g = asset->graphs[1];
    node *entry = g->nodes[0], *ret = g->nodes[1];
    node *delay = graph_add_node(g, &NODE_Delay);
    assert(!graph_remove_edge(g, entry, 0, ret, 0));
    assert(!graph_connect_exec(g, entry, 0, delay, 0, 0));
    assert(!graph_connect_exec(g, delay, 0, ret, 0, 0));
    pin_value args[6];
    for (int i = 1; i < entry->output_count; i++) {
        args[i - 1] = entry->output_pins[i].default_value;
        if (args[i - 1].type == PIN_F32) {
            args[i - 1].f = .5f;
        } else {
            args[i - 1].d = .5;
        }
    }
    for (int budget = 1; budget <= 6; budget++) {
        for (int stepping = 0; stepping < 2; stepping++) {
            test_flow_snapshot *expected = NULL;
            for (int mode = 0; mode < 3; mode++) {
                behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
                assert(!behavior_instance_set_backend(
                    bi,
                    (behavior_backend[]){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT }[mode]
                ));
                behavior_instance_set_trace_enabled(bi, true);
                flow_options options = { .instance = bi, .step_budget = budget };
                flow_run *run = flow_run_start(
                    asset->graph,
                    asset->graph->nodes[0],
                    (flow_inputs){ args, entry->output_count - 1 },
                    &options,
                    NULL
                );
                assert(run);
                uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
                int result = stepping ? flow_run_step(run) : flow_run_resume(run),
                    at = 0;
                for (;;) {
                    test_flow_snapshot s = test_flow_snapshot_take(run, bi, result);
                    if (!mode) {
                        apush(expected, s);
                    } else {
                        assert(run->whole_function_executed && at < acount(expected));
                        test_flow_snapshot_same(s, expected[at]);
                        test_flow_snapshot_free(&s);
                    }
                    assert(++at < 32);
                    flow_state state = flow_run_status(run);
                    if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                        break;
                    }
                    result = state == FLOW_SUSPENDED
                               ? flow_run_tick_at(run, flow_run_wake_time(run))
                               : flow_run_step(run);
                }
                if (mode) {
                    assert(at == acount(expected));
                    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                }
                flow_run_free(run);
                behavior_instance_destroy(bi);
            }
            for (int i = 0; i < acount(expected); i++) {
                test_flow_snapshot_free(&expected[i]);
            }
            afree(expected);
        }
    }
    behavior_asset_free(asset);
}

static void primitives(int kind, void *library)
{
    behavior_asset *asset = whole_motion_fixture(kind);
    behavior_function *f = lower(asset);
    char symbol[32];
    snprintf(symbol, sizeof symbol, "generated_motion%d", kind);
    int (*generated)(
        behavior_function_context *
    ) = library ? (int (*)(behavior_function_context *))dlsym(library, symbol) : NULL;
    assert(!library || generated);
    node *entry = asset->graphs[1]->nodes[0];
    double values[] = { NAN,
                        INFINITY,
                        -INFINITY,
                        -0.,
                        0.,
                        -.5,
                        .5,
                        -1.5,
                        1.5,
                        nextafter(.5, 0),
                        nextafter(-.5, 0),
                        DBL_MAX,
                        -DBL_MAX,
                        DBL_MIN,
                        -DBL_MIN,
                        0x1p52 - .5,
                        -0x1p52 + .5,
                        FLT_MAX,
                        FLT_TRUE_MIN,
                        .125 };
    int inputs = entry->output_count - 1;
    int samples = kind == WHOLE_MOTION_ANGLE ? 400 : 20 * inputs;
    for (int index = 0; index < samples; index++) {
        pin_value args[6];
        for (int i = 0; i < inputs; i++) {
            args[i] = entry->output_pins[i + 1].default_value;
            double value = kind == WHOLE_MOTION_ANGLE
                             ? values[i ? index / 20 : index % 20]
                         : index / 20 == i ? values[index % 20]
                         : i == 5          ? .125
                                           : 1;
            if (args[i].type == PIN_F32) {
                args[i].f = (float)value;
            } else {
                args[i].d = value;
            }
        }
        for (int budget = 1; budget <= 3; budget++) {
            sample(asset, f, args, budget, generated);
        }
    }
    if (kind == WHOLE_MOTION_SPRING) {
        uint32_t seed = 0xdeadbeef;
        for (int sample_index = 0; sample_index < 1000; sample_index++) {
            pin_value args[6];
            for (int i = 0; i < 6; i++) {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                args[i] = entry->output_pins[i + 1].default_value;
                memcpy(&args[i].f, &seed, sizeof seed);
            }
            sample(asset, f, args, 3, generated);
        }
    }
    behavior_function_free(f);
    behavior_asset_free(asset);
}
static void comparison_branches()
{
    const char *names[] = { "Less",    "Less Or Equal",
                            "Greater", "Greater Or Equal",
                            "Equal",   "Not Equal" };
    double cases[] = { NAN, INFINITY, -INFINITY, -0., 0., -1., 1., .25 };
    for (int width = 0; width < 2; width++) {
        for (int op = 0; op < 6; op++) {
            behavior_asset *a = behavior_asset_new("Floating branches");
            graph *g = a->graph;
            node *event = graph_add_node(g, &NODE_Event),
                 *branch = graph_add_node(g, &NODE_Branch);
            node *compare = graph_add_node(g, registry_node_by_name(names[op]));
            assert(!graph_set_event_name(g, event, "Run"));
            for (int i = 0; i < 2; i++) {
                pin_type type = width ? PIN_F64 : PIN_F32;
                const char *name = width ? "double" : "float";
                pin_spec p = { .name = i ? "B" : "A",
                               .kind = PIN_DATA,
                               .type = type,
                               .type_name = name,
                               .default_value = { .type = type } };
                assert(graph_add_pin(g, event, 1, &p) == i + 1);
                assert(!graph_connect_data(g, event, i + 1, compare, i));
            }
            behavior_variable *v = behavior_asset_add_variable(
                a,
                "Result",
                PIN_BOOL,
                "bool",
                PIN_CONTAINER_SINGLE,
                NULL,
                ""
            );
            uint64_t id = v->id;
            assert(!graph_connect_exec(g, event, 0, branch, 0, 0));
            assert(!graph_connect_data(g, compare, 0, branch, 1));
            for (int i = 0; i < 2; i++) {
                node *set = behavior_asset_add_set_node(a, id);
                assert(
                    !graph_set_pin_default(g, set, 0, 1, &(pin_value){ .type = PIN_BOOL, .b = !i })
                );
                assert(!graph_connect_exec(g, branch, i, set, 0, 0));
            }
            for (int mode = 0; mode < 3; mode++) {
                behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
                assert(!behavior_instance_set_backend(
                    bi,
                    (behavior_backend[]){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT }[mode]
                ));
                behavior_instance_set_trace_enabled(bi, false);
                for (int x = 0; x < 8; x++) {
                    for (int y = 0; y < 8; y++) {
                        pin_value args[2];
                        for (int i = 0; i < 2; i++) {
                            args[i] = event->output_pins[i + 1].default_value;
                            double value = cases[i ? y : x];
                            if (width) {
                                args[i].d = value;
                            } else {
                                args[i].f = (float)value;
                            }
                        }
                        double left = cases[x], right = cases[y];
                        bool want = (bool[]){ left<right, left <= right, left> right,
                                              left >= right,
                                              left == right,
                                              left != right }[op];
                        uint64_t eval = eval_oracle_calls(),
                                 flow = flow_oracle_calls();
                        flow_run *run = behavior_instance_dispatch(
                            bi,
                            "Run",
                            (flow_inputs){ args, 2 },
                            NULL
                        );
                        assert(
                            run && flow_run_status(run) == FLOW_COMPLETED &&
                            bi->variables[0].b == want
                        );
                        if (mode) {
                            assert(
                                run->whole_function_executed &&
                                eval == eval_oracle_calls() &&
                                flow == flow_oracle_calls()
                            );
                        }
                        assert(!behavior_instance_release_activation(bi, run));
                    }
                }
                behavior_instance_destroy(bi);
            }
            behavior_asset_free(a);
        }
    }
}
void emit_motion_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind <= WHOLE_MOTION_COUNT; kind++) {
        behavior_asset *asset = kind == WHOLE_MOTION_COUNT
                                  ? pressure_fixture()
                                  : whole_motion_fixture(kind);
        behavior_function *f = lower(asset);
        char symbol[32];
        snprintf(symbol, sizeof symbol, "generated_motion%d", kind);
        behavior_function_write_c(f, out, symbol);
        behavior_function_free(f);
        behavior_asset_free(asset);
    }
    assert(!fclose(out));
}
void test_compiled_motion(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    comparison_branches();
    for (int kind = 0; kind < WHOLE_MOTION_COUNT; kind++) {
        primitives(kind, library);
        inspection(kind);
    }
    behavior_asset *pressure = pressure_fixture();
    behavior_function *f = lower(pressure);
    int (*generated)(
        behavior_function_context *
    ) = library
          ? (int (*)(behavior_function_context *))dlsym(library, "generated_motion7")
          : NULL;
    assert(!library || generated);
    for (int i = -100; i <= 100; i++) {
        pin_value arg = pressure->graphs[1]->nodes[0]->output_pins[1].default_value;
        arg.d = i * .125;
        sample(pressure, f, &arg, 3, generated);
    }
    behavior_function_free(f);
    behavior_asset_free(pressure);
    whole_motion_smoke();
    if (library) {
        assert(!dlclose(library));
    }
    test_note("PASS: whole spring, angle, easing, sqrt and rounding match oracle values, errors, source pins, budgets, nested watches and stepping\n");
}

static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void benchmark_inputs(pin_value *args, const node *entry, int sample)
{
    for (int i = 1; i < entry->output_count; i++) {
        args[i - 1] = entry->output_pins[i].default_value;
        double value = i == 6 ? .016 : .125 + (sample & 1023) * .001 + i * .25;
        if (args[i - 1].type == PIN_F32) {
            args[i - 1].f = (float)value;
        } else {
            args[i - 1].d = value;
        }
    }
}
int bench_motion(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int kind = 0; kind < WHOLE_MOTION_COUNT; kind++) {
        behavior_asset *asset = whole_motion_fixture(kind);
        behavior_function *f = lower(asset);
        node *entry = asset->graphs[1]->nodes[0], *ret = asset->graphs[1]->nodes[1];
        char symbol[32];
        snprintf(symbol, sizeof symbol, "generated_motion%d", kind);
        int (*entries[])(behavior_function_context *) = {
            (int (*)(behavior_function_context *))behavior_function_entry(f),
            (int (*)(behavior_function_context *))dlsym(library, symbol)
        };
        assert(entries[0] && entries[1]);
        pin_value args[6], results[3];
        for (int i = 0; i < ret->input_count; i++) {
            results[i] = ret->input_pins[i].default_value;
        }
        behavior_function_context ctx = { .arguments = args,
                                          .argument_count = entry->output_count - 1,
                                          .results = results,
                                          .budget = 3 };
        double reference[1024];
        for (int i = 0; i < 1024; i++) {
            benchmark_inputs(args, entry, i);
            ctx.steps = 0;
            assert(!behavior_function_run(f, &ctx, false));
            reference[i] = results[1].type == PIN_F32
                             ? results[1].f + (double)results[2].f
                             : results[1].d;
        }
        for (int order = 0; order < 3; order++) {
            int mode = (order + rotation) % 3;
            double start = 0, checksum = 0, expected = 0;
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
            for (int i = -10000; i < count; i++) {
                if (!i) {
                    start = seconds();
                }
                benchmark_inputs(args, entry, i);
                ctx.steps = 0;
                assert(
                    !(mode == 2 ? behavior_function_run(f, &ctx, false)
                                : entries[mode](&ctx))
                );
                if (i >= 0) {
                    checksum += results[1].type == PIN_F32
                                  ? results[1].f + (double)results[2].f
                                  : results[1].d;
                    expected += reference[i & 1023];
                }
            }
            double elapsed = seconds() - start;
            assert(
                checksum == expected && eval == eval_oracle_calls() &&
                flow == flow_oracle_calls()
            );
            printf(
                "%s motion%d ns=%.3f checksum=%.12f\n",
                (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                kind,
                elapsed * 1e9 / count,
                checksum
            );
        }
        behavior_function_free(f);
        behavior_asset_free(asset);
    }
    assert(!dlclose(library));
    return 0;
}
