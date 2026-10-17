#include "authoring/native_bindings.h"
#include "execution/definition.h"
#include "execution/module.h"
#include "execution/status.h"
#include "export_compiler.h"
#include "language/lower.h"
#include "runtime/arrays.h"
#include "runtime/native_capabilities.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/copy.h"
#include "types/record.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Three source preparations, seven samples per path, one compiler child.
 * Generated C deliberately remains canonical. The one outlined wrapper uses
 * an explicit fixture-side two-argument/one-result invocation trampoline. */
#define ROW                                                                    \
    "Row :: struct { key:string; label:string; icon:string=\"node\"; "         \
    "prototype:string=\"\"; drop:string=\"\"; depth:int=1; behaviors:int=0; "  \
    "active:bool=true; marked:bool=false; hidden:bool=false; "                 \
    "locked:bool=false; renaming:bool=false; } "
#define MAKE_ROW                                                               \
    "row:Row={key:text,label:text,icon:text,prototype:text,drop:text,"         \
    "depth:i+5,behaviors:i+6,active:i%2==0,marked:i%2==1,hidden:i%2==0,"       \
    "locked:i%2==1,renaming:i%2==0}; "
#define LOOP                                                                   \
    "rows:Row[]=[]; i:int=0; while i<count { " MAKE_ROW                        \
    "rows=array_add(rows,row); i=i+1; } "
static const char
    *sources[] = {
        "library Rows { " ROW "Build::(count:int,text:string)->Row[] { " LOOP
        "return rows;} }",
        "library Rows { " ROW "Build::(count:int,text:string)->Row[] { "
        "rows:Row[]=[];prefix:Row[]=[];i:int=0;while i<count { " MAKE_ROW
        "rows=array_add(rows,row);if i==0 "
        "{prefix=rows;}i=i+1;}rows[0].key=\"changed\";return prefix;} }",
        "library Rows { " ROW "Build::(count:int,text:string)->Row[] { " LOOP "return rows;} Wrap::(count:int,text:string)->Row[] {return Build(count,text);} }"
    };
