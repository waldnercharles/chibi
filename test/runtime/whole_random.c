#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_random_fixture.h"

#include <dlfcn.h>
#include <float.h>
#include <time.h>

static behavior_function *lower(behavior_asset *asset)
{
    graph *g = asset->graphs[1];
    behavior_function_diagnostic diagnostic;
    behavior_function *f = behavior_function_lower(asset, g, g->nodes[0], &diagnostic);
    assert(f && behavior_function_compile(f));
    return f;
}
static void results(pin_value *out, const node *ret, float vectors[2][3])
{
    for (int i = 0; i < ret->input_count; i++) {
        out[i] = ret->input_pins[i].default_value;
        if (out[i].type == PIN_STRUCT) {
            out[i].ptr = vectors[i - 1];
        }
    }
}
static void same(pin_value actual, pin_value expected, int seed)
{
    assert(actual.type == expected.type && actual.container == expected.container);
    assert((actual.type_name == NULL) == (expected.type_name == NULL));
    assert(!actual.type_name || !strcmp(actual.type_name, expected.type_name));
    const void *a = actual.type == PIN_STRUCT ? actual.ptr : &actual.d;
    const void *b = expected.type == PIN_STRUCT ? expected.ptr : &expected.d;
    size_t size = actual.type == PIN_STRUCT ? 3 * sizeof(float)
                : actual.type == PIN_F32    ? sizeof(float)
                                            : sizeof(double);
    if (memcmp(a, b, size)) {
        fprintf(
            stderr,
            "random mismatch seed=%d type=%s actual=%a expected=%a\n",
            seed,
            pin_type_name(actual.type),
            actual.d,
            expected.d
        );
        assert(false);
    }
}
static void sample(
    behavior_asset *asset,
    behavior_function *f,
    pin_value *args,
    int seed,
    int budget,
    int (*generated)(behavior_function_context *)
)
{
    graph *g = asset->graphs[1];
    node *entry = g->nodes[0], *ret = g->nodes[1];
    int count = entry->output_count - 1;
    flow_options options = { .step_budget = budget };
    srand(seed);
    flow_run *oracle = flow_run_start(g, entry, (flow_inputs){ args, count }, &options, NULL);
    assert(oracle);
    int expected = flow_run_resume(oracle), next = rand();
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int mode = 0; mode < (generated ? 3 : 2); mode++) {
        pin_value out[4];
        float vectors[2][3];
        results(out, ret, vectors);
        behavior_function_context ctx = { .arguments = args,
                                          .argument_count = count,
                                          .results = out,
                                          .budget = budget };
        srand(seed);
        int result = mode == 2 ? generated(&ctx)
                               : behavior_function_run(f, &ctx, mode == 1);
        assert(result == expected && rand() == next && ctx.steps == flow_run_steps(oracle));
        if (result) {
            const flow_error *error = flow_run_error(oracle);
            assert(
                error->node && ctx.node_id == error->node->id &&
                ctx.error_steps == error->steps
            );
            if (result != FLOW_ERR_STEP_BUDGET) {
                assert(ctx.error_pin == error->pin);
                assert(ctx.error_message && !strcmp(ctx.error_message, error->message));
            }
        } else {
            for (int i = 1; i < ret->input_count; i++) {
                same(out[i], *flow_run_input_value(oracle, ret, i), seed);
            }
        }
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    flow_run_free(oracle);
}
static void arguments(pin_value *args, node *entry, float vectors[2][3], double first, double last)
{
    for (int i = 1; i < entry->output_count; i++) {
        args[i - 1] = entry->output_pins[i].default_value;
        double value = i == 1 ? first : last;
        if (args[i - 1].type == PIN_STRUCT) {
            for (int j = 0; j < 3; j++) {
                vectors[i - 1][j] = (float)value;
            }
            args[i - 1].ptr = vectors[i - 1];
        } else {
            args[i - 1].d = value;
        }
    }
}
static void inspection(int kind, bool loop)
{
    behavior_asset *asset = whole_random_fixture(kind);
    graph *g = asset->graph;
    node *event = g->nodes[0], *call = g->nodes[1], *write = g->nodes[2];
    node *delay = graph_add_node(g, &NODE_Delay);
    assert(!graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .25 }));
    assert(!graph_remove_edge(g, call, 0, write, 0));
    assert(!graph_connect_exec(g, call, 0, delay, 0, 0));
    assert(!graph_connect_exec(g, delay, 0, write, 0, 0));
    if (loop) {
        node *range = graph_add_node(g, &NODE_ForLoop);
        assert(!graph_set_pin_default(g, range, 0, 2, &(pin_value){ .type = PIN_I32, .i = 2 }));
        assert(!graph_remove_edge(g, event, 0, call, 0));
        assert(!graph_connect_exec(g, event, 0, range, 0, 0));
        assert(!graph_connect_exec(g, range, 0, call, 0, 0));
    }
    /* Resume, single-step, cancel at the first wait, then break at the write. */
    for (int control = 0; control < 4; control++) {
        assert(!graph_set_breakpoint(g, write, control == 3));
        test_flow_snapshot *expected = NULL;
        int next = 0;
        for (int mode = 0; mode < 3; mode++) {
            behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
            assert(!behavior_instance_set_backend(
                bi,
                (behavior_backend[]){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT }[mode]
            ));
            behavior_instance_set_trace_enabled(bi, true);
            srand(17);
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
            behavior_instance_set_breakpoints_enabled(bi, control == 3);
            flow_run *run = behavior_instance_enqueue(bi, event, (flow_inputs){ 0 }, NULL);
            assert(run);
            int result = control == 1 ? flow_run_step(run) : flow_run_resume(run),
                at = 0;
            for (;;) {
                test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, result);
                if (!mode) {
                    apush(expected, snapshot);
                } else {
                    assert(run->whole_function_executed && at < acount(expected));
                    test_flow_snapshot_same(snapshot, expected[at]);
                    test_flow_snapshot_free(&snapshot);
                }
                assert(++at < 64);
                flow_state state = flow_run_status(run);
                if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                    assert(state == FLOW_COMPLETED);
                    break;
                }
                if (control == 2 && state == FLOW_SUSPENDED) {
                    assert(!behavior_instance_stop_activation(bi, run));
                    assert(!behavior_instance_tick(bi, 100));
                    break;
                }
                result = state == FLOW_SUSPENDED
                           ? flow_run_tick_at(run, flow_run_wake_time(run))
                           : flow_run_step(run);
            }
            if (!mode) {
                next = rand();
            } else {
                assert(at == acount(expected) && rand() == next);
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            }
            behavior_instance_destroy(bi);
        }
        for (int i = 0; i < acount(expected); i++) {
            test_flow_snapshot_free(&expected[i]);
        }
        afree(expected);
    }
    behavior_asset_free(asset);
}
void test_compiled_random(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    double cases[][2] = { { 0, 1 },
                          { -7.5, 23.75 },
                          { 23.75, -7.5 },
                          { 2, 2 },
                          { -0., 0. },
                          { 0., -0. },
                          { -DBL_MAX, DBL_MAX },
                          { DBL_MAX, DBL_MAX },
                          { -FLT_MAX, FLT_MAX },
                          { NAN, 1 },
                          { 0, NAN },
                          { INFINITY, 0 },
                          { 0, -INFINITY },
                          { DBL_MIN, DBL_MIN * 2 },
                          { 1, 0x1.0000000000001p0 } };
    for (int kind = 0; kind < WHOLE_RANDOM_COUNT; kind++) {
        behavior_asset *asset = whole_random_fixture(kind);
        behavior_function *f = lower(asset);
        char name[32];
        snprintf(name, sizeof name, "generated_random%d", kind);
        int (*generated)(
            behavior_function_context *
        ) = library ? (int (*)(behavior_function_context *))dlsym(library, name) : NULL;
        assert(!library || generated);
        for (int c = 0; c < (kind ? (int)(sizeof cases / sizeof *cases) : 1); c++) {
            pin_value args[2];
            float vectors[2][3];
            arguments(args, asset->graphs[1]->nodes[0], vectors, cases[c][0], cases[c][1]);
            for (int seed = 0; seed < 32; seed++) {
                for (int budget = 1; budget <= 3; budget++) {
                    sample(asset, f, args, seed, budget, generated);
                }
            }
        }
        if (kind >= WHOLE_RANDOM_VECTOR) {
            for (int invalid = -1; invalid < 6; invalid++) {
                pin_value args[2];
                float vectors[2][3];
                arguments(args, asset->graphs[1]->nodes[0], vectors, -1, 1);
                vectors[0][1] = 17;
                vectors[1][1] = -7;
                vectors[0][2] = vectors[1][2] = -2;
                if (invalid >= 0) {
                    vectors[invalid / 3][invalid % 3] = NAN;
                }
                for (int seed = 0; seed < 32; seed++) {
                    sample(asset, f, args, seed, 3, generated);
                }
            }
            for (int missing = 0; missing < 2; missing++) {
                pin_value args[2];
                float vectors[2][3];
                arguments(args, asset->graphs[1]->nodes[0], vectors, 0, 1);
                args[missing].ptr = NULL;
                sample(asset, f, args, 17, 3, generated);
            }
        }
        behavior_function_free(f);
        behavior_asset_free(asset);
        inspection(kind, false);
        inspection(kind, true);
    }
    whole_random_smoke();
    if (library) {
        assert(!dlclose(library));
    }
    puts("PASS: whole random values and draw order across ranges, errors, nested calls, loops, waits, cancellation and inspection");
}
void emit_random_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_RANDOM_COUNT; kind++) {
        behavior_asset *asset = whole_random_fixture(kind);
        behavior_function *f = lower(asset);
        char name[32];
        snprintf(name, sizeof name, "generated_random%d", kind);
        behavior_function_write_c(f, out, name);
        behavior_function_free(f);
        behavior_asset_free(asset);
    }
    assert(!fclose(out));
}
static double seconds()
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return now.tv_sec + now.tv_nsec * 1e-9;
}
int bench_random(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int kind = 0; kind < WHOLE_RANDOM_COUNT; kind++) {
        behavior_asset *asset = whole_random_fixture(kind);
        behavior_function *f = lower(asset);
        node *entry = asset->graphs[1]->nodes[0], *ret = asset->graphs[1]->nodes[1];
        char name[32];
        snprintf(name, sizeof name, "generated_random%d", kind);
        int (*entries[])(behavior_function_context *) = {
            (int (*)(behavior_function_context *))behavior_function_entry(f),
            (int (*)(behavior_function_context *))dlsym(library, name)
        };
        assert(entries[0] && entries[1]);
        pin_value args[2], out[4];
        float input_vectors[2][3], output_vectors[2][3];
        arguments(args, entry, input_vectors, 0, 1);
        results(out, ret, output_vectors);
        behavior_function_activation activation = { 0 };
        behavior_function_activation_init(f, &activation);
        behavior_function_context ctx = { .arguments = args,
                                          .argument_count = entry->output_count - 1,
                                          .results = out,
                                          .activation = &activation,
                                          .budget = 3 };
        double expected = 0;
        int expected_next = 0;
        for (int order = 0; order < 3; order++) {
            int mode = (order + rotation) % 3;
            srand(813);
            double checksum = 0, start = 0;
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(),
                     allocations = 0;
            for (int i = -10000; i < count; i++) {
                if (!i) {
                    allocations = behavior_function_vm_allocations();
                    start = seconds();
                }
                ctx.steps = 0;
                assert(
                    !(mode == 2 ? behavior_function_run(f, &ctx, false)
                                : entries[mode](&ctx))
                );
                if (i >= 0) {
                    checksum += out[3].d;
                    for (int j = 1; j <= 2; j++) {
                        checksum += out[j].type == PIN_STRUCT
                                      ? ((float *)out[j].ptr)[0] +
                                            (double)((float *)out[j].ptr)[1] +
                                            ((float *)out[j].ptr)[2]
                                  : out[j].type == PIN_F32 ? out[j].f
                                                           : out[j].d;
                    }
                }
            }
            double elapsed = seconds() - start;
            int next = rand();
            if (!order) {
                expected = checksum;
                expected_next = next;
            }
            assert(checksum == expected && next == expected_next);
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            assert(allocations == behavior_function_vm_allocations());
            printf(
                "%s random%d ns=%.3f checksum=%.12f\n",
                (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                kind,
                elapsed * 1e9 / count,
                checksum
            );
        }
        behavior_function_activation_free(&activation);
        behavior_function_free(f);
        behavior_asset_free(asset);
    }
    assert(!dlclose(library));
    return 0;
}
