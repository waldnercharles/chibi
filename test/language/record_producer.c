#include "execution/module.h"
#include "execution/prepared.h"
#include "language/lower.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>

#define ROW                                                                    \
    "Row :: struct { key: string; label: string; icon: string = \"node\"; "    \
    "prototype: string = \"\"; drop: string = \"\"; depth: int = 1; "          \
    "behaviors: int = 0; active: bool = true; marked: bool = false; "          \
    "hidden: bool = false; locked: bool = false; renaming: bool = false; } "
static const char *const sources[] = {
    "Rows :: library { " ROW
    "Make :: (text: string) -> Row { return {key: text, label: text}; } }",
    "Rows :: library { " ROW
    "MakeRows :: (n: int, empty: Row[]) -> Row[] { if n < 0 { return empty; } "
    "if n == 0 { return []; } a: Row = {key: \"a\", label: \"A\"}; "
    "if n == 1 { return [a]; } return [a, a, a]; } }"
};
static const char *directory;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static unsigned char *bytes;
static FILE *file;
static void cleanup()
{
    if (file) {
        fclose(file);
    }
    file = NULL;
    free(bytes);
    bytes = NULL;
    execution_module_free(module);
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    module = NULL;
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
}
TEST_CASE(produce_two_owned_record_modules)
{
    const script_catalog catalog = { 0 };
    const script_lower_bindings bindings = { 0 };
    for (int i = 0; i < 2; i++) {
        cleanup();
        syntax = script_parse(sources[i]);
        REQUIRE(syntax && !syntax->diagnostic_count);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(analysis && analysis->valid && analysis->function_count == 1);
        plan = script_lower(analysis, &catalog, &bindings);
        REQUIRE(plan && plan->valid);
        char error[256];
        module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(module);
        script_analysis_free(analysis);
        script_syntax_free(syntax);
        analysis = NULL;
        syntax = NULL;
        size_t length = 0;
        REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
        REQUIRE(length > 0 && length <= 1024 * 1024);
        char path[1024];
        int count = snprintf(path, sizeof path, "%s/record-%d.prepared", directory, i);
        REQUIRE(count > 0 && count < (int)sizeof path);
        file = fopen(path, "wb");
        REQUIRE(file && fwrite(bytes, 1, length, file) == length);
        int closed = fclose(file);
        file = NULL;
        REQUIRE(!closed);
    }
    return true;
}
void test_language_record_producer(const char *path)
{
    directory = path;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(produce_two_owned_record_modules);
    pu_clear_setup();
}
