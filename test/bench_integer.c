#include "test/bench_integer.h"

#include "graph/compiler/behavior_program.h"
#include "graph/nodes/conv_nodes.h"
#include "test/whole_integer_fixture.h"

#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <time.h>

static const behavior_function *entry(behavior_asset *a, bool pure)
{
    behavior_program_publish(a);
    graph *g = pure ? a->graphs[1] : a->graph;
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(a->program, g),
        g->nodes[0]
    );
    assert(f);
    return f;
}

static behavior_asset *cast_fixture(int i)
{
    const node_desc *descs[] = { &NODE_To_Byte,      &NODE_To_Integer,
                                 &NODE_To_Integer64, &NODE_To_Byte,
                                 &NODE_To_Integer,   &NODE_To_Integer64,
                                 &NODE_To_Real,      &NODE_To_Real,
                                 &NODE_To_Real,      &NODE_To_Integer64 };
    pin_type from[] = { PIN_F32, PIN_F32, PIN_F32, PIN_F64, PIN_F64,
                        PIN_F64, PIN_U8,  PIN_I32, PIN_I64, PIN_I32 };
    pin_type to[] = { PIN_U8,  PIN_I32, PIN_I64, PIN_U8,  PIN_I32,
                      PIN_I64, PIN_F64, PIN_F64, PIN_F64, PIN_I64 };
    return whole_integer_operation(descs[i], from[i], to[i]);
}

void emit_integer_c(FILE *out)
{
    char name[64];
    for (int type = 0; type < 2; type++) {
        for (int order = 0; order < 2; order++) {
            behavior_asset *a = whole_integer_fixture(type ? PIN_I64 : PIN_I32, order);
            for (int pure = 0; pure < 2; pure++) {
                snprintf(name, sizeof name, "generated_integer_%d_%d_%d", type, order, pure);
                behavior_function_write_c(entry(a, pure), out, name);
            }
            behavior_asset_free(a);
        }
    }
    for (int i = 0; i < 10; i++) {
        behavior_asset *a = cast_fixture(i);
        snprintf(name, sizeof name, "generated_cast_%d", i);
        behavior_function_write_c(entry(a, true), out, name);
        behavior_asset_free(a);
    }
}

static double seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static pin_value integer(pin_type type, int64_t value)
{
    return type == PIN_I32 ? (pin_value){ .type = type, .i = (int32_t)value }
                           : (pin_value){ .type = type, .i64 = value };
}
static int64_t number(pin_value value)
{
    return value.type == PIN_I32 ? value.i : value.i64;
}

