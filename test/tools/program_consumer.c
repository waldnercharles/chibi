/* Compiles with exported SDK headers and pico_unit only. */
#include "authoring/program.h"
#include "execution/status.h"
#include "runtime/core_definition.h"
#include "third_party/pico_headers/pico_unit.h"

#include <string.h>

static const source_package_snapshot_options *snapshot(const char *text)
{
    static source_package_input file;
    static source_package_snapshot_options options;
    file = (source_package_input){ .path = "/program-consumer/Main.chibi",
                                   .text = text,
                                   .size = strlen(text) };
    options = (source_package_snapshot_options){
        .project_root = "/program-consumer",
        .files = &file,
        .file_count = 1
    };
    return &options;
}

static bool accept(authoring_program *program, const char *text)
{
    return authoring_program_compile(program, snapshot(text)) &&
           authoring_program_prepare(program, "Main", false) &&
           authoring_program_commit(program);
}

TEST_CASE(exported_program_sessions_keep_independent_retained_state)
{
    const native_definition_pack definition = CORE_DEFINITION_PACK;
    char error[256];
    native_capabilities *
        native = native_capabilities_from_definition_packs(NULL, 0, &definition, 1, error, sizeof error);
    REQUIRE(native);
    const authoring_program_options options = {
        .native = native,
        .backend = behavior_default_backend(),
    };
    authoring_program *first = authoring_program_new(&options);
    authoring_program *second = authoring_program_new(&options);
    REQUIRE(first && second && first != second);
    const char *source = "package consumer\nMain :: () -> int { count: retain "
                         "int = 0; count += 1; return count }\n";
    REQUIRE(accept(first, source));
    REQUIRE(accept(second, source));
    int result = 0;
    REQUIRE(authoring_program_call(first, NULL, &result) == FLOW_OK && result == 1);
    REQUIRE(authoring_program_call(second, NULL, &result) == FLOW_OK && result == 1);
    REQUIRE(
        accept(first, "package consumer\nMain :: () -> int { count: retain int = 0; count += 1; return count + 40 }\n")
    );
    REQUIRE(authoring_program_call(first, NULL, &result) == FLOW_OK && result == 42);
    REQUIRE(authoring_program_call(second, NULL, &result) == FLOW_OK && result == 2);
    REQUIRE(
        !authoring_program_compile(first, snapshot("package consumer\nMain :: () { missing() }\n"))
    );
    const authoring_program_diagnostic *diagnostic = authoring_program_error(first);
    REQUIRE(diagnostic && diagnostic->line == 2 && strstr(diagnostic->message, "missing"));
    authoring_program_discard(first);
    REQUIRE(authoring_program_call(first, NULL, &result) == FLOW_OK && result == 43);
    authoring_program_free(first);
    REQUIRE(authoring_program_call(second, NULL, &result) == FLOW_OK && result == 3);
    authoring_program_free(second);
    native_capabilities_free(native);
    return true;
}

TEST_CASE(exported_program_calls_respect_the_host_instruction_budget)
{
    const native_definition_pack definition = CORE_DEFINITION_PACK;
    char error[256];
    native_capabilities *
        native = native_capabilities_from_definition_packs(NULL, 0, &definition, 1, error, sizeof error);
    REQUIRE(native);
    const authoring_program_options options = { .native = native,
                                                .backend = behavior_default_backend() };
    authoring_program *program = authoring_program_new(&options);
    REQUIRE(
        accept(program, "package consumer\nMain :: () -> int {\n count := 0\n while count < 1000 {\n count += 1\n }\n return count\n}\n")
    );
    behavior_execution_scope host = { .remaining = 32, .prefer_compiled = true };
    behavior_execution_scope_begin(&host);
    int result = 0;
    int status = authoring_program_call(program, NULL, &result);
    behavior_execution_scope_end(&host);
    bool exhausted = strstr(
                         authoring_program_error(program)->message
                             ? authoring_program_error(program)->message
                             : "",
                         "shared instruction budget exhausted"
                     ) != NULL;
    authoring_program_free(program);
    native_capabilities_free(native);
    REQUIRE(status == FLOW_ERR_STEP_BUDGET);
    REQUIRE(!host.remaining && exhausted && !behavior_execution_scope_active());
    return true;
}

void test_program_sdk()
{
    RUN_TEST_CASE(exported_program_sessions_keep_independent_retained_state);
    RUN_TEST_CASE(exported_program_calls_respect_the_host_instruction_budget);
}
