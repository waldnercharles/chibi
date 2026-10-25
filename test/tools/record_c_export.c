#include "authoring/native_bindings.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/status.h"
#include "export_compiler.h"
#include "language/lower.h"
#include "runtime/arrays_definition.h"
#include "runtime/native_capabilities.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Three functions, eight samples per backend, one compiler child. No frames.
 * The third builds its rows with in-place local array updates. */
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
    "if n == 1 { return [a]; } return [a, a, a]; } }",
    "Rows :: library { " ROW
    "Grow :: (n: int) -> Row[] { rows: Row[] = []; i: int = 0; while i < n { "
    "row: Row = {key: \"a\", label: \"A\"}; rows = append(rows, row); i = i "
    "+ 1; } "
    "grown: Row[] = []; grown = extend(grown, rows); "
    "if n > 1 { grown[n - 1] = {key: \"z\", label: \"Z\"}; } return grown; } }"
};
enum
{
    FUNCTIONS = 3,
    SAMPLES = 8
};
static execution_module *modules[FUNCTIONS];
static const behavior_function *functions[FUNCTIONS];
static native_capabilities *native;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static behavior_function_context context;
static behavior_function_activation activation;
static behavior_run_scope runs;
static arena storage, escaped;
static pin_value result, retained[2][SAMPLES];
static char directory[128];
static FILE *output;
static void *library;
static void finish_call()
{
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
}
static void cleanup()
{
    finish_call();
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    execution_module_plan_free(plan);
    analysis = NULL;
    syntax = NULL;
    plan = NULL;
    for (int i = 0; i < FUNCTIONS; i++) {
        execution_module_free(modules[i]);
        modules[i] = NULL;
        functions[i] = NULL;
    }
    native_capabilities_free(native);
    native = NULL;
    if (output) {
        fclose(output);
    }
    output = NULL;
    if (library) {
        dlclose(library);
    }
    library = NULL;
    arena_free(&escaped);
    memset(retained, 0, sizeof retained);
    if (directory[0]) {
        tool_remove_tree(directory);
    }
    directory[0] = 0;
}
static bool in_place(const behavior_function *f, const node_host_call *call)
{
    for (int i = 0; i < acount(f->host_calls); i++) {
        if (f->host_calls[i].capability_id == execution_intrinsic_id(call)) {
            return true;
        }
    }
    return false;
}
static bool prepare(int index)
{
    const script_catalog empty = { 0 };
    const script_catalog *catalog = index == 2 ? native_capabilities_catalog(native)
                                               : &empty;
    const script_lower_bindings bindings = index == 2
                                             ? authoring_native_bindings(native)
                                             : (script_lower_bindings){ 0 };
    syntax = script_parse(sources[index]);
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, catalog);
    REQUIRE(analysis && analysis->valid && analysis->function_count == 1);
    uint64_t id = analysis->symbols[analysis->functions[0].symbol].id;
    plan = script_lower(analysis, catalog, &bindings);
    REQUIRE(plan && plan->valid);
    char error[256];
    modules[index] = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!modules[index]) {
        fprintf(stderr, "record C export preparation: %s\n", error);
    }
    REQUIRE(modules[index]);
    for (int i = 0; i < execution_module_function_count(modules[index]); i++) {
        execution_module_function_info function = execution_module_function_at(modules[index], i);
        if (function.id == id) {
            functions[index] = function.body;
        }
    }
    REQUIRE(functions[index] && functions[index]->result_count == 1);
    if (index == 2) {
        REQUIRE(in_place(functions[index], &EXECUTION_array_push_call));
        REQUIRE(in_place(functions[index], &EXECUTION_array_extend_call));
        REQUIRE(in_place(functions[index], &EXECUTION_array_store_call));
    }
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
    char name[32];
    snprintf(name, sizeof name, "record_export_%d", index);
    behavior_function_write_c(functions[index], output, name);
    return true;
}
static pin_value member(const pin_value *row, int field)
{
    pin_value value;
    {
        const char *error = pin_read_field(
            &storage,
            row->ptr,
            &row->record->type.fields[field],
            &value
        );
        if (error) {
            abort();
        }
    };
    return value;
}
static bool row(const pin_value *value, const char *key, const char *label)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE);
    REQUIRE(pin_record_valid(value->record) && value->record->type.field_count == 12);
    REQUIRE(!strcmp(value->type_name, "Rows.Row") && value->ptr);
    REQUIRE(!strcmp(pin_text(member(value, 0)).bytes, key));
    REQUIRE(!strcmp(pin_text(member(value, 1)).bytes, label));
    REQUIRE(!strcmp(pin_text(member(value, 2)).bytes, "node"));
    REQUIRE(
        !strcmp(pin_text(member(value, 3)).bytes, "") &&
        !strcmp(pin_text(member(value, 4)).bytes, "")
    );
    REQUIRE(member(value, 5).i == 1 && member(value, 6).i == 0);
    REQUIRE(member(value, 7).b);
    for (int i = 8; i < 12; i++) {
        REQUIRE(!member(value, i).b);
    }
    return true;
}
static bool owned_row(const pin_value *value)
{
    REQUIRE(arena_contains(&storage, value->ptr));
    for (int field = 0; field < 5; field++) {
        const char *text = pin_text(member(value, field)).bytes;
        REQUIRE(text && arena_contains(&storage, text));
    }
    return true;
}
static bool sample(int backend, int index)
{
    finish_call();
    int function = index == 0 ? 0 : index < 5 ? 1 : 2;
    const behavior_function *f = functions[function];
    const int counts[] = { -1, 0, 1, 3, 0, 1, 20 };
    char text[] = "export-owner";
    pin_value args[2] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = text, .length = (uint32_t)strlen(text) } },
        f->result_defaults[0]
    };
    if (function) {
        args[0] = (pin_value){ .type = PIN_I32, .i = counts[index - 1] };
        args[1].ptr = NULL;
    }
    int argument_count = function == 1 ? 2 : 1;
    result = f->result_defaults[0];
    behavior_function_prepare_results(f, &result, &storage);
    context = (behavior_function_context){ .activation = &activation,
                                           .arguments = args,
                                           .argument_count = argument_count,
                                           .results = &result,
                                           .budget = 10000 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(f, &context, NULL, 0));
    behavior_function_prepare_host(f, &context);
    REQUIRE(!context.source_function);
    behavior_run_scope_reset(&runs);
    behavior_run_scope_begin(&runs);
    int status;
    if (backend) {
        char name[32];
        snprintf(name, sizeof name, "record_export_%d", function);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        status = generated(&context);
    } else {
        status = behavior_function_run(f, &context, false);
    }
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK) {
        fprintf(
            stderr,
            "record C export run: %d %s\n",
            status,
            context.error_message ? context.error_message : ""
        );
    }
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(!runs.counts.sljit_entries && !runs.counts.wasm_entries);
    REQUIRE(backend ? !runs.counts.bytecode_entries : runs.counts.bytecode_entries > 0);
    REQUIRE(!backend || !context.source_function);
    REQUIRE(result.record && arena_contains(&storage, result.record));
    memset(text, 'x', sizeof text - 1);
    if (!function) {
        REQUIRE(row(&result, "export-owner", "export-owner"));
        REQUIRE(owned_row(&result));
    } else {
        REQUIRE(result.type == PIN_STRUCT && result.container == PIN_CONTAINER_ARRAY);
        REQUIRE(!strcmp(result.type_name, "Rows.Row"));
        const pin_array *array = result.ptr;
        if (index == 1) {
            REQUIRE(!array);
        } else {
            int count = counts[index - 1];
            REQUIRE(array && array->count == count);
            REQUIRE(array->record == result.record);
            REQUIRE(arena_contains(&storage, array->record));
            for (int i = 0; i < array->count; i++) {
                bool last = function == 2 && count > 1 && i == count - 1;
                pin_value item = pin_array_get(array, i);
                REQUIRE(item.record == array->record);
                REQUIRE(row(&item, last ? "z" : "a", last ? "Z" : "A"));
                REQUIRE(owned_row(&item));
            }
        }
    }
    REQUIRE(!pin_value_snapshot_to_arena(&escaped, &retained[backend][index], &result));
    return true;
}
TEST_CASE(authored_record_headers_survive_exported_c)
{
    REQUIRE(!type_catalog_find("Rows.Row"));
    char error[256];
    native = native_capabilities_from_definition_packs(
        NULL,
        0,
        &NATIVE_ARRAY_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
    REQUIRE(native);
    snprintf(directory, sizeof directory, "/tmp/chibi-record-c-XXXXXX");
    char *created = mkdtemp(directory);
    if (!created) {
        directory[0] = 0;
    }
    REQUIRE(created);
    char source[256], binary[256], log[256], includes[PATH_MAX], debug[PATH_MAX];
    snprintf(source, sizeof source, "%s/records.c", directory);
    snprintf(binary, sizeof binary, "%s/records.so", directory);
    snprintf(log, sizeof log, "%s/compiler.log", directory);
    snprintf(includes, sizeof includes, "%s/src", tool_root());
    snprintf(debug, sizeof debug, "%s/src/execution/source_debug.c", tool_root());
    output = fopen(source, "w");
    REQUIRE(output);
    REQUIRE(prepare(0) && prepare(1) && prepare(2));
    long bytes = ftell(output);
    /* The row loop is about 190 KiB of the roughly 220 KiB of source. */
    REQUIRE(bytes > 0 && bytes < 512 * 1024);
    int closed = fclose(output);
    output = NULL;
    REQUIRE(!closed && !syntax && !analysis && !type_catalog_find("Rows.Row"));
    const char **argv = NULL;
    for (int i = 0; export_compiler[i]; i++) {
        apush(argv, export_compiler[i]);
    }
    const char *arguments[] = { "-std=c11", "-shared", "-fPIC", "-I",
                                includes,   source,    debug,   "-o",
                                binary,     NULL };
    for (int i = 0; i < (int)(sizeof arguments / sizeof *arguments); i++) {
        apush(argv, arguments[i]);
    }
    int compiled = tool_run(argv, directory, log);
    afree(argv);
    REQUIRE(!compiled);
    library = dlopen(binary, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "record C export load: %s\n", dlerror());
    }
    REQUIRE(library);
    for (int backend = 0; backend < 2; backend++) {
        for (int i = 0; i < SAMPLES; i++) {
            REQUIRE(sample(backend, i));
        }
    }
    finish_call();
    for (int i = 0; i < FUNCTIONS; i++) {
        execution_module_free(modules[i]);
        modules[i] = NULL;
        functions[i] = NULL;
    }
    for (int i = 0; i < SAMPLES; i++) {
        REQUIRE(pin_record_valid(retained[0][i].record));
        REQUIRE(pin_record_valid(retained[1][i].record));
        const pin_value *left = &retained[0][i], *right = &retained[1][i];
        bool equal = i == 1 ? !left->ptr && !right->ptr && left->type == right->type &&
                                  left->container == right->container &&
                                  !strcmp(left->type_name, right->type_name) &&
                                  pin_record_same(left->record, right->record)
                            : pin_value_equal(left, right);
        if (!equal) {
            fprintf(stderr, "record C export comparison sample %d\n", i);
        }
        REQUIRE(equal);
    }
    REQUIRE(row(&retained[1][0], "export-owner", "export-owner"));
    REQUIRE(!retained[1][1].ptr && retained[1][1].record);
    REQUIRE(((pin_array *)retained[1][4].ptr)->count == 3);
    REQUIRE(((pin_array *)retained[1][7].ptr)->count == 20);
    return true;
}
void test_tool_record_c_export()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(authored_record_headers_survive_exported_c);
    pu_clear_setup();
}