static void compare_body(void *library, int count, int rotation, pin_type type, bool order, bool pure)
{
    behavior_asset *a = whole_integer_fixture(type, order);
    const behavior_function *f = entry(a, pure);
    char name[64];
    snprintf(name, sizeof name, "generated_integer_%d_%d_%d", type == PIN_I64, order, pure);
    int (*entries[])(behavior_function_context *) = {
        (int (*)(behavior_function_context *))behavior_function_entry(f),
        (int (*)(behavior_function_context *))dlsym(library, name)
    };
    assert(entries[0] && entries[1]);
    int64_t lo = type == PIN_I32 ? INT32_MIN : INT64_MIN,
            hi = type == PIN_I32 ? INT32_MAX : INT64_MAX;
    int64_t cases[] = { lo, lo + 1, -17, -1, 0, 1, 17, hi - 1, hi };
    for (int x = 0; x < 9; x++) {
        for (int y = 0; y < 9; y++) {
            for (int budget = 0; budget <= 8; budget++) {
                pin_value args[] = { integer(type, cases[x]),
                                     integer(type, cases[y]) };
                pin_value reference[4] = { 0 }, results[3] = { 0 };
                behavior_function_context expected = { .variables = reference,
                                                       .arguments = args,
                                                       .argument_count = budget % 3,
                                                       .results = results,
                                                       .budget = budget };
                int status = behavior_function_run(f, &expected, false);
                for (int mode = 0; mode < 2; mode++) {
                    pin_value variables[4] = { 0 }, values[3] = { 0 };
                    behavior_function_context actual = { .variables = variables,
                                                         .arguments = args,
                                                         .argument_count = budget % 3,
                                                         .results = values,
                                                         .budget = budget };
                    assert(
                        entries[mode](&actual) == status &&
                        actual.steps == expected.steps
                    );
                    assert(actual.returned_node_id == expected.returned_node_id);
                    if (status) {
                        assert(
                            actual.node_id == expected.node_id &&
                            actual.failure == expected.failure
                        );
                        assert(
                            actual.error_frame == expected.error_frame &&
                            actual.error_source == expected.error_source
                        );
                        assert(
                            actual.error_pin == expected.error_pin &&
                            actual.error_steps == expected.error_steps
                        );
                    }
                    assert(variables[0].i == reference[0].i);
                    for (int i = 0; i < 3; i++) {
                        assert(
                            type == PIN_I32
                                ? variables[i + 1].i == reference[i + 1].i
                                : variables[i + 1].i64 == reference[i + 1].i64
                        );
                        assert(
                            type == PIN_I32 ? values[i].i == results[i].i
                                            : values[i].i64 == results[i].i64
                        );
                    }
                }
            }
        }
    }
    if (pure && !order) {
        for (int pass = 0; pass < 3; pass++) {
            int mode = (pass + rotation) % 3;
            pin_value args[] = { integer(type, 0), integer(type, 1) }, results[3];
            for (int i = 0; i < 3; i++) {
                results[i] = integer(type, 0);
            }
            behavior_function_context ctx = { .arguments = args,
                                              .argument_count = 2,
                                              .results = results };
            uint64_t checksum = 0, expected = 0;
            for (int i = 0; i < count; i++) {
                int64_t x = (i & 1023) - 512, y = (i & 31) + 1;
                expected += (uint64_t)(x / y + x % y + x * x + y);
            }
            double start = 0;
            for (int i = -10000; i < count; i++) {
                if (!i) {
                    start = seconds();
                }
                int64_t x = (i & 1023) - 512, y = (i & 31) + 1;
                args[0] = integer(type, x);
                args[1] = integer(type, y);
                assert(
                    !(mode < 2 ? entries[mode](&ctx)
                               : behavior_function_run(f, &ctx, false))
                );
                if (i >= 0) {
                    checksum += (uint64_t)(number(results[0]) + number(results[1]) +
                                           number(results[2]));
                }
            }
            assert(checksum == expected);
            printf(
                "%s integer%d_body ns=%.3f checksum=%llu\n",
                (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                type == PIN_I32 ? 32 : 64,
                (seconds() - start) * 1e9 / count,
                (unsigned long long)checksum
            );
        }
    }
    behavior_asset_free(a);
}

static void compare_casts(void *library)
{
    double cases[] = { NAN,
                       INFINITY,
                       -INFINITY,
                       -.99,
                       -1.99,
                       0,
                       1,
                       254.99,
                       255,
                       256,
                       -0x1p31,
                       0x1p31 - 1,
                       0x1p31,
                       -0x1p63,
                       nextafter(-0x1p63, 0),
                       nextafter(0x1p63, 0),
                       0x1p63 };
    for (int i = 0; i < 10; i++) {
        behavior_asset *a = cast_fixture(i);
        const behavior_function *f = entry(a, true);
        char name[64];
        snprintf(name, sizeof name, "generated_cast_%d", i);
        int (*c)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))dlsym(library, name);
        assert(c);
        for (int k = 0; k < 17; k++) {
            pin_type type = a->variables[0].type;
            pin_value arg = { .type = type };
            if (type == PIN_F32) {
                arg.f = (float)cases[k];
            } else if (type == PIN_F64) {
                arg.d = cases[k];
            } else if (type == PIN_U8) {
                arg.u8 = k * 17;
            } else if (type == PIN_I32) {
                arg.i = k % 2 ? INT32_MIN + k : INT32_MAX - k;
            } else {
                arg.i64 = k % 2 ? INT64_MIN + k : INT64_MAX - k;
            }
            pin_value expected = { 0 }, actual = { 0 };
            behavior_function_context ctx = { .arguments = &arg,
                                              .argument_count = 1,
                                              .results = &expected };
            assert(!behavior_function_run(f, &ctx, false));
            ctx.results = &actual;
            assert(!c(&ctx));
            pin_type to = a->graphs[1]->nodes[1]->input_pins[0].type;
            switch (to) {
                case PIN_U8: assert(actual.u8 == expected.u8); break;
                case PIN_I32: assert(actual.i == expected.i); break;
                case PIN_I64: assert(actual.i64 == expected.i64); break;
                case PIN_F64: assert(actual.d == expected.d); break;
                default: assert(false);
            }
        }
        behavior_asset_free(a);
    }
}

void compare_integer_c(void *library, int count, int rotation)
{
    for (int type = 0; type < 2; type++) {
        for (int order = 0; order < 2; order++) {
            for (int pure = 0; pure < 2; pure++) {
                compare_body(library, count, rotation, type ? PIN_I64 : PIN_I32, order, pure);
            }
        }
    }
    compare_casts(library);
    puts("integer_parity=SLJIT,BYTECODE,GENERATED_C overflow,errors,defaults,casts,budgets,source-scopes");
}
