#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/instance.h"
#include "test/suite_internal.h"
#include "test/whole_command_fixture.h"

#include <dlfcn.h>
#include <time.h>

static const behavior_function *body(behavior_asset *a)
{
    graph *g = a->graphs[acount(a->graphs) - 1];
    behavior_program_publish(a);
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(behavior_program_cached(a), g),
        g->nodes[0]
    );
    assert(f);
    return f;
}
void emit_command_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_COMMAND_COUNT; kind++) {
        for (int bad = 0; bad < 3; bad++) {
            behavior_asset *a = whole_command_fixture(kind, bad == 1, bad == 2 && kind == WHOLE_COMMAND_ARGUMENTS);
            char name[40];
            snprintf(name, sizeof name, "generated_command%d_%d", kind, bad);
            behavior_function_write_c(body(a), out, name);
            behavior_asset_free(a);
        }
    }
    assert(!fclose(out));
}
static double seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void same(const pin_value *a, const pin_value *b, int count)
{
    for (int i = 0; i < count; i++) {
        assert(a[i].type == b[i].type && a[i].container == b[i].container);
        assert(!memcmp(&a[i].d, &b[i].d, behavior_scalar_size(a[i].type)));
        assert((a[i].type_name != NULL) == (b[i].type_name != NULL));
        if (a[i].type_name) {
            assert(!strcmp(a[i].type_name, b[i].type_name));
        }
    }
}
int bench_command(const char *path, int count, int rotation)
{
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int kind = 0; kind < WHOLE_COMMAND_COUNT; kind++) {
        for (int bad = 0; bad < 3; bad++) {
            behavior_asset *a = whole_command_fixture(kind, bad == 1, bad == 2 && kind == WHOLE_COMMAND_ARGUMENTS);
            const behavior_function *f = body(a);
            char name[40];
            snprintf(name, sizeof name, "generated_command%d_%d", kind, bad);
            int (*entries[])(behavior_function_context *) = {
                (int (*)(behavior_function_context *))behavior_function_entry(f),
                (int (*)(behavior_function_context *))dlsym(library, name)
            };
            assert(entries[0] && entries[1]);
            pin_value items[] = { { .type = PIN_F64, .type_name = "item", .d = 1 },
                                  { .type = PIN_F64, .d = 2 } };
            pin_array array = { .element_type = PIN_F64,
                                .type_name = "double",
                                .items = items,
                                .count = 2,
                                .capacity = 2 };
            pin_value args[] = { { .type = PIN_I32, .type_name = "integer arg", .i = -13 },
                                 { .type = PIN_F64, .type_name = "double arg", .d = 2.75 },
                                 { .type = PIN_F32, .type_name = "float arg", .f = 1.5 },
                                 { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 123 },
                                 { .type = PIN_F64,
                                   .type_name = "double",
                                   .container = PIN_CONTAINER_ARRAY,
                                   .ptr = &array } },
                      result[5] = { 0 };
            behavior_instance *owner = behavior_instance_create(a, NULL, NULL, NULL);
            behavior_function_context ctx = {
                .arguments = args,
                .argument_count = kind == WHOLE_COMMAND_ARRAY ? 5 : 4,
                .results = result,
                .delta_seconds = .125,
                .owner = 123,
                .owner_type_name = "ecs.entity"
            };
            behavior_instance_bind_values(owner, &ctx);
            behavior_function_prepare_host(f, &ctx);
            for (int scenario = 0; scenario < 3; scenario++) {
                for (unsigned budget = 1; budget <= 25; budget++) {
                    whole_command_capture expected = { 0 };
                    pin_value expected_vars[4], expected_results[5];
                    behavior_function_context expected_ctx = { 0 };
                    int status = 0;
                    for (int mode = 0; mode < 3; mode++) {
                        whole_command_capture c = { .record = true,
                                                    .fail_at = 2,
                                                    .error = scenario == 2 ? FLOW_ERR_STATE
                                                                           : 0 };
                        for (int j = 0; j < acount(ctx.commands); j++) {
                            ctx.commands[j].service = scenario == 1 ? NULL : &c;
                        }
                        for (int j = 0; j < 4; j++) {
                            ctx.variables[j] = a->variables[j].default_value;
                        }
                        graph *g = a->graphs[acount(a->graphs) - 1];
                        for (int j = 0; j < 5; j++) {
                            result[j] = g->nodes[1]->input_pins[j].default_value;
                        }
                        ctx.budget = budget == 25 ? 1000 : budget;
                        ctx.steps = 0;
                        int actual = !mode ? behavior_function_run(f, &ctx, false)
                                           : entries[mode - 1](&ctx);
                        if (!mode) {
                            status = actual;
                            expected = c;
                            expected_ctx = ctx;
                            memcpy(expected_vars, ctx.variables, sizeof expected_vars);
                            memcpy(expected_results, result, sizeof expected_results);
                            continue;
                        }
                        assert(actual == status && ctx.steps == expected_ctx.steps);
                        assert(
                            c.calls == expected.calls && c.checksum == expected.checksum
                        );
                        assert(acount(c.values) == acount(expected.values));
                        same(c.values, expected.values, acount(c.values));
                        same(ctx.variables, expected_vars, 4);
                        if (!actual) {
                            same(result + 1, expected_results + 1, 4);
                        }
                        if (actual) {
                            assert(
                                ctx.node_id == expected_ctx.node_id &&
                                ctx.error_pin == expected_ctx.error_pin &&
                                ctx.failure == expected_ctx.failure &&
                                ctx.error_stack == expected_ctx.error_stack &&
                                ctx.error_steps == expected_ctx.error_steps
                            );
                            assert(
                                (ctx.error_message != NULL) ==
                                (expected_ctx.error_message != NULL)
                            );
                            if (ctx.error_message) {
                                assert(!strcmp(ctx.error_message, expected_ctx.error_message));
                            }
                        }
                        whole_command_clear(&c);
                    }
                    whole_command_clear(&expected);
                }
            }
            if (kind == WHOLE_COMMAND_LOOP && !bad && count > 0) {
                for (int order = 0; order < 3; order++) {
                    int mode = (order + rotation) % 3;
                    whole_command_capture c = { 0 };
                    for (int j = 0; j < acount(ctx.commands); j++) {
                        ctx.commands[j].service = &c;
                    }
                    ctx.budget = 1000;
                    double start = 0;
                    uint64_t checksum = 0;
                    for (int i = -10000; i < count; i++) {
                        if (!i) {
                            start = seconds();
                        }
                        c.calls = 0;
                        c.checksum = 0;
                        ctx.steps = 0;
                        args[1].d = 2.75 + (i & 7);
                        int status = mode < 2
                                       ? entries[mode](&ctx)
                                       : behavior_function_run(f, &ctx, false);
                        assert(!status && c.calls == 4);
                        if (i >= 0) {
                            checksum += c.checksum;
                        }
                    }
                    printf(
                        "%s command_body ns=%.3f checksum=%llu\n",
                        (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                        (seconds() - start) * 1e9 / count,
                        (unsigned long long)checksum
                    );
                }
            }
            behavior_function_release_host(&ctx);
            behavior_instance_destroy(owner);
            behavior_asset_free(a);
        }
    }
    assert(!dlclose(library));
    puts("PASS: generated-C/SLJIT/bytecode command payloads, errors, stores, loops and source parity");
    return 0;
}
