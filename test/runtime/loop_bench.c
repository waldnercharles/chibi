#include "graph/compiler/behavior_function_internal.h"
#include "test/suite_internal.h"
#include "test/whole_loop_fixture.h"

#include <dlfcn.h>
#include <limits.h>
#include <time.h>

static const behavior_function *body(behavior_asset *a)
{
    behavior_program_publish(a);
    graph *g = a->graphs[1];
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(a->program, g),
        g->nodes[0]
    );
    assert(f && behavior_function_entry(f));
    return f;
}
void emit_loop_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_LOOP_COUNT; kind++) {
        char name[40];
        snprintf(name, sizeof name, "generated_loop%d", kind);
        behavior_asset *a = whole_loop_fixture(kind);
        const behavior_function *f = body(a);
        if (kind == WHOLE_LOOP_RANGE) {
            behavior_loop_budget proof;
            assert(behavior_function_loop_budget(f, &proof));
            if (f->runtime_body) {
                behavior_function runtime = *f;
                runtime.instructions = f->runtime_body;
                assert(behavior_function_loop_budget(&runtime, &proof));
            }
        }
        behavior_function_write_c(f, out, name);
        behavior_asset_free(a);
    }
    assert(!fclose(out));
}
static double seconds()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec * 1e-9;
}
int bench_loop(const char *path, int count, int rotation, int length)
{
    assert(length > 0 && length <= 10000);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int kind = 0; kind < WHOLE_LOOP_COUNT; kind++) {
        char name[40];
        snprintf(name, sizeof name, "generated_loop%d", kind);
        behavior_asset *a = whole_loop_fixture(kind);
        const behavior_function *f = body(a);
        int (*entries[])(behavior_function_context *) = {
            (int (*)(behavior_function_context *))behavior_function_entry(f),
            (int (*)(behavior_function_context *))dlsym(library, name)
        };
        assert(entries[0] && entries[1]);
        const int ranges[][2] = { { 1, 7 },
                                  { 7, 1 },
                                  { 1, 1 },
                                  { INT_MAX - 1, INT_MAX },
                                  { INT_MIN + 1, INT_MIN },
                                  { INT_MIN, INT_MAX } };
        for (int range = 0; range < 6; range++) {
            for (int budget = 1; budget < 80; budget++) {
                int status = 0;
                behavior_function_context expected = { 0 };
                pin_value reference[2] = { 0 }, returned = { 0 };
                const uintptr_t initial_steps[] = { 0,
                                                    (unsigned)budget - 1,
                                                    (unsigned)budget,
                                                    (unsigned)budget + 1,
                                                    UINTPTR_MAX };
                for (int initial = 0; initial < 5; initial++) {
                    for (int mode = 0; mode < 3; mode++) {
                        pin_value variables[] = { { .type = PIN_I32 },
                                                  { .type = PIN_I32, .i = 1 } };
                        pin_value results[2] = { 0 };
                        pin_value args[] = {
                            { .type = PIN_I32, .i = ranges[range][0] },
                            { .type = PIN_I32, .i = ranges[range][1] }
                        };
                        behavior_function_context ctx = { .results = results,
                                                          .variables = variables,
                                                          .arguments = args,
                                                          .argument_count = 2,
                                                          .budget = (unsigned)budget,
                                                          .steps = initial_steps[initial] };
                        int result = mode ? entries[mode - 1](&ctx)
                                          : behavior_function_run(f, &ctx, false);
                        if (!mode) {
                            expected = ctx;
                            status = result;
                            memcpy(reference, variables, sizeof reference);
                            returned = results[1];
                        } else {
                            assert(result == status && ctx.steps == expected.steps);
                            assert(
                                variables[0].i == reference[0].i &&
                                variables[1].i == reference[1].i
                            );
                            if (result) {
                                assert(
                                    ctx.node_id == expected.node_id &&
                                    ctx.failure == expected.failure
                                );
                                assert(
                                    ctx.error_pin == expected.error_pin &&
                                    ctx.error_steps == expected.error_steps
                                );
                                assert(
                                    ctx.error_source == expected.error_source &&
                                    ctx.error_stack == expected.error_stack
                                );
                            } else {
                                assert(results[1].i == returned.i);
                            }
                        }
                    }
                }
            }
        }
        if (kind == WHOLE_LOOP_RANGE) {
            for (int order = 0; order < 3; order++) {
                int mode = (order + rotation) % 3;
                pin_value variables[] = { { .type = PIN_I32 },
                                          { .type = PIN_I32, .i = 1 } };
                pin_value args[] = { { .type = PIN_I32, .i = 1 },
                                     { .type = PIN_I32, .i = 64 } };
                pin_value results[2] = { 0 };
                behavior_function_context ctx = { .variables = variables,
                                                  .arguments = args,
                                                  .argument_count = 2,
                                                  .results = results,
                                                  .budget = (unsigned)(2 * length + 20) };
                uint64_t checksum = 0;
                double start = seconds();
                for (int i = 0; i < count; i++) {
                    args[1].i = length + (i & 7);
                    variables[0].i = 0;
                    ctx.steps = 0;
                    int result = mode < 2 ? entries[mode](&ctx)
                                          : behavior_function_run(f, &ctx, false);
                    assert(
                        !result && variables[0].i == args[1].i &&
                        results[1].i == args[1].i
                    );
                    checksum += (unsigned)variables[0].i;
                }
                printf(
                    "%s loop_body ns=%.3f checksum=%llu\n",
                    (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                    (seconds() - start) * 1e9 / count,
                    (unsigned long long)checksum
                );
            }
        }
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    puts("PASS: generated C, SLJIT and bytecode loop state, budgets and source stacks");
    return 0;
}
