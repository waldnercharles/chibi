#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/status.h"
#include "export_compiler.h"
#include "language/lower.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    ALIAS_FIXTURES = 12,
    FIXTURES = ALIAS_FIXTURES + 1
};
static behavior_function *functions[FIXTURES];
static behavior_function_builder *builder;
static char directory[128];
static FILE *source;
static void *library;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static behavior_function_context pause_context;
static behavior_function_activation pause_activation;
static arena pause_storage;

static void release_pause()
{
    behavior_function_release_host(&pause_context);
    behavior_function_activation_free(&pause_activation);
    arena_free(&pause_storage);
    pause_context = (behavior_function_context){ 0 };
}

static void cleanup()
{
    release_pause();
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
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
    for (int i = 0; i < FIXTURES; i++) {
        behavior_function_free(functions[i]);
        functions[i] = NULL;
    }
    if (directory[0]) {
        tool_remove_tree(directory);
    }
    directory[0] = 0;
}
static pin_value integer(pin_type type, int64_t number)
{
    pin_value value = { .type = type };
    if (type == PIN_I32) {
        value.i = (int32_t)number;
    } else {
        value.i64 = number;
    }
    return value;
}
static int64_t number(pin_value value)
{
    return value.type == PIN_I32 ? value.i : value.i64;
}
static bool fixture(int index)
{
    pin_type type = index < 6 ? PIN_I32 : PIN_I64;
    bool modulo = index % 6 >= 3;
    int alias = index % 3;
    pin_value values[] = { { .type = type }, { .type = type } };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, values, 2, values, 2, NULL, 0);
    int operands[2];
    for (int i = 0; i < 2; i++) {
        operands[i] = behavior_function_builder_slot(builder, type);
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_ARGUMENT,
                                             .type = type,
                                             .output = operands[i],
                                             .left = i,
                                             .value = values[i] }
        );
    }
    int output = alias == 2 ? behavior_function_builder_slot(builder, type)
                            : operands[1 - alias];
    int secondary = alias == 2 ? output : operands[alias];
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = modulo ? BF_MOD : BF_DIV,
                                         .type = type,
                                         .output = output,
                                         .left = operands[0],
                                         .right = operands[1],
                                         .target = secondary }
    );
    int results[] = { output, secondary };
    for (int i = 0; i < 2; i++) {
        behavior_function_builder_emit(
            builder,
            (
                behavior_function_instruction
            ){ .op = BF_RESULT, .type = type, .left = results[i], .target = i }
        );
    }
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    functions[index] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[index]) {
        fprintf(stderr, "C export fixture: %s\n", error);
    }
    REQUIRE(functions[index]);
    behavior_function_builder_free(builder);
    builder = NULL;
    char name[40];
    snprintf(name, sizeof name, "export_alias_%d", index);
    behavior_function_write_c(functions[index], source, name);
    return true;
}
static int inspect(void *user, const behavior_function_point *point)
{
    (void)user;
    (void)point;
    return 0;
}

static bool pause_fixture()
{
    const script_catalog catalog = { 0 };
    const script_lower_bindings bindings = { 0 };
    syntax = script_parse(
        "library Scalar {\n"
        "Eval :: pure (amount: number) -> number {\n"
        "value: number | Cancelled = amount;\n"
        "if value is number found { return found; }\n"
        "return 0; } }"
    );
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis && analysis->valid);
    plan = script_lower(analysis, &catalog, &bindings);
    REQUIRE(plan && plan->valid);
    for (int i = 0; i < plan->function_count; i++) {
        behavior_function *body = plan->functions[i].body;
        if (body && body->argument_count == 1 && body->result_count == 1) {
            functions[ALIAS_FIXTURES] = body;
            plan->functions[i].body = NULL;
            break;
        }
    }
    REQUIRE(functions[ALIAS_FIXTURES]);
    behavior_function_write_c(functions[ALIAS_FIXTURES], source, "export_pause");
    return true;
}

static int pause_line(void *, behavior_function_context *, const behavior_source_point *point)
{
    return point->line == 4 && point->kind == BEHAVIOR_SOURCE_STATEMENT;
}

static bool resumes_exported_pause()
{
    typedef int (*entry)(behavior_function_context *);
    entry generated = (entry)dlsym(library, "export_pause");
    REQUIRE(generated);
    behavior_function *f = functions[ALIAS_FIXTURES];
    const double samples[] = { 3.75, -0.0 };
    for (int i = 0; i < 2; i++) {
        release_pause();
        pin_value input = { .type = PIN_F64, .d = samples[i] };
        pin_value output = { .type = PIN_F64, .d = 99 };
        behavior_function_activation_init(f, &pause_activation);
        pause_context = (behavior_function_context){
            .activation = &pause_activation,
            .arguments = &input,
            .argument_count = 1,
            .results = &output,
            .budget = 100,
            .source_pause = pause_line,
            .source_function = f,
            .source_points = f->source_points,
            .source_watches = f->source_watches,
        };
        behavior_function_bind_storage(&pause_context, &pause_storage);
        REQUIRE(behavior_function_bind(f, &pause_context, NULL, 0));
        behavior_function_prepare_host(f, &pause_context);
        REQUIRE(generated(&pause_context) == FLOW_PAUSED_RESULT);
        pause_context.source_pause = NULL;
        REQUIRE(generated(&pause_context) == FLOW_OK);
        REQUIRE(!memcmp(&output.d, &input.d, sizeof output.d));
    }
    return true;
}

