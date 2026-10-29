#include "backend.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/managed.h"
#include "execution/module.h"
#include "execution/status.h"
#include "execution/wasm_jit.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Record values, escaped layouts and native metadata lifetime. */
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static behavior_function_builder *builder;
static uint64_t function_id;
static behavior_function_context context;
static behavior_function_context reference_owner;
static behavior_managed_heap heap;
static behavior_function_activation activation;
static behavior_run_scope runs;
static arena storage, escaped;
static pin_value result, saved;
static const script_catalog catalog;
static const script_lower_bindings bindings;
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
#define ROW_FIELDS                                                             \
    "key: string; label: string; icon: string = \"node\"; "                    \
    "prototype: string = \"\"; drop: string = \"\"; depth: int = 1; "          \
    "behaviors: int = 0; active: bool = true; marked: bool = false; "          \
    "hidden: bool = false; locked: bool = false; renaming: bool = false;"
#define ROW "Row :: struct { " ROW_FIELDS " } "
static void finish_call()
{
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    arena_free(&storage);
    result = (pin_value){ 0 };
}
static void cleanup()
{
    finish_call();
    behavior_function_release_host(&reference_owner);
    reference_owner = (behavior_function_context){ 0 };
    execution_module_free(module);
    if (builder) {
        behavior_function_builder_free(builder);
        builder = NULL;
    }
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    arena_free(&escaped);
    behavior_managed_heap_free(&heap);
    module = NULL;
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
    saved = (pin_value){ 0 };
}
static bool compile(const char *source, behavior_backend backend, bool reject_types)
{
    syntax = script_parse(source);
    for (int i = 0; i < syntax->diagnostic_count; i++) {
        const script_diagnostic *d = &syntax->diagnostics[i];
        fprintf(
            stderr,
            "record parse %d:%d [%d,%d): %s: %.64s\n",
            d->range.line,
            d->range.column,
            d->range.start,
            d->range.end,
            d->message,
            source + d->range.start
        );
    }
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "record semantic: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid);
    REQUIRE(analysis->function_count >= 1 && analysis->functions[0].symbol >= 0);
    function_id = analysis->symbols[analysis->functions[0].symbol].id;
    plan = script_lower(analysis, &catalog, &bindings);
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "record lower: %s\n", plan->diagnostics[i].message);
    }
    REQUIRE(plan && plan->valid);
    char error[256];
    if (reject_types) {
        REQUIRE(plan->type_count == 1 && plan->types[0].record);
        execution_type_requirement original = plan->types[0];
        for (int i = 0; i < 4; i++) {
            plan->types[0] = original;
            if (i == 0) {
                plan->types[0].id++;
            }
            if (i == 1) {
                plan->types[0].layout++;
            }
            if (i == 2) {
                plan->types[0].size++;
            }
            if (i == 3) {
                plan->types[0].name = "Rows.Other";
            }
            REQUIRE(!execution_module_finish(&plan, backend, error, sizeof error));
            REQUIRE(plan && strstr(error, "authored type requirement"));
        }
        plan->types[0] = original;
    }
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "record publish: %s\n", error);
    }
    REQUIRE(module && execution_module_backend(module) == backend);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
    return true;
}
static const behavior_function *body()
{
    const behavior_function *function = NULL;
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info f = execution_module_function_at(module, i);
        if (f.id == function_id) {
            function = f.body;
        }
    }
    return function;
}
static bool run(behavior_backend backend, const pin_value *args, int count)
{
    finish_call();
    if (!heap.owner) {
        behavior_managed_heap_init(&heap);
    }
    const behavior_function *function = body();
    REQUIRE(function && function->result_count == 1);
    const pin_record *record = NULL;
    for (int i = 0; i < function->argument_count; i++) {
        if (function->argument_defaults[i].record) {
            record = function->argument_defaults[i].record;
        }
    }
    if (function->result_defaults[0].record) {
        if (record) {
            REQUIRE(record == function->result_defaults[0].record);
        }
        record = function->result_defaults[0].record;
    }
    for (int i = 0; i < acount(function->data_values); i++) {
        const pin_record *shape = function->data_values[i].initial.record;
        if (shape) {
            REQUIRE(pin_record_valid(shape) && arena_contains(&function->owned, shape));
            REQUIRE(!record || shape == record);
            record = shape;
        }
    }
    result = function->result_defaults[0];
    behavior_function_prepare_results(function, &result, &storage);
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    context = (behavior_function_context){ .activation = &activation,
                                           .arguments = args,
                                           .argument_count = count,
                                           .results = &result,
                                           .managed_heap = &heap,
                                           .budget = 10000 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope_reset(&runs);
    execution_wasm_jit_stats before, after;
    execution_wasm_jit_get_stats(&before);
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &context, backend != BEHAVIOR_BYTECODE);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK) {
        fprintf(
            stderr,
            "record run: %d %s\n",
            status,
            context.error_message ? context.error_message : ""
        );
    }
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(
        backend == BEHAVIOR_BYTECODE
            ? runs.counts.bytecode_entries > 0 && !runs.counts.sljit_entries &&
                  !runs.counts.wasm_entries
        : backend == BEHAVIOR_SLJIT
            ? runs.counts.sljit_entries > 0 && !runs.counts.bytecode_entries &&
                  !runs.counts.wasm_entries
            : runs.counts.wasm_entries > 0 && !runs.counts.bytecode_entries &&
                  !runs.counts.sljit_entries
    );
    execution_wasm_jit_get_stats(&after);
    REQUIRE(
        backend != BEHAVIOR_WASM || (after.executions > before.executions &&
                                     after.compile_failures == before.compile_failures)
    );
    return true;
}
static pin_value member(const pin_value *row, int index)
{
    pin_value field;
    {
        const char *error = pin_read_field(
            &storage,
            row->ptr,
            &row->record->type.fields[index],
            &field
        );
        if (error) {
            abort();
        }
    };
    return field;
}
static pin_value array_member(const pin_array *array, int row, int field)
{
    pin_value item = pin_array_get(array, row);
    return member(&item, field);
}
TEST_CASE(record_runtime_owned_return_and_value_copy)
{
    static const char source[] = "Rows :: library { " ROW
                                 "Make :: (text: string) -> Row { a: Row = "
                                 "{key: text, label: text}; "
                                 "b := a; b.label = \"changed\"; b.depth = 4; "
                                 "if a.label != text or a.depth != 1 { b.depth "
                                 "= -100; } return b; } }";
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(source, backends[b], b == 0));
        char producer[] = "producer";
        pin_value arg = { .type = PIN_STR,
                          .text = &(const pin_string){
                              .bytes = producer,
                              .length = (uint32_t)strlen(producer) } };
        REQUIRE(run(backends[b], &arg, 1));
        REQUIRE(result.record && pin_record_valid(result.record) && result.ptr);
        REQUIRE(arena_contains(&storage, result.record));
        REQUIRE(!strcmp(pin_text(member(&result, 0)).bytes, "producer"));
        REQUIRE(!strcmp(pin_text(member(&result, 1)).bytes, "changed"));
        REQUIRE(!strcmp(pin_text(member(&result, 2)).bytes, "node"));
        REQUIRE(member(&result, 5).i == 4 && member(&result, 7).b);
        memset(producer, 'x', sizeof producer - 1);
        REQUIRE(!strcmp(pin_text(member(&result, 0)).bytes, "producer"));
        REQUIRE(!pin_value_snapshot_to_arena(&escaped, &saved, &result));
        finish_call();
        execution_module_free(module);
        module = NULL;
        REQUIRE(pin_record_valid(saved.record));
        REQUIRE(!strcmp(pin_text(member(&saved, 0)).bytes, "producer"));
        REQUIRE(!strcmp(pin_text(member(&saved, 1)).bytes, "changed"));
        REQUIRE(member(&saved, 5).i == 4);
        pin_value operands[2] = { saved, saved };
        pin_value output = { .type = PIN_I32, .i = 73 }, before = output;
        REQUIRE(EXECUTION_value_equal_call.value(&escaped, operands, 1, &output));
        REQUIRE(!memcmp(&output, &before, sizeof output));
        const pin_record *other = pin_record_create(
            &escaped,
            saved.type_name,
            saved.record->id + 1,
            saved.record->type.fields,
            saved.record->type.field_count
        );
        REQUIRE(other);
        if (b == 0) {
            pin_record malformed = *saved.record;
            malformed.type.size++;
            pin_value bad = saved;
            bad.ptr = NULL;
            bad.record = &malformed;
            builder = behavior_function_builder_new();
            behavior_function_builder_signature(builder, NULL, 0, &bad, 1, NULL, 0);
            behavior_function_builder_values(builder, &bad, 1);
            behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
            char error[192];
            REQUIRE(!behavior_function_builder_finish(builder, error, sizeof error));
            REQUIRE(strstr(error, "invalid signature") || strstr(error, "invalid host"));
            behavior_function_builder_free(builder);
            builder = NULL;
        }
        operands[1].record = other;
        operands[1].type_name = other->type.name;
        REQUIRE(EXECUTION_value_equal_call.value(&escaped, operands, 2, &output));
        REQUIRE(!memcmp(&output, &before, sizeof output));
    }
    return true;
}
TEST_CASE(record_runtime_array_results)
{
    static const char source
        [] = "Rows :: library { " ROW
             "MakeRows :: (n: int, empty: Row[]) -> Row[] { if n < 0 { return "
             "empty; } if n == 0 { return []; } "
             "a: Row = {key: \"a\", label: \"A\"}; if n == 1 { return [a]; } "
             "return [a, {key: \"b\", label: \"B\"}, {key: \"c\", label: "
             "\"C\"}]; } }";
    const int counts[] = { -1, 0, 1, 3 };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(source, backends[b], false));
        pin_value empty_saved = { 0 };
        REQUIRE(body());
        pin_value args[2] = { { .type = PIN_I32 }, body()->result_defaults[0] };
        args[1].ptr = NULL;
        for (int i = 0; i < 4; i++) {
            args[0].i = counts[i];
            REQUIRE(run(backends[b], args, 2));
            REQUIRE(result.record && pin_record_valid(result.record));
            REQUIRE(arena_contains(&heap.storage, result.record));
            REQUIRE(result.type == PIN_STRUCT && result.container == PIN_CONTAINER_ARRAY);
            pin_array *array = result.ptr;
            REQUIRE(array && array->storage == &heap.storage);
            if (counts[i] < 0) {
                REQUIRE(array->count == 0);
                REQUIRE(!pin_value_capture_to_arena(&escaped, &empty_saved, &result));
                continue;
            }
            REQUIRE(array->count == counts[i]);
            REQUIRE(result.record == array->record);
            REQUIRE(arena_contains(&heap.storage, array->record));
            for (int j = 0; j < array->count; j++) {
                REQUIRE(pin_array_get(array, j).record == array->record);
                pin_value item = pin_array_get(array, j);
                REQUIRE(member(&item, 5).i == 1);
            }
        }
        REQUIRE(!pin_value_capture_to_arena(&escaped, &saved, &result));
        finish_call();
        behavior_managed_heap_free(&heap);
        execution_module_free(module);
        module = NULL;
        pin_array *array = saved.ptr;
        REQUIRE(empty_saved.record && pin_record_valid(empty_saved.record));
        REQUIRE(
            empty_saved.ptr && empty_saved.type == PIN_STRUCT &&
            empty_saved.container == PIN_CONTAINER_ARRAY
        );
        REQUIRE(((pin_array *)empty_saved.ptr)->count == 0);
        REQUIRE(!strcmp(empty_saved.type_name, "Rows.Row"));
        REQUIRE(
            arena_contains(&escaped, saved.record) &&
            arena_contains(&escaped, array->record)
        );
        REQUIRE(pin_record_valid(saved.record) && array->count == 3);
        pin_value last = pin_array_get(array, 2);
        REQUIRE(!strcmp(pin_text(member(&last, 1)).bytes, "C"));
    }
    return true;
}
/* A heap its call owns collects at call boundaries, and what the call still
 * holds survives every collection. */
