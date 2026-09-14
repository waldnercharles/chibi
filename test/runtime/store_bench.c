#include "test/suite_internal.h"
#include "test/whole_store_fixture.h"

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
    return f;
}
void emit_store_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_STORE_COUNT; kind++) {
        char name[40];
        snprintf(name, sizeof name, "generated_store%d", kind);
        behavior_asset *a = whole_store_fixture(kind);
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
static const char *unexpected_name_helper(void *, const char *)
{
    assert(!"known-type stores must not call an ownership helper");
    return NULL;
}
static void same(const whole_record *a, const whole_record *b)
{
    assert((a != NULL) == (b != NULL));
    if (!a) {
        return;
    }
    assert(a->tag == b->tag && a->active == b->active && a->stamp == b->stamp);
    assert(!memcmp(&a->position, &b->position, sizeof a->position));
    assert(!memcmp(&a->weight, &b->weight, sizeof a->weight));
}
int bench_store(const char *path, int count, int rotation)
{
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library && count > 0);
    for (int kind = 0; kind < WHOLE_STORE_COUNT; kind++) {
        behavior_asset *a = whole_store_fixture(kind);
        const behavior_function *f = body(a);
        char name[40];
        snprintf(name, sizeof name, "generated_store%d", kind);
        int (*entries[])(behavior_function_context *) = {
            (int (*)(behavior_function_context *))behavior_function_entry(f),
            (int (*)(behavior_function_context *))dlsym(library, name)
        };
        assert(entries[0] && entries[1]);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        whole_record input = { .tag = 251,
                               .position = { -0.f, 4 },
                               .weight = -0.,
                               .active = true,
                               .stamp = INT64_MIN + 7 };
        pin_value arg = { .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = &input };
        pin_value empty = { .type = PIN_STRUCT, .type_name = "WholeRecord" };
        arena storage = { 0 };
        for (unsigned budget = 1; budget < 18; budget++) {
            for (int missing = 0; missing < 2; missing++) {
                arg.ptr = missing ? NULL : &input;
                pin_value results[2] = { 0 };
                behavior_function_prepare_results(f, results, &storage);
                assert(!behavior_instance_set_variable(bi, a->variables[0].id, &empty));
                behavior_function_context expected = { .arguments = &arg,
                                                       .argument_count = 1,
                                                       .results = results,
                                                       .budget = budget };
                behavior_instance_bind_values(bi, &expected);
                int status = behavior_function_run(f, &expected, false);
                bool stored = bi->variables[0].ptr != NULL,
                     returned = !status && results[1].ptr;
                whole_record expected_value = { 0 }, expected_result = { 0 };
                if (stored) {
                    expected_value = *(whole_record *)bi->variables[0].ptr;
                }
                if (returned) {
                    expected_result = *(whole_record *)results[1].ptr;
                }
                for (int mode = 0; mode < 2; mode++) {
                    assert(!behavior_instance_set_variable(bi, a->variables[0].id, &empty));
                    behavior_function_prepare_results(f, results, &storage);
                    behavior_function_context actual = { .arguments = &arg,
                                                         .argument_count = 1,
                                                         .results = results,
                                                         .budget = budget };
                    behavior_instance_bind_values(bi, &actual);
                    assert(entries[mode](&actual) == status);
                    assert(
                        actual.steps == expected.steps &&
                        actual.returned_node_id == expected.returned_node_id
                    );
                    same(bi->variables[0].ptr, stored ? &expected_value : NULL);
                    if (status) {
                        assert(
                            actual.node_id == expected.node_id &&
                            actual.error_pin == expected.error_pin &&
                            actual.error_source == expected.error_source &&
                            actual.error_stack == expected.error_stack
                        );
                    } else {
                        same(results[1].ptr, returned ? &expected_result : NULL);
                    }
                }
                arena_reset(&storage);
            }
        }
        arg.ptr = &input;
        if (kind == WHOLE_STORE_COPY || kind == WHOLE_STORE_MAKE) {
            pin_value results[2] = { 0 };
            behavior_function_prepare_results(f, results, &storage);
            for (int pass = 0; pass < 3; pass++) {
                int mode = (pass + rotation) % 3;
                behavior_function_context ctx = { .arguments = &arg,
                                                  .argument_count = 1,
                                                  .results = results,
                                                  .budget = 100 };
                behavior_instance_bind_values(bi, &ctx);
                if (kind == WHOLE_STORE_MAKE) {
                    ctx.own_type_name = unexpected_name_helper;
                }
                double start = 0, checksum = 0, expected = 0;
                for (int i = -10000; i < count; i++) {
                    if (!i) {
                        start = seconds();
                    }
                    input.position.x = (i & 1023) * .125f;
                    ctx.steps = 0;
                    assert(
                        !(mode == 2 ? behavior_function_run(f, &ctx, false)
                                    : entries[mode](&ctx))
                    );
                    if (i >= 0) {
                        checksum += ((whole_record *)bi->variables[0].ptr)
                                        ->position.x;
                        expected += (i & 1023) * .125f +
                                    (kind == WHOLE_STORE_MAKE ? .5f : 0);
                    }
                }
                assert(checksum == expected);
                printf(
                    "%s %s ns=%.3f checksum=%.12f\n",
                    (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                    kind == WHOLE_STORE_MAKE ? "store_body" : "store_dynamic_body",
                    (seconds() - start) * 1e9 / count,
                    checksum
                );
            }
        }
        arena_free(&storage);
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    puts("store_parity=SLJIT,BYTECODE,GENERATED_C persistent-fields,metadata,nulls,snapshots,loops,budgets");
    return 0;
}
