#include "graph/compiler/behavior_program.h"
#include "graph/runtime/flow.h"
#include "support/arena.h"
#include "test/whole_aggregate_fixture.h"

#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <string.h>
#include <time.h>

static const behavior_function *body(behavior_asset *a, int graph)
{
    behavior_program_publish(a);
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(a->program, a->graphs[graph]),
        a->graphs[graph]->nodes[0]
    );
    assert(f && behavior_function_entry(f));
    return f;
}
void emit_aggregate_c(FILE *out)
{
    behavior_asset *a = whole_aggregate_fixture();
    behavior_function_write_c(body(a, 2), out, "generated_aggregate");
    behavior_asset_free(a);
    a = whole_aggregate_scopes();
    behavior_function_write_c(body(a, 4), out, "generated_aggregate_scopes");
    behavior_asset_free(a);
    for (int project = 0; project < 2; project++) {
        a = whole_aggregate_identity(project);
        behavior_function_write_c(body(a, 1), out, project ? "generated_projection" : "generated_identity");
        behavior_asset_free(a);
    }
}
static double seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void same(const whole_record *a, const whole_record *b)
{
    assert(a->tag == b->tag && a->stamp == b->stamp && a->active == b->active);
    assert(
        isnan(a->weight)
            ? isnan(b->weight)
            : a->weight == b->weight && signbit(a->weight) == signbit(b->weight)
    );
    assert(
        isnan(a->position.x) ? isnan(b->position.x)
                             : a->position.x == b->position.x &&
                                   signbit(a->position.x) == signbit(b->position.x)
    );
    assert(
        isnan(a->position.y) ? isnan(b->position.y)
                             : a->position.y == b->position.y &&
                                   signbit(a->position.y) == signbit(b->position.y)
    );
}
void compare_aggregate_c(void *library, int count, int rotation)
{
    whole_record input = { .tag = 251,
                           .position = { 3, 4 },
                           .weight = 2.75,
                           .active = true,
                           .stamp = INT64_MIN + 7 };
    pin_value args[] = { { .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = &input },
                         { .type = PIN_F32, .f = .25f } };
    const char *names[] = { "generated_aggregate",
                            "generated_identity",
                            "generated_projection",
                            "generated_aggregate_scopes" };
    for (int fixture = 0; fixture < 4; fixture++) {
        behavior_asset *a = fixture == 3 ? whole_aggregate_scopes()
                          : fixture ? whole_aggregate_identity(fixture == 2)
                                    : whole_aggregate_fixture();
        const behavior_function *f = body(
            a,
            fixture == 3 ? 4
            : fixture    ? 1
                         : 2
        );
        int (*entries[])(behavior_function_context *) = {
            (int (*)(behavior_function_context *))behavior_function_entry(f),
            (int (*)(behavior_function_context *))dlsym(library, names[fixture])
        };
        assert(entries[0] && entries[1]);
        for (int sample = 0; sample < 8; sample++) {
            double value[] = { NAN,    -0., INFINITY, -INFINITY,
                               1.e-40, -3., 0.,       2.75 };
            input.weight = value[sample];
            input.position = (whole_pair){ (float)value[sample], -(float)value[sample] };
            args[0].ptr = sample == 6 ? NULL : &input;
            arena memory = { 0 };
            pin_value reference[3] = { 0 };
            behavior_function_prepare_results(f, reference, &memory);
            behavior_function_context expected = { .arguments = args,
                                                   .argument_count = 2,
                                                   .results = reference,
                                                   .budget = 100 };
            int status = behavior_function_run(f, &expected, false);
            for (int mode = 0; mode < 2; mode++) {
                pin_value results[3] = { 0 };
                behavior_function_prepare_results(f, results, &memory);
                behavior_function_context actual = { .arguments = args,
                                                     .argument_count = 2,
                                                     .results = results,
                                                     .budget = 100 };
                assert(entries[mode](&actual) == status);
                assert(
                    actual.steps == expected.steps &&
                    actual.returned_node_id == expected.returned_node_id
                );
                if (status) {
                    assert(
                        actual.node_id == expected.node_id &&
                        actual.error_pin == expected.error_pin &&
                        actual.error_source == expected.error_source
                    );
                } else if (fixture == 2) {
                    assert(
                        isnan(reference[0].d) ? isnan(results[0].d)
                                              : reference[0].d == results[0].d
                    );
                } else {
                    assert(
                        (results[fixture == 3 ? 1 : 0].ptr != NULL) ==
                        (reference[fixture == 3 ? 1 : 0].ptr != NULL)
                    );
                    if (reference[fixture == 3 ? 1 : 0].ptr) {
                        same(
                            reference[fixture == 3 ? 1 : 0].ptr,
                            results[fixture == 3 ? 1 : 0].ptr
                        );
                    }
                }
            }
            arena_free(&memory);
        }
        if (!fixture) {
            whole_record result;
            pin_value results[] = {
                { .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = &result },
                { .type = PIN_F32 }
            };
            args[0].ptr = &input;
            for (int pass = 0; pass < 3; pass++) {
                int mode = (pass + rotation) % 3;
                behavior_function_context ctx = { .arguments = args,
                                                  .argument_count = 2,
                                                  .results = results,
                                                  .budget = 100 };
                double start = 0, checksum = 0, expected = 0;
                for (int i = -10000; i < count; i++) {
                    if (!i) {
                        start = seconds();
                    }
                    input.position.x = (i & 1023) * .125f;
                    input.position.y = -(i & 1023) * .125f;
                    if (mode == 2) {
                        assert(!behavior_function_run(f, &ctx, false));
                    } else {
                        assert(!entries[mode](&ctx));
                    }
                    if (i >= 0) {
                        checksum += result.position.x;
                        expected += (i & 1023) * .125f + .5f;
                    }
                }
                assert(checksum == expected);
                printf(
                    "%s aggregate_body ns=%.3f checksum=%.12f\n",
                    (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                    (seconds() - start) * 1e9 / count,
                    checksum
                );
            }
        }
        behavior_asset_free(a);
    }
    puts("aggregate_parity=SLJIT,BYTECODE,GENERATED_C nested-fields,projected-results,nulls,error-sources,owned-returns");
}