TEST_CASE(record_runtime_collects_during_call)
{
    static const char
        source[] = "Rows :: library { " ROW
                   "Keep :: (text: string) -> Row[] { kept := Batch(text); "
                   "i := 0; while (i < 60) { batch := Batch(\"garbage\"); "
                   "if batch[2].label != \"batch\" { return []; } i += 1; } "
                   "return kept; } "
                   "Batch :: (text: string) -> Row[] { a: Row = {key: text, "
                   "label: \"batch\"}; return [a, a, a]; } }";
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(source, backends[b], false));
        behavior_managed_heap_init(&heap);
        heap.call_collection_bytes = 4096;
        char text[] = "kept";
        pin_value arg = { .type = PIN_STR,
                          .text = &(
                              const pin_string
                          ){ .bytes = text, .length = (uint32_t)strlen(text) } };
        REQUIRE(run(backends[b], &arg, 1));
        REQUIRE(heap.storage.kept);
        pin_array *array = result.ptr;
        REQUIRE(result.container == PIN_CONTAINER_ARRAY && array && array->count == 3);
        for (int i = 0; i < array->count; i++) {
            REQUIRE(!strcmp(pin_text(array_member(array, i, 0)).bytes, "kept"));
            REQUIRE(!strcmp(pin_text(array_member(array, i, 1)).bytes, "batch"));
        }
        /* Past the budget, the heap still grows to twice what the last
         * collection kept before tracing it again. */
        size_t kept = heap.storage.kept;
        heap.call_collection_bytes = kept;
        heap.storage.allocated = 2 * kept - 1;
        heap.running_calls = 1;
        behavior_managed_heap_safepoint(&heap, NULL, NULL);
        REQUIRE(heap.storage.allocated == 2 * kept - 1 && heap.storage.kept == kept);
        heap.running_calls = 0;
        /* A call that runs inside a native of another call never collects:
         * no collection traces that native's locals. */
        cleanup();
        REQUIRE(compile(source, backends[b], false));
        behavior_managed_heap_init(&heap);
        heap.call_collection_bytes = 4096;
        heap.running_calls = 1;
        REQUIRE(run(backends[b], &arg, 1));
        REQUIRE(heap.running_calls == 1 && !heap.storage.kept);
        heap.running_calls = 0;
    }
    return true;
}
TEST_CASE(record_runtime_fieldwise_equality)
{
    static const char source[] = "Rows :: library { " ROW
                                 "Same :: (change: bool) -> bool { a: Row = "
                                 "{key: \"a\", label: \"A\"}; "
                                 "b: Row = {key: \"a\", label: \"A\"}; if "
                                 "change { b.label = \"B\"; } "
                                 "different: bool = a != b; "
                                 "return (a == b) and not different; } }";
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(source, backends[b], false));
        for (int i = 0; i < 2; i++) {
            pin_value arg = { .type = PIN_BOOL, .b = i != 0 };
            REQUIRE(run(backends[b], &arg, 1));
            REQUIRE(result.type == PIN_BOOL && result.b == (i == 0));
        }
    }
    return true;
}
TEST_CASE(record_runtime_pure_owned_mutation_preserves_caller)
{
    static const char producer[] = "Rows :: library { " ROW
                                   "Make :: () -> Row[] { return "
                                   "[{key: \"a\", label: \"A\"}, "
                                   "{key: \"b\", label: \"B\"}]; } }";
    static const char
        consumer[] = "Rows :: library { " ROW
                     "Change :: (row: Row, rows: Row[]) -> Row { "
                     "local := row; local.label = \"local\"; "
                     "row.label = \"parameter\"; rows[0].label = \"array\"; "
                     "rows[0].depth += 4; local.key = row.label; "
                     "local.depth = row.depth + rows[0].depth; "
                     "if rows[0].label != \"array\" { local.depth = -100; } "
                     "return local; } }";
    static const char reference_consumer
        [] = "Rows :: library { " ROW
             "Change :: (row: ^Row, rows: ^(Row[])) -> Row { "
             "row.label = \"reference\"; rows^[0].label = \"reference-array\"; "
             "rows^[0].depth += 7; return rows^[0]; } }";
    static const char exposed_consumer
        [] = "Rows :: library { " ROW
             "Change :: (row: Row, rows: Row[], count: int) -> Row { "
             "Mutate(&row, &rows, &count); row.depth += count; "
             "if rows[0].label != \"exposed-array\" { row.depth = -100; } "
             "return row; } "
             "Mutate :: (row: ^Row, rows: ^(Row[]), count: ^int) { "
             "row.label = \"exposed\"; rows^[0].label = \"exposed-array\"; "
             "count^ += 5; } }";
    static const char
        inline_consumer[] = "Rows :: library { " ROW
                            "Change :: (row: Row, rows: Row[], count: "
                            "int) -> Row { "
                            "row.label = \"inline\"; rows[0].label = "
                            "\"inline-array\"; count += 3; row.depth = count; "
                            "if rows[0].label != \"inline-array\" { row.depth "
                            "= -100; } return row; } }";
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(producer, backends[b], false));
        REQUIRE(run(backends[b], NULL, 0));
        REQUIRE(!pin_value_capture_to_arena(&escaped, &saved, &result));
        finish_call();
        behavior_managed_heap_free(&heap);
        execution_module_free(module);
        module = NULL;
        pin_array *original = saved.ptr;
        REQUIRE(original && original->count == 2);
        REQUIRE(compile(consumer, backends[b], false));
        pin_value args[] = { pin_array_get(original, 0), saved };
        pin_value returned = { 0 };
        for (int call = 0; call < 2; call++) {
            REQUIRE(run(backends[b], args, 2));
            REQUIRE(result.record && pin_record_valid(result.record));
            REQUIRE(!strcmp(pin_text(member(&result, 0)).bytes, "parameter"));
            REQUIRE(!strcmp(pin_text(member(&result, 1)).bytes, "local"));
            REQUIRE(member(&result, 5).i == 6);
            REQUIRE(!strcmp(pin_text(member(&args[0], 0)).bytes, "a"));
            REQUIRE(!strcmp(pin_text(member(&args[0], 1)).bytes, "A"));
            REQUIRE(member(&args[0], 5).i == 1);
            REQUIRE(!strcmp(pin_text(array_member(original, 0, 1)).bytes, "A"));
            REQUIRE(array_member(original, 0, 5).i == 1);
            REQUIRE(!strcmp(pin_text(array_member(original, 1, 1)).bytes, "B"));
            REQUIRE(array_member(original, 1, 5).i == 1);
            REQUIRE(!pin_value_snapshot_to_arena(&escaped, &returned, &result));
        }
        finish_call();
        execution_module_free(module);
        module = NULL;
        REQUIRE(pin_record_valid(returned.record));
        REQUIRE(!strcmp(pin_text(member(&returned, 0)).bytes, "parameter"));
        REQUIRE(!strcmp(pin_text(member(&returned, 1)).bytes, "local"));
        REQUIRE(member(&returned, 5).i == 6);
        REQUIRE(pin_record_valid(original->record));
        REQUIRE(!strcmp(pin_text(array_member(original, 0, 1)).bytes, "A"));
        const char *value_consumers[] = { exposed_consumer, inline_consumer };
        const char *labels[] = { "exposed", "inline" };
        const int depths[] = { 8, 5 };
        for (int c = 0; c < 2; c++) {
            REQUIRE(compile(value_consumers[c], backends[b], false));
            pin_value inputs[] = { pin_array_get(original, 0),
                                   saved,
                                   { .type = PIN_I32, .i = 2 } };
            REQUIRE(run(backends[b], inputs, 3));
            REQUIRE(!strcmp(pin_text(member(&result, 1)).bytes, labels[c]));
            REQUIRE(member(&result, 5).i == depths[c]);
            REQUIRE(!strcmp(pin_text(member(&inputs[0], 1)).bytes, "A"));
            REQUIRE(member(&inputs[0], 5).i == 1);
            REQUIRE(inputs[1].ptr == original);
            REQUIRE(!strcmp(pin_text(array_member(original, 0, 1)).bytes, "A"));
            REQUIRE(array_member(original, 0, 5).i == 1);
            REQUIRE(inputs[2].i == 2);
            finish_call();
            execution_module_free(module);
            module = NULL;
        }
        REQUIRE(compile(reference_consumer, backends[b], false));
        reference_owner = (behavior_function_context){ .value_storage = &escaped,
                                                       .managed_heap = &heap };
        /* References write their values in place, so the row is a value of
         * its own rather than a header over the array's first item. */
        pin_value row;
        REQUIRE(!pin_value_snapshot_to_arena(&escaped, &row, &args[0]));
        pin_value references[] = {
            execution_reference_value(&escaped, &reference_owner, &row, row),
            execution_reference_value(&escaped, &reference_owner, &saved, saved),
        };
        REQUIRE(run(backends[b], references, 2));
        REQUIRE(!strcmp(pin_text(member(&row, 1)).bytes, "reference"));
        pin_array *changed = saved.ptr;
        REQUIRE(changed && changed->count == 2);
        REQUIRE(!strcmp(pin_text(array_member(changed, 0, 1)).bytes, "reference-array"));
        REQUIRE(array_member(changed, 0, 5).i == 8);
        REQUIRE(!strcmp(pin_text(member(&result, 1)).bytes, "reference-array"));
        REQUIRE(member(&result, 5).i == 8);
        REQUIRE(!strcmp(pin_text(array_member(original, 0, 1)).bytes, "A"));
        finish_call();
        execution_module_free(module);
        module = NULL;
    }
    return true;
}
typedef struct
{
    const type_desc *types;
    int count;
} private_types;
static const type_desc *find_private_type(void *user, const char *name)
{
    const private_types *source = user;
    for (int i = 0; source && i < source->count; i++) {
        if (!strcmp(source->types[i].name, name)) {
            return &source->types[i];
        }
    }
    return NULL;
}
TEST_CASE(record_runtime_private_layout_lifetime)
{
    typedef struct
    {
        const char *label;
        int count;
    } child_value;
    typedef struct
    {
        bool enabled;
        child_value child;
        uint64_t opaque;
    } root_value;
    char root_name[] = "private.Root", child_name[] = "private.Child";
    char description[] = "Native child label", label[] = "retained";
    type_field child_fields[] = {
        { .name = "label",
          .type = PIN_STR,
          .offset = offsetof(child_value, label),
          .size = sizeof(char *),
          .type_name = "const char *",
          .description = description },
        { .name = "count",
          .type = PIN_I32,
          .offset = offsetof(child_value, count),
          .size = sizeof(int),
          .type_name = "int" },
    };
    type_field root_fields[] = {
        { .name = "enabled",
          .type = PIN_BOOL,
          .offset = offsetof(root_value, enabled),
          .size = sizeof(bool),
          .type_name = "bool" },
        { .name = "child",
          .type = PIN_STRUCT,
          .offset = offsetof(root_value, child),
          .size = sizeof(child_value),
          .type_name = child_name },
    };
    type_desc types[] = {
        { .name = root_name, .size = sizeof(root_value), .fields = root_fields, .field_count = 2 },
        { .name = child_name,
          .size = sizeof(child_value),
          .fields = child_fields,
          .field_count = 2 },
    };
    private_types source = { types, 2 };
    type_resolver resolver = { .find = find_private_type, .user = &source };
    REQUIRE(!pin_record_from_type(&escaped, &types[0]));
    const pin_record *record = pin_record_from_type_resolved(&escaped, &types[0], resolver);
    REQUIRE(record && record->native && pin_record_valid(record));
    root_value original = { true, { label, 7 }, UINT64_C(0x12345678) };
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = &original };
    REQUIRE(!pin_value_snapshot_to_arena(&storage, &result, &value));

    source = (private_types){ 0 };
    memset(types, 0, sizeof types);
    memset(child_fields, 0, sizeof child_fields);
    memset(root_fields, 0, sizeof root_fields);
    memset(root_name, 0, sizeof root_name);
    memset(child_name, 0, sizeof child_name);
    memset(description, 0, sizeof description);
    memset(label, 'x', sizeof label - 1);
    arena_reset(&escaped);
    memset(arena_alloc(&escaped, 4096), 0, 4096);

    REQUIRE(pin_record_valid(result.record));
    resolver = pin_record_resolver(result.record);
    const type_desc *root = type_resolve(resolver, "private.Root");
    const type_desc *child = type_resolve(resolver, "private.Child");
    REQUIRE(root && child && root->size == sizeof(root_value));
    REQUIRE(root->fields[1].offset == offsetof(root_value, child));
    REQUIRE(root->fields[1].layout == child);
    REQUIRE(!child->fields[0].text_descriptor);
    REQUIRE(!strcmp(child->fields[0].description, "Native child label"));
    REQUIRE(((const root_value *)result.ptr)->opaque == UINT64_C(0x12345678));
    pin_value selected, text;
    REQUIRE(!pin_read_field(&escaped, result.ptr, &root->fields[1], &selected));
    REQUIRE(selected.record == child->carrier);
    REQUIRE(!pin_read_field(&escaped, selected.ptr, &child->fields[0], &text));
    REQUIRE(pin_text(text).length == 8);
    REQUIRE(!memcmp(pin_text(text).bytes, "retained", 8));
    REQUIRE(!type_resolve(resolver, "private.Missing"));
    REQUIRE(!type_resolve(pin_record_resolver(NULL), "private.Root"));
    return true;
}
TEST_CASE(record_runtime_private_alias_identity)
{
    const type_field field = { .name = "value",
                               .type = PIN_I32,
                               .size = sizeof(int),
                               .type_name = "int" };
    const type_desc types[] = {
        { .name = "private.Left", .size = sizeof(int), .fields = &field, .field_count = 1 },
        { .name = "private.Right", .size = sizeof(int), .fields = &field, .field_count = 1 },
    };
    private_types source = { types, 2 };
    type_resolver resolver = { .find = find_private_type, .user = &source };
    type_desc alias = { .name = "private.Alias",
                        .size = sizeof(int),
                        .alias_of = "private.Left" };
    const pin_record *left = pin_record_from_type_resolved(&escaped, &alias, resolver);
    alias.alias_of = "private.Right";
    const pin_record *right = pin_record_from_type_resolved(&escaped, &alias, resolver);
    REQUIRE(left && right && pin_record_valid(left) && pin_record_valid(right));
    source = (private_types){ 0 };
    REQUIRE(!pin_record_same(left, right));
    REQUIRE(
        type_resolve(pin_record_resolver(left), "private.Left") == left->type.alias
    );
    REQUIRE(!type_resolve(pin_record_resolver(left), "private.Right"));
    REQUIRE(!strcmp(right->type.alias->name, "private.Right"));

    alias.alias_of = "private.Missing";
    REQUIRE(!pin_record_from_type_resolved(&escaped, &alias, resolver));
    alias.alias_of = alias.name;
    source = (private_types){ &alias, 1 };
    REQUIRE(!pin_record_from_type_resolved(&escaped, &alias, resolver));
    return true;
}
TEST_CASE(record_runtime_conflicting_private_layouts)
{
    const type_field first = { .name = "first",
                               .type = PIN_I32,
                               .size = sizeof(int),
                               .type_name = "int" };
    const type_field second = { .name = "second",
                                .type = PIN_I32,
                                .size = sizeof(int),
                                .type_name = "int" };
    const type_desc children[] = {
        { .name = "private.Duplicate", .size = sizeof(int), .fields = &first, .field_count = 1 },
        { .name = "private.Duplicate", .size = sizeof(int), .fields = &second, .field_count = 1 },
    };
    const type_field fields[] = {
        { .name = "a",
          .type = PIN_STRUCT,
          .size = sizeof(int),
          .type_name = "private.Duplicate",
          .layout = &children[0] },
        { .name = "b",
          .type = PIN_STRUCT,
          .offset = sizeof(int),
          .size = sizeof(int),
          .type_name = "private.Duplicate",
          .layout = &children[1] },
    };
    const type_desc type = { .name = "private.DuplicateRoot",
                             .size = 2 * sizeof(int),
                             .fields = fields,
                             .field_count = 2 };
    const pin_record *record = pin_record_from_type(&escaped, &type);
    REQUIRE(record && pin_record_valid(record));
    type_resolver resolver = pin_record_resolver(record);
    REQUIRE(type_resolve(resolver, "private.DuplicateRoot") == &record->type);
    REQUIRE(!type_resolve(resolver, "private.Duplicate"));
    REQUIRE(!strcmp(record->type.fields[0].layout->fields[0].name, "first"));
    REQUIRE(!strcmp(record->type.fields[1].layout->fields[0].name, "second"));
    return true;
}
void test_language_record_runtime()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_runtime_owned_return_and_value_copy);
    RUN_TEST_CASE(record_runtime_array_results);
    RUN_TEST_CASE(record_runtime_collects_during_call);
    RUN_TEST_CASE(record_runtime_fieldwise_equality);
    RUN_TEST_CASE(record_runtime_pure_owned_mutation_preserves_caller);
    RUN_TEST_CASE(record_runtime_private_layout_lifetime);
    RUN_TEST_CASE(record_runtime_private_alias_identity);
    RUN_TEST_CASE(record_runtime_conflicting_private_layouts);
    pu_clear_setup();
}
