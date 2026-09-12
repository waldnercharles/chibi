#include "graph/compiler/behavior_function_internal.h"
#include "test/suite_internal.h"
#include "test/whole_array_fixture.h"

#include <dlfcn.h>
#include <time.h>

static const behavior_function *body(behavior_asset *a)
{
    behavior_program_publish(a);
    graph *g = a->graphs[3];
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(a->program, g),
        g->nodes[0]
    );
    assert(f && behavior_function_entry(f));
    for (int i = 0; i < acount(f->instructions); i++) {
        assert(
            !behavior_function_call(f->instructions[i].op) ||
            behavior_function_math(f->instructions[i].op)
        );
    }
    return f;
}
void emit_array_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_ARRAY_COUNT; kind++) {
        behavior_asset *a = whole_array_fixture(kind);
        char name[40];
        snprintf(name, sizeof name, "generated_array%d", kind);
        behavior_function_write_c(body(a), out, name);
        behavior_asset_free(a);
    }
    assert(!fclose(out));
}
static double seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void compare(
    const behavior_function *f,
    behavior_instance *bi,
    pin_value *args,
    int (*entry)(behavior_function_context *),
    unsigned budget
)
{
    pin_value results[7] = { 0 }, expected[7] = { 0 };
    arena storage = { 0 };
    behavior_function_prepare_results(f, expected, &storage);
    bi->variables[2].d = 0;
    behavior_function_context reference = { .arguments = args,
                                            .argument_count = 2,
                                            .results = expected,
                                            .budget = budget };
    behavior_instance_bind_values(bi, &reference);
    int status = behavior_function_run(f, &reference, false);
    double sum = bi->variables[2].d;
    bi->variables[2].d = 0;
    behavior_function_prepare_results(f, results, &storage);
    behavior_function_context actual = { .arguments = args,
                                         .argument_count = 2,
                                         .results = results,
                                         .budget = budget };
    behavior_instance_bind_values(bi, &actual);
    assert(
        entry(&actual) == status && actual.steps == reference.steps &&
        bi->variables[2].d == sum
    );
    if (status) {
        assert(
            actual.node_id == reference.node_id &&
            actual.error_pin == reference.error_pin &&
            actual.error_steps == reference.error_steps &&
            actual.failure == reference.failure &&
            actual.error_source == reference.error_source &&
            actual.error_stack == reference.error_stack
        );
    } else {
        for (int i = 1; i < bi->asset->graphs[3]->nodes[1]->input_count; i++) {
            if (expected[i].type == PIN_STRUCT) {
                assert((expected[i].ptr != NULL) == (results[i].ptr != NULL));
                if (expected[i].ptr) {
                    const whole_record *a = expected[i].ptr, *b = results[i].ptr;
                    assert(a->weight == b->weight && a->tag == b->tag && a->stamp == b->stamp);
                    assert(!memcmp(&a->position, &b->position, sizeof a->position));
                }
            } else {
                assert(expected[i].type == results[i].type);
                if (expected[i].type == PIN_F64) {
                    assert(expected[i].d == results[i].d);
                } else if (expected[i].type == PIN_BOOL) {
                    assert(expected[i].b == results[i].b);
                } else {
                    assert(expected[i].i == results[i].i);
                }
            }
        }
    }
    arena_free(&storage);
}
int bench_array(const char *path, int count, int rotation)
{
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library && count > 0);
    for (int kind = 0; kind < WHOLE_ARRAY_COUNT; kind++) {
        behavior_asset *a = whole_array_fixture(kind);
        const behavior_function *f = body(a);
        char name[40];
        snprintf(name, sizeof name, "generated_array%d", kind);
        int (*entries[])(behavior_function_context *) = {
            (int (*)(behavior_function_context *))behavior_function_entry(f),
            (int (*)(behavior_function_context *))dlsym(library, name)
        };
        assert(entries[0] && entries[1]);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        pin_value items[64];
        whole_record records[64];
        for (int i = 0; i < 64; i++) {
            records[i] = (whole_record){ .tag = 200 + i % 32,
                                         .weight = i + .25,
                                         .position = { i, -i },
                                         .stamp = INT64_MAX - i };
            items[i] = kind == WHOLE_ARRAY_STRUCT
                         ? (pin_value){ .type = PIN_STRUCT,
                                        .type_name = "WholeRecord",
                                        .ptr = &records[i] }
                         : (pin_value){ .type = PIN_F64,
                                        .type_name = "double",
                                        .d = i + .25 };
        }
        pin_array array = { .element_type = items[0].type,
                            .type_name = items[0].type_name,
                            .count = 4,
                            .capacity = 64,
                            .items = items };
        pin_value args[] = { { .type = items[0].type,
                               .container = PIN_CONTAINER_ARRAY,
                               .type_name = items[0].type_name,
                               .ptr = &array },
                             { .type = PIN_I32, .i = 2 } };
        for (int length = -1; length <= 4; length++) {
            array.count = length < 0 ? 0 : length;
            args[0].ptr = length < 0 ? NULL : &array;
            assert(!behavior_instance_set_variable(bi, a->variables[3].id, &args[0]));
            for (unsigned budget = 1; budget <= 32; budget++) {
                for (int mode = 0; mode < 2; mode++) {
                    compare(f, bi, args, entries[mode], budget);
                }
            }
        }
        if (kind == WHOLE_ARRAY_RANGE) {
            for (int i = 0; i < acount(f->instructions); i++) {
                assert(!behavior_function_call(f->instructions[i].op));
            }
            array.count = 64;
            args[0].ptr = &array;
            pin_value results[3] = { 0 };
            for (int order = 0; order < 3; order++) {
                int mode = (order + rotation) % 3;
                behavior_function_context ctx = { .arguments = args,
                                                  .argument_count = 2,
                                                  .results = results,
                                                  .budget = 200 };
                behavior_instance_bind_values(bi, &ctx);
                uint64_t checksum = 0;
                double start = 0;
                for (int i = -10000; i < count; i++) {
                    if (!i) {
                        start = seconds();
                    }
                    items[0].d = i & 7;
                    bi->variables[2].d = 0;
                    ctx.steps = 0;
                    int status = mode < 2 ? entries[mode](&ctx)
                                          : behavior_function_run(f, &ctx, false);
                    assert(
                        !status && results[1].d == 2031.75 + items[0].d &&
                        results[2].i == 63
                    );
                    if (i >= 0) {
                        checksum += (uint64_t)(results[1].d * 4);
                    }
                }
                printf(
                    "%s array_body ns=%.3f checksum=%llu\n",
                    (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                    (seconds() - start) * 1e9 / count,
                    (unsigned long long)checksum
                );
            }
        }
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    puts("PASS: generated-C/SLJIT/bytecode array state, results, errors and source stacks; no array helpers");
    return 0;
}
