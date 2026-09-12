#include "graph/compiler/behavior_function_internal.h"
#include "test/suite_internal.h"
#include "test/whole_loop_fixture.h"
#include "test/whole_suspend_fixture.h"

#include <dlfcn.h>
#include <time.h>

static double seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_suspend(const char *path, int count, int rotation)
{
    test_generated_suspensions(path);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    behavior_asset *a = whole_loop_fixture(WHOLE_LOOP_NESTED_CALL);
    graph *g = a->graphs[1];
    behavior_function_diagnostic diagnostic;
    behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
    assert(f);
    whole_suspend_instrument(f, false);
    assert(behavior_function_compile(f));
    char name[40];
    snprintf(name, sizeof name, "generated_suspend%d_0", WHOLE_LOOP_NESTED_CALL);
    int (*entries[])(behavior_function_context *) = {
        (int (*)(behavior_function_context *))behavior_function_entry(f),
        (int (*)(behavior_function_context *))dlsym(library, name)
    };
    assert(entries[1]);
    behavior_function_activation activation = { 0 };
    behavior_function_activation_init(f, &activation);
    pin_value variables[] = { a->variables[0].default_value,
                              a->variables[1].default_value };
    pin_value args[] = { { .type = PIN_I32, .i = 1 }, { .type = PIN_I32, .i = 4 } };
    pin_value result[2] = { 0 };
    behavior_function_context ctx = { .activation = &activation,
                                      .arguments = args,
                                      .argument_count = 2,
                                      .variables = variables,
                                      .results = result,
                                      .budget = 1000 };
    for (int order = 0; order < 3; order++) {
        int mode = (order + rotation) % 3;
        double start = 0;
        uint64_t checksum = 0, resumes = 0;
        for (int iteration = -10000; iteration < count; iteration++) {
            if (!iteration) {
                start = seconds();
            }
            ctx.steps = 0;
            variables[0].i = 0;
            args[1].i = 2 + (iteration & 3);
            int status, calls = 0;
            do {
                status = mode < 2 ? entries[mode](&ctx)
                                  : behavior_function_run(f, &ctx, false);
                calls++;
            } while (status == FLOW_SUSPENDED_RESULT);
            assert(!status && variables[0].i == args[1].i * 2);
            if (iteration >= 0) {
                checksum += variables[0].i;
                resumes += calls;
            }
        }
        printf(
            "%s suspend_body ns=%.3f checksum=%llu calls=%llu\n",
            (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
            (seconds() - start) * 1e9 / count,
            (unsigned long long)checksum,
            (unsigned long long)resumes
        );
    }
    behavior_function_activation_free(&activation);
    behavior_function_free(f);
    behavior_asset_free(a);
    assert(!dlclose(library));
    return 0;
}
