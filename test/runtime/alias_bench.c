#include "test/suite_internal.h"
#include "test/whole_alias_fixture.h"

#include <dlfcn.h>
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
void emit_alias_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int reverse = 0; reverse < 2; reverse++) {
        behavior_asset *a = whole_alias_fixture(
            registry_node_by_name(reverse ? "game_transform_from_cf" : "game_transform_to_cf"),
            true,
            true
        );
        behavior_function_write_c(body(a), out, reverse ? "generated_alias1" : "generated_alias0");
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
int bench_alias(const char *path, int count, int rotation)
{
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library && count > 0);
    for (int reverse = 0; reverse < 2; reverse++) {
        behavior_asset *a = whole_alias_fixture(
            registry_node_by_name(reverse ? "game_transform_from_cf" : "game_transform_to_cf"),
            true,
            true
        );
        const behavior_function *f = body(a);
        int (*entries[])(behavior_function_context *) = {
            (int (*)(behavior_function_context *))behavior_function_entry(f),
            (int (*)(behavior_function_context *))
                dlsym(library, reverse ? "generated_alias1" : "generated_alias0")
        };
        assert(entries[0] && entries[1]);
        float input[] = { -0.f, NAN }, output[2];
        pin_value arg = { .type = PIN_STRUCT,
                          .ptr = input,
                          .type_name = reverse ? "CF_V2" : "GameTransform" };
        for (int missing = 0; missing < 2; missing++) {
            arg.ptr = missing ? NULL : input;
            pin_value result = { .type = PIN_STRUCT, .ptr = output };
            behavior_function_context expected = { .arguments = &arg,
                                                   .argument_count = 1,
                                                   .results = &result };
            int status = behavior_function_run(f, &expected, false);
            for (int mode = 0; mode < 2; mode++) {
                behavior_function_context actual = { .arguments = &arg,
                                                     .argument_count = 1,
                                                     .results = &result };
                assert(entries[mode](&actual) == status);
                assert(
                    actual.node_id == expected.node_id &&
                    actual.error_pin == expected.error_pin &&
                    actual.error_source == expected.error_source &&
                    actual.error_stack == expected.error_stack
                );
                if (status) {
                    assert(!strcmp(actual.error_message, expected.error_message));
                } else {
                    assert(!memcmp(input, output, sizeof input));
                }
            }
        }
        arg.ptr = input;
        if (!reverse) {
            for (int pass = 0; pass < 3; pass++) {
                int mode = (pass + rotation) % 3;
                pin_value result = { .type = PIN_STRUCT, .ptr = output };
                behavior_function_context ctx = { .arguments = &arg,
                                                  .argument_count = 1,
                                                  .results = &result };
                double start = 0, checksum = 0, expected = 0;
                for (int i = -10000; i < count; i++) {
                    if (!i) {
                        start = seconds();
                    }
                    input[0] = (i & 1023) * .125f;
                    input[1] = -input[0];
                    assert(
                        !(mode == 2 ? behavior_function_run(f, &ctx, false)
                                    : entries[mode](&ctx))
                    );
                    if (i >= 0) {
                        checksum += output[0];
                        expected += (i & 1023) * .125f;
                    }
                }
                assert(checksum == expected);
                printf(
                    "%s alias_body ns=%.3f checksum=%.12f\n",
                    (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                    (seconds() - start) * 1e9 / count,
                    checksum
                );
            }
        }
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    puts("alias_parity=SLJIT,BYTECODE,GENERATED_C both-directions,nulls,source-errors");
    return 0;
}