TEST_CASE(generated_c_preserves_aliased_operands_and_debug_continuations)
{
    snprintf(directory, sizeof directory, "/tmp/chibi-c-export-XXXXXX");
    char *created = mkdtemp(directory);
    if (!created) {
        directory[0] = 0;
    }
    REQUIRE(created);
    char input[256], output[256], log[256], includes[PATH_MAX], debug[PATH_MAX];
    snprintf(input, sizeof input, "%s/aliases.c", directory);
    snprintf(output, sizeof output, "%s/aliases.so", directory);
    snprintf(log, sizeof log, "%s/compiler.log", directory);
    snprintf(includes, sizeof includes, "%s/src", tool_root());
    snprintf(debug, sizeof debug, "%s/src/execution/source_debug.c", tool_root());
    source = fopen(input, "w");
    REQUIRE(source);
    for (int i = 0; i < ALIAS_FIXTURES; i++) {
        REQUIRE(fixture(i));
    }
    REQUIRE(pause_fixture());
    long bytes = ftell(source);
    REQUIRE(bytes > 0 && bytes < 128 * 1024);
    int closed = fclose(source);
    source = NULL;
    REQUIRE(!closed);
    const char **argv = NULL;
    for (int i = 0; export_compiler[i]; i++) {
        apush(argv, export_compiler[i]);
    }
    const char *arguments[] = { "-std=c11", "-shared", "-fPIC", "-I",
                                includes,   input,     debug,   "-o",
                                output,     NULL };
    for (int i = 0; i < (int)(sizeof arguments / sizeof *arguments); i++) {
        apush(argv, arguments[i]);
    }
    int compiled = tool_run(argv, directory, log);
    afree(argv);
    REQUIRE(compiled == 0);
    library = dlopen(output, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "C export load: %s\n", dlerror());
    }
    REQUIRE(library);
    for (int i = 0; i < ALIAS_FIXTURES; i++) {
        char name[40];
        snprintf(name, sizeof name, "export_alias_%d", i);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        pin_type type = i < 6 ? PIN_I32 : PIN_I64;
        int64_t minimum = type == PIN_I32 ? INT32_MIN : INT64_MIN;
        const int64_t samples[][2] = { { 10, 3 },
                                       { -10, 3 },
                                       { 10, -3 },
                                       { minimum, -1 },
                                       { 1, 0 } };
        for (int sample = 0; sample < 5; sample++) {
            for (int inspection = 0; inspection < 2; inspection++) {
                pin_value inputs[] = { integer(type, samples[sample][0]),
                                       integer(type, samples[sample][1]) };
                pin_value actual[] = { integer(type, 77), integer(type, 88) };
                pin_value expected[] = { integer(type, 77), integer(type, 88) };
                behavior_function_context vm = { .arguments = inputs,
                                                 .argument_count = 2,
                                                 .results = expected,
                                                 .budget = 32,
                                                 .inspect = inspect };
                behavior_function_context compiled_context = {
                    .arguments = inputs,
                    .argument_count = 2,
                    .results = actual,
                    .budget = 32,
                    .inspect = inspection ? inspect : NULL
                };
                REQUIRE(behavior_function_bind(functions[i], &vm, NULL, 0));
                int reference = behavior_function_run(functions[i], &vm, false);
                int status = generated(&compiled_context);
                REQUIRE(status == reference);
                REQUIRE(status == (sample >= 3 ? FLOW_ERR_INVALID_GRAPH : FLOW_OK));
                REQUIRE(number(actual[0]) == number(expected[0]));
                REQUIRE(number(actual[1]) == number(expected[1]));
                if (sample >= 3) {
                    REQUIRE(number(actual[0]) == 77 && number(actual[1]) == 88);
                    REQUIRE(compiled_context.failure == BEHAVIOR_FUNCTION_DATA);
                } else {
                    int64_t quotient = samples[sample][0] / samples[sample][1];
                    int64_t remainder = samples[sample][0] % samples[sample][1];
                    bool modulo = i % 6 >= 3;
                    int64_t primary = modulo ? remainder : quotient;
                    int64_t secondary = i % 3 == 2
                                          ? primary
                                          : (modulo ? quotient : remainder);
                    REQUIRE(number(actual[0]) == primary);
                    REQUIRE(number(actual[1]) == secondary);
                }
            }
        }
    }
    REQUIRE(resumes_exported_pause());
    return true;
}
void test_tool_c_export()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(generated_c_preserves_aliased_operands_and_debug_continuations);
    pu_clear_setup();
}
