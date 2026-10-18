#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/status.h"
#include "export_compiler.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Two tiny IR definitions, one generated-C compiler child, seven calls through
 * each of generated C and bytecode. No authored loops, host callbacks or ECS.
 * The existing tool_run child/shared deadlines own the compiler process. */
static behavior_function_builder *builder;
static behavior_function *functions[2];
static FILE *source;
static void *library;
static char directory[128];

static void cleanup()
{
    if (source) {
        fclose(source);
    }
    source = NULL;
    if (library) {
        dlclose(library);
    }
    library = NULL;
    if (builder) {
        behavior_function_builder_free(builder);
    }
    builder = NULL;
    for (int i = 0; i < 2; i++) {
        behavior_function_free(functions[i]);
        functions[i] = NULL;
    }
    if (directory[0]) {
        tool_remove_tree(directory);
    }
    directory[0] = 0;
}
static bool definition(int prepaid)
{
    builder = behavior_function_builder_new();
    pin_value output = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, NULL, 0, &output, 1, NULL, 0);
    int slot = behavior_function_builder_slot(builder, PIN_I32);
    if (prepaid) {
        behavior_function_builder_limits(builder, 2, 0);
    } else {
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_CHECK, .node_id = 31 }
        );
    }
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .output = slot,
                                         .value = { .type = PIN_I32, .i = 7 } }
    );
    if (!prepaid) {
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_CHECK, .node_id = 32 }
        );
    }
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_RESULT, .type = PIN_I32, .left = slot, .target = 0 }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    functions[prepaid] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[prepaid]) {
        fprintf(stderr, "request-work C definition: %s\n", error);
    }
    REQUIRE(functions[prepaid]);
    REQUIRE(acount(functions[prepaid]->instructions) <= 6);
    REQUIRE(functions[prepaid]->root_steps == (prepaid ? 2u : 0u));
    behavior_function_builder_free(builder);
    builder = NULL;
    behavior_function_write_c(functions[prepaid], source, prepaid ? "work_prepaid" : "work_checked");
    return true;
}
TEST_CASE(generated_c_shares_checkpoint_fuel_and_refuses_unchecked_prepaid_bodies)
{
    snprintf(directory, sizeof directory, "/tmp/chibi-request-work-c-XXXXXX");
    char *created = mkdtemp(directory);
    if (!created) {
        directory[0] = 0;
    }
    REQUIRE(created);
    char input[256], output[256], log[256], includes[PATH_MAX];
    snprintf(input, sizeof input, "%s/work.c", directory);
    snprintf(output, sizeof output, "%s/work.so", directory);
    snprintf(log, sizeof log, "%s/compiler.log", directory);
    snprintf(includes, sizeof includes, "%s/src", tool_root());
    source = fopen(input, "w");
    REQUIRE(source && definition(0) && definition(1));
    long bytes = ftell(source);
    REQUIRE(bytes > 0 && bytes < 16 * 1024);
    int closed = fclose(source);
    source = NULL;
    REQUIRE(!closed);
    const char **argv = NULL;
    for (int i = 0; export_compiler[i]; i++) {
        apush(argv, export_compiler[i]);
    }
    const char *arguments[] = { "-std=c11", "-shared", "-fPIC", "-I", includes,
                                input,      "-o",      output,  NULL };
    for (int i = 0; i < (int)(sizeof arguments / sizeof *arguments); i++) {
        apush(argv, arguments[i]);
    }
    int compiled = tool_run(argv, directory, log);
    afree(argv);
    REQUIRE(compiled == 0);
    library = dlopen(output, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "request-work C load: %s\n", dlerror());
    }
    REQUIRE(library);
    typedef int (*entry)(behavior_function_context *);
    entry generated[] = { (entry)dlsym(library, "work_checked"),
                          (entry)dlsym(library, "work_prepaid") };
    REQUIRE(generated[0] && generated[1]);
    for (int backend = 0; backend < 2; backend++) {
        execution_work work = { .limit = 3 };
        execution_work limited = { .limit = 3 };
        for (int sample = 0; sample < 7; sample++) {
            bool prepaid = sample == 4 || sample == 5;
            pin_value result = { .type = PIN_I32, .i = -9 };
            behavior_function_context context = {
                .results = &result,
                .budget = sample == 3 || sample == 6 ? 1 : UINTPTR_MAX,
                .work = sample == 3 || sample == 5 ? NULL
                      : sample == 6                ? &limited
                                                   : &work,
            };
            REQUIRE(behavior_function_bind(functions[prepaid], &context, NULL, 0));
            int status = backend
                           ? generated[prepaid](&context)
                           : behavior_function_run(functions[prepaid], &context, false);
            if (sample == 0 || sample == 5) {
                REQUIRE(status == FLOW_OK && result.i == 7 && context.steps == 2);
            } else if (sample == 4) {
                REQUIRE(status == FLOW_ERR_STATE && result.i == -9);
                REQUIRE(
                    context.failure == BEHAVIOR_FUNCTION_STATE &&
                    context.error_status == FLOW_ERR_STATE
                );
                REQUIRE(context.steps == 0 && context.error_steps == 0);
                REQUIRE(
                    !strcmp(context.error_message, "Request work policy requires canonical execution checkpoints")
                );
            } else {
                REQUIRE(status == FLOW_ERR_STEP_BUDGET && result.i == -9);
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_BUDGET);
                REQUIRE(context.steps == (sample == 2 ? 0u : 1u));
                REQUIRE(context.error_steps == context.steps);
                REQUIRE(context.node_id == (sample == 2 ? 31u : 32u));
            }
            REQUIRE(work.limit == 3 && work.spent == (sample == 0 ? 2u : 3u));
            REQUIRE(limited.limit == 3 && limited.spent == (sample == 6 ? 1u : 0u));
            behavior_function_release_host(&context);
        }
    }
    return true;
}
void test_tool_request_work_c()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(generated_c_shares_checkpoint_fuel_and_refuses_unchecked_prepaid_bodies);
    pu_clear_setup();
}