static execution_module *modules[3];
static const behavior_function *functions[3];
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static native_capabilities *native;
static behavior_function_context context, child;
static behavior_function_activation activation, child_activation;
static behavior_run_scope runs;
static pin_copy_scope copies;
static pin_copy_counts measured[2][7];
static arena storage, child_storage, escaped;
static pin_value result, retained[2][7];
static char directory[128];
static FILE *output;
static void *library;
static int c_entries, c_children;
static void finish_child()
{
    behavior_function_release_host(&child);
    behavior_function_activation_free(&child_activation);
    child = (behavior_function_context){ 0 };
    child_activation = (behavior_function_activation){ 0 };
    arena_free(&child_storage);
}
static void finish_call()
{
    if (copies.active) {
        pin_copy_scope_end(&copies);
    }
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    finish_child();
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    arena_free(&storage);
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
    for (int i = 0; i < 3; i++) {
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
    memset(measured, 0, sizeof measured);
    if (directory[0]) {
        tool_remove_tree(directory);
    }
    directory[0] = 0;
    c_entries = c_children = 0;
}
static bool prepare(int index)
{
    const script_catalog *catalog = native_capabilities_catalog(native);
    script_lower_bindings bindings = authoring_native_bindings(native);
    syntax = script_parse(sources[index]);
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "row-builder C semantic: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid && analysis->function_count == (index == 2 ? 2 : 1));
    uint64_t id = 0;
    for (int i = 0; i < analysis->function_count; i++) {
        int symbol = analysis->functions[i].symbol;
        if (symbol >= 0 &&
            !strcmp(analysis->symbols[symbol].name, index == 2 ? "Wrap" : "Build")) {
            id = analysis->symbols[symbol].id;
        }
    }
    REQUIRE(id);
    plan = script_lower(analysis, catalog, &bindings);
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "row-builder C lower: %s\n", plan->diagnostics[i].message);
    }
    REQUIRE(plan && plan->valid);
    char error[256];
    modules[index] = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!modules[index]) {
        fprintf(stderr, "row-builder C module: %s\n", error);
    }
    REQUIRE(modules[index]);
    for (int i = 0; i < execution_module_function_count(modules[index]); i++) {
        execution_module_function_info f = execution_module_function_at(modules[index], i);
        if (f.id == id) {
            functions[index] = f.body;
        }
    }
    const behavior_function *f = functions[index];
    REQUIRE(f && f->result_count == 1);
    REQUIRE(!acount(f->definitions));
    REQUIRE((bool)f->rows == (index == 0));
    if (index == 2) {
        REQUIRE(acount(f->calls) == 1 && f->calls[0].function->rows);
        const behavior_function *callee = f->calls[0].function;
        REQUIRE(callee != f && !acount(callee->definitions));
        REQUIRE(execution_module_function_count(modules[index]) == 2);
        int matches = 0;
        for (int i = 0; i < execution_module_function_count(modules[index]); i++) {
            matches += execution_module_function_at(modules[index], i).body == callee;
        }
        REQUIRE(matches == 1 && callee->argument_count == 2 && callee->result_count == 1);
    }
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    char name[32];
    snprintf(name, sizeof name, "row_builder_%d", index);
    behavior_function_write_c(f, output, name);
    if (index == 2) {
        /* Module siblings are linked, not wrapper-owned definitions. Export
         * the actual call target explicitly for the bounded trampoline. */
        snprintf(name, sizeof name, "row_builder_%d_callee_1", index);
        behavior_function_write_c(f->calls[0].function, output, name);
    }
    return true;
}
static bool check(const pin_value *value, int count, arena *owner)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
    REQUIRE(
        value->record && pin_record_valid(value->record) &&
        !strcmp(value->type_name, "Rows.Row")
    );
    REQUIRE(value->type_name == value->record->type.name && value->record->type.field_count == 12);
    REQUIRE(arena_contains(owner, value->record));
    const pin_array *a = value->ptr;
    REQUIRE(
        a && a->count == count && a->record == value->record &&
        a->type_name == value->type_name
    );
    REQUIRE(arena_contains(owner, a) && (!count || arena_contains(owner, a->items)));
    for (int i = 0; i < count; i++) {
        const pin_value *row = &a->items[i];
        REQUIRE(
            row->type == PIN_STRUCT && row->container == PIN_CONTAINER_SINGLE &&
            row->record == a->record && row->type_name == a->type_name
        );
        REQUIRE(arena_contains(owner, row->ptr));
        for (int j = 0; j < 12; j++) {
            pin_value field;
            pin_read_field(row->ptr, &row->record->type.fields[j], &field);
            if (j < 5) {
                REQUIRE(field.type == PIN_STR && field.str && !strcmp(field.str, "producer"));
                REQUIRE(arena_contains(owner, field.str));
            } else if (j < 7) {
                REQUIRE(field.type == PIN_I32 && field.i == i + j);
            } else {
                REQUIRE(field.type == PIN_BOOL && field.b == (bool)((i + j) % 2));
            }
        }
    }
    return true;
}
typedef int (*entry)(behavior_function_context *);
static bool call_generated(int index, int *status)
{
    char name[64];
    snprintf(name, sizeof name, "row_builder_%d", index);
    entry generated = (entry)dlsym(library, name);
    REQUIRE(generated);
    REQUIRE(!context.source_function);
    c_entries++;
    *status = generated(&context);
    if (index != 2) {
        return true;
    }
    REQUIRE(*status == BEHAVIOR_FUNCTION_CALL_RESULT);
    const behavior_function *f = functions[index];
    REQUIRE(activation.pc > 0 && activation.pc <= acount(f->instructions));
    const behavior_function_instruction *pending = &f->instructions[activation.pc - 1];
    REQUIRE(pending->op == BF_INVOKE && pending->left == 0 && acount(f->calls) == 1);
    const behavior_function_invocation *call = &f->calls[pending->left];
    REQUIRE(call->count == 2 && call->result_count == 1 && !call->task);
    REQUIRE(context.data && context.data->function == f);
    REQUIRE(call->first >= 0 && call->first <= acount(f->data_values) - call->count);
    REQUIRE(call->result_first >= 0 && call->result_first < acount(f->data_values));
    REQUIRE(
        call->function && call->function->argument_count == 2 &&
        call->function->result_count == 1
    );
    REQUIRE(context.data->values[call->first].type == PIN_I32);
    REQUIRE(context.data->values[call->first + 1].type == PIN_STR);
    const pin_value *declared = &call->function->result_defaults[0];
    const pin_value *destination = &context.data->values[call->result_first];
    REQUIRE(
        destination->type == declared->type &&
        destination->container == declared->container
    );
    REQUIRE(pin_record_same(destination->record, declared->record));
    REQUIRE(destination->type_name && !strcmp(destination->type_name, declared->type_name));
    int continuation = activation.pc;
    pin_value args[2], out = context.data->values[call->result_first];
    for (int i = 0; i < 2; i++) {
        REQUIRE(!pin_value_snapshot_to_arena(
            &child_storage,
            &args[i],
            &context.data->values[call->first + i]
        ));
    }
    child = (behavior_function_context){ .parent = &context,
                                         .activation = &child_activation,
                                         .arguments = args,
                                         .argument_count = 2,
                                         .results = &out,
                                         .budget = 10000,
                                         .call_depth = 1 };
    behavior_function_bind_storage(&child, &child_storage);
    behavior_function_prepare_results(call->function, &out, &child_storage);
    REQUIRE(behavior_function_bind(call->function, &child, NULL, 0));
    behavior_function_prepare_host(call->function, &child);
    if (behavior_function_resumable(call->function)) {
        behavior_function_activation_init(call->function, &child_activation);
    }
    REQUIRE(!child.source_function && child.data->function == call->function);
    snprintf(name, sizeof name, "row_builder_%d_callee_1", index);
    entry callee = (entry)dlsym(library, name);
    REQUIRE(callee);
    c_children++;
    int nested = callee(&child);
    REQUIRE(nested == FLOW_OK && child.returned && !child.source_function);
    REQUIRE(
        !pin_value_snapshot_to_arena(&storage, &context.data->values[call->result_first], &out)
    );
    context.returned_outcome = child.returned_outcome;
    finish_child();
    REQUIRE(activation.pc == continuation && !context.source_function);
    c_entries++;
    *status = generated(&context);
    REQUIRE(!context.source_function);
    return true;
}
static bool sample(int backend, int sample)
{
    finish_call();
    int index = sample < 5 ? 0 : sample - 4;
    const int counts[] = { -1, 0, 1, 4, 16, 4, 4 };
    int count = counts[sample];
    const behavior_function *f = functions[index];
    char text[] = "producer";
    pin_value args[] = { { .type = PIN_I32, .i = count },
                         { .type = PIN_STR, .str = text } };
    result = f->result_defaults[0];
    behavior_function_prepare_results(f, &result, &storage);
    context = (behavior_function_context){ .activation = &activation,
                                           .arguments = args,
                                           .argument_count = 2,
                                           .results = &result,
                                           .budget = 10000 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(f, &context, NULL, 0));
    behavior_function_prepare_host(f, &context);
    if (behavior_function_resumable(f)) {
        behavior_function_activation_init(f, &activation);
    }
    REQUIRE(!context.source_function);
    behavior_run_scope_reset(&runs);
    pin_copy_scope_reset(&copies);
    behavior_run_scope_begin(&runs);
    pin_copy_scope_begin(&copies);
    int status;
    if (backend) {
        REQUIRE(call_generated(index, &status));
    } else {
        status = behavior_function_run(f, &context, false);
    }
    pin_copy_scope_end(&copies);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK) {
        fprintf(
            stderr,
            "row-builder C sample %d/%d: %d %s\n",
            backend,
            sample,
            status,
            context.error_message ? context.error_message : ""
        );
    }
    REQUIRE(
        status == FLOW_OK && context.returned && !copies.counts.overflow &&
        !runs.counts.overflow
    );
    REQUIRE(
        !runs.counts.sljit_entries && !runs.counts.wasm_entries && !runs.counts.inspection_bytecode_fallbacks
    );
    REQUIRE(runs.counts.bytecode_entries == (backend ? 0 : index == 2 ? 3 : 1));
    REQUIRE(!backend || !context.source_function);
    measured[backend][sample] = copies.counts;
    if (sample < 5) {
        const uint64_t rows[2][5] = { { 0, 0, 5, 17, 65 }, { 0, 0, 6, 33, 321 } };
        REQUIRE(
            copies.counts.record_bytes ==
            rows[backend][sample] * result.record->type.size
        );
    }
    memset(text, 'x', sizeof text - 1);
    REQUIRE(check(&result, index == 1 ? 1 : count > 0 ? count : 0, &storage));
    REQUIRE(!pin_value_snapshot_to_arena(&escaped, &retained[backend][sample], &result));
    return true;
}
TEST_CASE(counted_rows_use_canonical_generated_c)
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
    snprintf(directory, sizeof directory, "/tmp/chibi-row-builder-c-XXXXXX");
    char *created = mkdtemp(directory);
    if (!created) {
        directory[0] = 0;
    }
    REQUIRE(created);
    char source[256], binary[256], log[256], includes[PATH_MAX], debug[PATH_MAX];
    snprintf(source, sizeof source, "%s/rows.c", directory);
    snprintf(binary, sizeof binary, "%s/rows.so", directory);
    snprintf(log, sizeof log, "%s/compiler.log", directory);
    snprintf(includes, sizeof includes, "%s/src", tool_root());
    snprintf(debug, sizeof debug, "%s/src/execution/source_debug.c", tool_root());
    output = fopen(source, "w");
    REQUIRE(output);
    for (int i = 0; i < 3; i++) {
        REQUIRE(prepare(i));
    }
    long bytes = ftell(output);
    REQUIRE(bytes > 0 && bytes < 512 * 1024);
    int closed = fclose(output);
    output = NULL;
    REQUIRE(!closed);
    native_capabilities_free(native);
    native = NULL;
    REQUIRE(!syntax && !analysis && !type_catalog_find("Rows.Row"));
    const char **argv = NULL;
    for (int i = 0; export_compiler[i]; i++) {
        apush(argv, export_compiler[i]);
    }
    const char *args[] = { "-std=c11", "-shared", "-fPIC", "-I",   includes,
                           source,     debug,     "-o",    binary, NULL };
    for (int i = 0; i < (int)(sizeof args / sizeof *args); i++) {
        apush(argv, args[i]);
    }
    int compiled = tool_run(argv, directory, log);
    afree(argv);
    REQUIRE(!compiled);
    library = dlopen(binary, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "row-builder C load: %s\n", dlerror());
    }
    REQUIRE(library);
    for (int backend = 0; backend < 2; backend++) {
        for (int i = 0; i < 7; i++) {
            REQUIRE(sample(backend, i));
        }
    }
    REQUIRE(c_entries == 8 && c_children == 1);
    /* Same alias work. The emitted eligible callee removes neither triangular
     * term; its caller's unchanged preparation/return work remains included. */
    REQUIRE(!memcmp(&measured[0][5], &measured[1][5], sizeof(pin_copy_counts)));
    size_t size = retained[0][6].record->type.size;
    REQUIRE(measured[1][6].record_bytes == measured[0][6].record_bytes + 16 * size);
    REQUIRE(measured[1][6].text_bytes == measured[0][6].text_bytes + 720);
    REQUIRE(measured[1][6].value_header_moves == measured[0][6].value_header_moves + 20);
    finish_call();
    for (int i = 0; i < 3; i++) {
        execution_module_free(modules[i]);
        modules[i] = NULL;
        functions[i] = NULL;
    }
    const int counts[] = { 0, 0, 1, 4, 16, 1, 4 };
    for (int i = 0; i < 7; i++) {
        REQUIRE(pin_value_equal(&retained[0][i], &retained[1][i]));
        REQUIRE(check(&retained[0][i], counts[i], &escaped));
        REQUIRE(check(&retained[1][i], counts[i], &escaped));
    }
    return true;
}
void test_tool_row_builder_c()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(counted_rows_use_canonical_generated_c);
    pu_clear_setup();
}
