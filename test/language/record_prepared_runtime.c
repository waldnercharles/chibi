#include "authoring/source_format.h"
#include "backend.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/prepared_internal.h"
#include "execution/status.h"
#include "execution/wasm_jit.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"
#include "types/trace.h"
#include "types/union.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fourteen fixed prepared cases. The largest validation fixture has 128 fields
 * and 256 shared headers, and the type lookup fixtures up to 512 types and 512
 * headers. No files, frames, worker pools or benchmark loops. */
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static behavior_function_builder *builder;
static behavior_function *decoded_function;
static uint64_t function_id;
static behavior_function_context context;
static behavior_function_activation activation;
static behavior_run_scope runs;
static arena storage, escaped;
static pin_value result, saved;
static unsigned char *image, *input;
static size_t image_size;
static prepared_context writer, reader;
static execution_module_plan *write_plan, *read_plan;
static arena producer;
static bool project_set;
static const script_catalog catalog;
static const script_lower_bindings bindings;
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
#define ROW_FIELDS                                                             \
    "key: string; label: string; icon: string = \"node\"; "                    \
    "prototype: string = \"\"; drop: string = \"\"; depth: int = 1; "          \
    "behaviors: int = 0; active: bool = true; marked: bool = false; "          \
    "hidden: bool = false; locked: bool = false; renaming: bool = false;"
#define ROW "Row :: struct { " ROW_FIELDS " } "
static pin_value text_value(const char *bytes)
{
    pin_value value = { 0 };
    const char *error = pin_value_text(&producer, &value, bytes, strlen(bytes));
    assert(!error);
    (void)error;
    return value;
}
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
    execution_module_free(module);
    behavior_function_free(decoded_function);
    decoded_function = NULL;
    if (builder) {
        behavior_function_builder_free(builder);
        builder = NULL;
    }
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    arena_free(&escaped);
    free(image);
    free(input);
    free(writer.records);
    free(reader.records);
    afree(writer.functions);
    afree(reader.functions);
    afree(writer.wire.data);
    execution_module_plan_free(write_plan);
    execution_module_plan_free(read_plan);
    arena_free(&producer);
    if (project_set) {
        type_catalog_set_project(NULL);
    }
    project_set = false;
    image = input = NULL;
    image_size = 0;
    writer = reader = (prepared_context){ 0 };
    write_plan = read_plan = NULL;
    module = NULL;
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
    saved = (pin_value){ 0 };
}
static bool compile(const char *source, behavior_backend backend)
{
    syntax = script_parse(source);
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "record semantic: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid);
    REQUIRE(analysis->function_count == 1 && analysis->functions[0].symbol >= 0);
    function_id = analysis->symbols[analysis->functions[0].symbol].id;
    plan = script_lower(analysis, &catalog, &bindings);
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "record lower: %s\n", plan->diagnostics[i].message);
    }
    REQUIRE(plan && plan->valid);
    char error[256];
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
    context = (behavior_function_context){ .activation = &activation,
                                           .arguments = args,
                                           .argument_count = count,
                                           .results = &result,
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
    const char *error = pin_read_field(&producer, row->ptr, &row->record->type.fields[index], &field);
    assert(!error);
    (void)error;
    return field;
}
static bool encode()
{
    char error[256];
    REQUIRE(execution_prepared_write(module, &image, &image_size, error, sizeof error));
    REQUIRE(image && image_size > 32);
    execution_module_free(module);
    module = NULL;
    return true;
}
static bool decode(behavior_backend backend)
{
    input = malloc(image_size);
    REQUIRE(input);
    memcpy(input, image, image_size);
    char error[256];
    module = execution_prepared_read(input, image_size, NULL, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "record prepared: %s\n", error);
    }
    REQUIRE(module && execution_module_backend(module) == backend);
    memset(input, 0xa5, image_size);
    free(input);
    input = NULL;
    return true;
}
TEST_CASE(record_prepared_scalar_owners)
{
    static const char source[] = "Rows :: library { " ROW
                                 "Make :: (text: string) -> Row { return {key: "
                                 "text, label: text}; } }";
    REQUIRE(compile(source, BEHAVIOR_BYTECODE));
    REQUIRE(encode());
    for (int b = 0; b < 2; b++) {
        REQUIRE(decode(backends[b]));
        char text[] = "prepared-owner";
        pin_string descriptor = { .bytes = text, .length = sizeof text - 1 };
        pin_value arg = { .type = PIN_STR, .text = &descriptor };
        REQUIRE(run(backends[b], &arg, 1));
        REQUIRE(result.record && arena_contains(&storage, result.record));
        memset(text, 'x', sizeof text - 1);
        REQUIRE(!strcmp(pin_text(member(&result, 0)).bytes, "prepared-owner"));
        REQUIRE(!strcmp(pin_text(member(&result, 2)).bytes, "node"));
        REQUIRE(!pin_value_snapshot_to_arena(&escaped, &saved, &result));
        finish_call();
        execution_module_free(module);
        module = NULL;
        REQUIRE(pin_record_valid(saved.record));
        REQUIRE(!strcmp(pin_text(member(&saved, 1)).bytes, "prepared-owner"));
        arena_free(&escaped);
        saved = (pin_value){ 0 };
    }
    return true;
}
TEST_CASE(record_prepared_array_owners)
{
    static const char source
        [] = "Rows :: library { " ROW
             "MakeRows :: (n: int, empty: Row[]) -> Row[] { "
             "if n < 0 { return empty; } "
             "if n == 0 { return []; } a: Row = {key: \"a\", label: \"A\"}; "
             "if n == 1 { return [a]; } return [a, a, a]; } }";
    REQUIRE(compile(source, BEHAVIOR_BYTECODE));
    REQUIRE(encode());
    const int counts[] = { -1, 0, 1, 3 };
    for (int b = 0; b < 2; b++) {
        REQUIRE(decode(backends[b]));
        REQUIRE(body());
        pin_value args[2] = { { .type = PIN_I32 }, body()->result_defaults[0] };
        args[1].ptr = NULL;
        pin_value null_copy = { 0 };
        for (int i = 0; i < 4; i++) {
            args[0].i = counts[i];
            REQUIRE(run(backends[b], args, 2));
            REQUIRE(result.record && arena_contains(&storage, result.record));
            REQUIRE(result.container == PIN_CONTAINER_ARRAY);
            const pin_array *a = result.ptr;
            if (counts[i] < 0) {
                REQUIRE(!a);
                REQUIRE(!pin_value_snapshot_to_arena(&escaped, &null_copy, &result));
                continue;
            }
            REQUIRE(a && a->count == counts[i] && a->record == result.record);
            for (int j = 0; j < a->count; j++) {
                REQUIRE(pin_array_get(a, j).record == a->record);
                pin_value item = pin_array_get(a, j);
                REQUIRE(!strcmp(pin_text(member(&item, 1)).bytes, "A"));
            }
        }
        REQUIRE(!pin_value_snapshot_to_arena(&escaped, &saved, &result));
        finish_call();
        execution_module_free(module);
        module = NULL;
        REQUIRE(null_copy.record && !null_copy.ptr && pin_record_valid(null_copy.record));
        REQUIRE(pin_record_valid(saved.record) && ((pin_array *)saved.ptr)->count == 3);
        arena_free(&escaped);
        saved = (pin_value){ 0 };
    }
    return true;
}
static const type_field fields[] = { { .name = "key", .type = PIN_STR },
                                     { .name = "label", .type = PIN_STR },
                                     { .name = "icon", .type = PIN_STR },
                                     { .name = "prototype", .type = PIN_STR },
                                     { .name = "drop", .type = PIN_STR },
                                     { .name = "depth", .type = PIN_I32 },
                                     { .name = "behaviors", .type = PIN_I32 },
                                     { .name = "active", .type = PIN_BOOL },
                                     { .name = "marked", .type = PIN_BOOL },
                                     { .name = "hidden", .type = PIN_BOOL },
                                     { .name = "locked", .type = PIN_BOOL },
                                     { .name = "renaming", .type = PIN_BOOL } };
static bool table_fields(const type_field *shape, int count)
{
    write_plan = execution_module_plan_new();
    const pin_record *r = pin_record_create(&write_plan->storage, "Rows.Row", 91, shape, count);
    REQUIRE(r);
    apush(
        write_plan->types,
        ((execution_type_requirement){ .id = r->id,
                                       .storage = PIN_STRUCT,
                                       .name = r->type.name,
                                       .record = r,
                                       .layout = r->layout,
                                       .size = r->type.size })
    );
    write_plan->type_count = 1;
    writer = (prepared_context){ .plan = write_plan };
    writer.wire.storage = &write_plan->storage;
    prepared_types(&writer);
    REQUIRE(!writer.wire.error);
    return true;
}
static bool table()
{
    return table_fields(fields, 12);
}
static bool read_table()
{
    read_plan = execution_module_plan_new();
    arena_init_collectable(&read_plan->storage);
    reader = (prepared_context){ .plan = read_plan };
    reader.wire = (execution_wire){ .reading = true,
                                    .data = writer.wire.data,
                                    .size = writer.wire.size,
                                    .storage = &read_plan->storage };
    prepared_types(&reader);
    REQUIRE(!reader.wire.error && read_plan->type_count == 1);
    return true;
}
TEST_CASE(record_prepared_shared_headers_fit_default_work)
{
    enum
    {
        FIELD_COUNT = 128,
        HEADER_COUNT = 256
    };
    char names[FIELD_COUNT][16];
    type_field shape[FIELD_COUNT] = { 0 };
    for (int i = 0; i < FIELD_COUNT; i++) {
        snprintf(names[i], sizeof names[i], "field%03d", i);
        shape[i] = (type_field){ .name = names[i], .type = PIN_I32 };
    }
    const pin_record *record = pin_record_create(&producer, "Rows.Row", 91, shape, FIELD_COUNT);
    REQUIRE(record);
    const pin_value header = { .type = PIN_STRUCT,
                               .type_name = record->type.name,
                               .record = record };
    builder = behavior_function_builder_new();
    for (int i = 0; i < HEADER_COUNT; i++) {
        behavior_function_builder_values(builder, &header, 1);
    }
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    decoded_function = behavior_function_builder_finish(builder, error, sizeof error);
    REQUIRE(decoded_function);
    behavior_function_builder_free(builder);
    builder = NULL;
    plan = execution_module_plan_new();
    plan->id = function_id = 1;
    plan->valid = true;
    apush(
        plan->types,
        ((execution_type_requirement){ .id = record->id,
                                       .storage = PIN_STRUCT,
                                       .name = record->type.name,
                                       .record = record,
                                       .layout = record->layout,
                                       .size = record->type.size })
    );
    plan->type_count = 1;
    apush(
        plan->functions,
        ((execution_module_function){ .id = function_id,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = decoded_function })
    );
    decoded_function = NULL;
    plan->function_count = 1;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module && !plan);
    arena_free(&producer);
    REQUIRE(encode());
    /* The wire and all initial header checks fit the default layout-work
     * bound. Revalidating the same 128-field layout for every final header
     * used to exhaust it. Neither writer nor reader receives raised bounds. */
    REQUIRE(decode(BEHAVIOR_BYTECODE));
    const behavior_function *function = body();
    REQUIRE(function && acount(function->data_values) == HEADER_COUNT);
    record = function->data_values[0].initial.record;
    REQUIRE(record && pin_record_valid(record));
    REQUIRE(record->type.field_count == FIELD_COUNT && arena_contains(&function->owned, record));
    for (int i = 0; i < HEADER_COUNT; i++) {
        pin_value value = function->data_values[i].initial;
        REQUIRE(value.type == PIN_STRUCT && value.container == PIN_CONTAINER_SINGLE);
        REQUIRE(
            value.record == record && value.type_name == record->type.name &&
            !value.ptr
        );
    }
    REQUIRE(
        !pin_value_snapshot_to_arena(&escaped, &saved, &function->data_values[0].initial)
    );
    execution_module_free(module);
    module = NULL;
    REQUIRE(saved.record && pin_record_valid(saved.record));
    REQUIRE(saved.record->type.field_count == FIELD_COUNT);
    REQUIRE(!strcmp(saved.record->type.fields[FIELD_COUNT - 1].name, names[FIELD_COUNT - 1]));
    return true;
}
enum
{
    NAMED_TYPES = 256,
    NAMED_HEADERS = 256
};
/* A prepared image of a module requiring `types` one-field records, whose
 * `headers` values all have the last one's type. */
static bool named_headers_image(int types, int headers)
{
    static const type_field shape[] = { { .name = "value", .type = PIN_I32 } };
    plan = execution_module_plan_new();
    plan->id = function_id = 1;
    plan->valid = true;
    const pin_record *record = NULL;
    for (int i = 0; i < types; i++) {
        char name[16];
        snprintf(name, sizeof name, "Rows.T%04d", i);
        record = pin_record_create(&producer, name, 100 + i, shape, 1);
        REQUIRE(record);
        apush(
            plan->types,
            ((execution_type_requirement){ .id = record->id,
                                           .storage = PIN_STRUCT,
                                           .name = record->type.name,
                                           .record = record,
                                           .layout = record->layout,
                                           .size = record->type.size })
        );
    }
    plan->type_count = types;
    const pin_value header = { .type = PIN_STRUCT,
                               .type_name = record->type.name,
                               .record = record };
    builder = behavior_function_builder_new();
    for (int i = 0; i < headers; i++) {
        behavior_function_builder_values(builder, &header, 1);
    }
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    decoded_function = behavior_function_builder_finish(builder, error, sizeof error);
    REQUIRE(decoded_function);
    behavior_function_builder_free(builder);
    builder = NULL;
    apush(
        plan->functions,
        ((execution_module_function){ .id = function_id,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = decoded_function })
    );
    decoded_function = NULL;
    plan->function_count = 1;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module && !plan);
    arena_free(&producer);
    return encode();
}
/* Whether `image` decodes with `layout_work` units of layout work. Any other
 * failure fails the case. */
static bool admits_layout_work(size_t layout_work, bool *admitted)
{
    input = malloc(image_size);
    REQUIRE(input);
    memcpy(input, image, image_size);
    size_t remaining = EXECUTION_WIRE_MAX_BYTES;
    execution_prepared_linker bounded = { .layout_work = layout_work };
    char error[256] = { 0 };
    module = execution_prepared_read_budget(
        input,
        image_size,
        &bounded,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    free(input);
    input = NULL;
    *admitted = module;
    execution_module_free(module);
    module = NULL;
    REQUIRE(*admitted || strstr(error, "layout work limit"));
    return true;
}
/* The least layout work that decodes the image of `headers` named headers. */
static bool least_layout_work(int types, int headers, size_t *least)
{
    REQUIRE(named_headers_image(types, headers));
    size_t low = 1, high = EXECUTION_WIRE_MAX_BYTES;
    bool admitted;
    REQUIRE(admits_layout_work(high, &admitted) && admitted);
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        REQUIRE(admits_layout_work(middle, &admitted));
        if (admitted) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    free(image);
    image = NULL;
    *least = low;
    return true;
}
/* The layout work each of the named headers adds, from the least work that
 * decodes `headers` and twice as many with `types` types. */
static bool header_layout_work(int types, int headers, size_t *each)
{
    size_t least[2];
    for (int i = 0; i < 2; i++) {
        REQUIRE(least_layout_work(types, headers << i, &least[i]));
    }
    REQUIRE(least[1] > least[0]);
    *each = (least[1] - least[0]) / (size_t)headers;
    return true;
}
/* Each header finds its type by name in log2 of the type count comparisons,
 * not by a scan of every type before it. A scan charges about NAMED_TYPES
 * times the name per header; a quarter of that is the bound. Every
 * comparison is still charged: twice the types cost each header at least one
 * more comparison. */
TEST_CASE(record_prepared_finds_header_types_by_name)
{
    size_t each, wider;
    REQUIRE(header_layout_work(NAMED_TYPES, NAMED_HEADERS, &each));
    REQUIRE(header_layout_work(2 * NAMED_TYPES, NAMED_HEADERS, &wider));
    size_t name = strlen("Rows.T0000") + 2;
    REQUIRE(each < NAMED_TYPES * name / 4);
    REQUIRE(wider >= each + name);
    return true;
}
/* Exercise the decoded verifier directly: ordinary builder ownership also
 * validates incoming records and could hide a broken verifier proof cache. */
static bool decoded_headers(const type_field *shape, int count)
{
    assert(!decoded_function);
    decoded_function = calloc(1, sizeof *decoded_function);
    REQUIRE(decoded_function);
    decoded_function->artifact = execution_artifact_new();
    apush(decoded_function->sources, ((behavior_function_source){ .parent = -1 }));
    apush(
        decoded_function->frames,
        ((behavior_function_frame){ .source = { .parent = -1 }, .stack = -1 })
    );
    const pin_record *record = pin_record_create(&decoded_function->owned, "Rows.Row", 91, shape, count);
    REQUIRE(record);
    pin_value header = { .type = PIN_STRUCT,
                         .type_name = record->type.name,
                         .record = record };
    for (int i = 0; i < 2; i++) {
        apush(decoded_function->data_values, ((behavior_function_data_value){ .initial = header }));
    }
    apush(decoded_function->instructions, ((behavior_function_instruction){ .op = BF_END }));
    return true;
}
TEST_CASE(record_prepared_proof_keeps_each_header_check)
{
    const type_field field = { .name = "value", .type = PIN_I32 };
    for (int mode = 0; mode < 4; mode++) {
        REQUIRE(decoded_headers(&field, 1));
        pin_value *later = &decoded_function->data_values[1].initial;
        if (mode == 0) {
            const pin_record *other = pin_record_create(
                &decoded_function->owned,
                later->record->type.name,
                later->record->id,
                &field,
                1
            );
            REQUIRE(
                other && other != later->record && pin_record_same(other, later->record)
            );
            ((type_field *)other->type.fields)[0].offset++;
            REQUIRE(!pin_record_valid(other));
            later->record = other;
            later->type_name = other->type.name;
        } else if (mode == 1) {
            later->type = PIN_I32;
        } else if (mode == 2) {
            later->container = (pin_container)(PIN_CONTAINER_MAP + 1);
        } else {
            later->type_name = "Rows.Other";
        }
        char error[256];
        REQUIRE(
            !behavior_function_prepare_definition(decoded_function, SIZE_MAX, error, sizeof error)
        );
        REQUIRE(strstr(error, "invalid host value type") && !decoded_function->frozen);
        behavior_function_free(decoded_function);
        decoded_function = NULL;
    }
    return true;
}
TEST_CASE(record_prepared_failed_proofs_do_not_survive_retry)
{
    const type_field field = { .name = "value", .type = PIN_I32 };
    REQUIRE(decoded_headers(&field, 1));
    type_field *owned = (type_field *)decoded_function->data_values[0]
                            .initial.record->type.fields;
    owned[0].offset++;
    char error[256];
    REQUIRE(!behavior_function_prepare_definition(decoded_function, SIZE_MAX, error, sizeof error));
    REQUIRE(strstr(error, "invalid host value type"));
    owned[0].offset--;
    pin_value *later = &decoded_function->data_values[1].initial;
    const char *name = later->type_name;
    later->type_name = "Rows.Other";
    REQUIRE(!behavior_function_prepare_definition(decoded_function, SIZE_MAX, error, sizeof error));
    REQUIRE(strstr(error, "invalid host value type"));
    later->type_name = name;
    owned[0].offset++;
    REQUIRE(!behavior_function_prepare_definition(decoded_function, SIZE_MAX, error, sizeof error));
    REQUIRE(strstr(error, "invalid host value type"));
    owned[0].offset--;
    REQUIRE(behavior_function_prepare_definition(decoded_function, SIZE_MAX, error, sizeof error));
    REQUIRE(decoded_function->frozen && !*error);
    return true;
}
TEST_CASE(record_prepared_proofs_do_not_outlive_native_catalog)
{
    static const type_field native_fields[] = {
        { .name = "value", .type = PIN_I32, .size = sizeof(int32_t) },
        { .name = "changed", .type = PIN_I32, .size = sizeof(int32_t) }
    };
    static const type_desc native_types[] = {
        { .name = "RecordValidation.Native",
          .size = sizeof(int32_t),
          .fields = &native_fields[0],
          .field_count = 1 },
        { .name = "RecordValidation.Native",
          .size = sizeof(int32_t),
          .fields = &native_fields[1],
          .field_count = 1 }
    };
    static const type_desc *const first[] = { &native_types[0] };
    static const type_desc *const replacement[] = { &native_types[1] };
    static const type_catalog_pack packs[] = { { .types = first, .type_count = 1 },
                                               { .types = replacement,
                                                 .type_count = 1 } };
    type_catalog_set_project(&packs[0]);
    project_set = true;
    const type_field nested = { .name = "part",
                                .type = PIN_STRUCT,
                                .type_name = "RecordValidation.Native" };
    REQUIRE(decoded_headers(&nested, 1));
    pin_value *later = &decoded_function->data_values[1].initial;
    const char *name = later->type_name;
    const pin_record *record = later->record;
    REQUIRE(pin_record_valid(record));
    later->type_name = "Rows.Other";
    char error[256];
    REQUIRE(!behavior_function_prepare_definition(decoded_function, SIZE_MAX, error, sizeof error));
    REQUIRE(strstr(error, "invalid host value type"));
    later->type_name = name;
    type_catalog_set_project(NULL);
    project_set = false;
    type_catalog_set_project(&packs[1]);
    project_set = true;
    /* The descriptor pointer and nested size are unchanged. Its old layout
     * proof must not survive replacing the native catalog between attempts. */
    REQUIRE(pin_record_current(record) && !pin_record_valid(record));
    REQUIRE(!behavior_function_prepare_definition(decoded_function, SIZE_MAX, error, sizeof error));
    REQUIRE(strstr(error, "invalid host value type"));
    type_catalog_set_project(NULL);
    project_set = false;
    type_catalog_set_project(&packs[0]);
    project_set = true;
    REQUIRE(behavior_function_prepare_definition(decoded_function, SIZE_MAX, error, sizeof error));
    REQUIRE(decoded_function->frozen && pin_record_valid(record));
    return true;
}
static void collect(arena *a, const void *root)
{
    arena_collection c;
    arena *owners[] = { a };
    arena_collect_begin(&c, owners, 1);
    arena_collect_mark(&c, root);
    arena_collect_end(&c);
}
TEST_CASE(record_prepared_recursive_codec)
{
    REQUIRE(table());
    const pin_record *r = write_plan->types[0].record;
    pin_value row = { .type = PIN_STRUCT,
                      .type_name = r->type.name,
                      .record = r,
                      .ptr = arena_alloc(&producer, r->type.size) };
    memset(row.ptr, 0, r->type.size);
    for (int i = 0; i < 5; i++) {
        char *text = arena_alloc(&producer, 7);
        memcpy(text, "owned!", 7);
        REQUIRE(!pin_write_field(&producer, row.ptr, &r->type.fields[i], text_value(text)));
    }
    pin_value items[3] = { row, row, row };
    pin_array arrays[2] = {
        { .element_type = PIN_STRUCT, .type_name = r->type.name, .record = r },
        { .element_type = PIN_STRUCT,
          .type_name = r->type.name,
          .record = r,
          .count = 3,
          .capacity = 3,
          .items = items }
    };
    pin_union *tagged = arena_alloc(&producer, sizeof *tagged + 4 * sizeof(pin_value));
    *tagged = (pin_union){ .tag = 73, .count = 4 };
    tagged->values[0] = row;
    for (int i = 1; i < 4; i++) {
        tagged->values[i] = (pin_value){ .type = PIN_STRUCT,
                                         .type_name = r->type.name,
                                         .record = r,
                                         .container = PIN_CONTAINER_ARRAY,
                                         .ptr = i == 1 ? NULL : &arrays[i - 2] };
    }
    pin_value v = { .type = PIN_UNION, .ptr = tagged };
    prepared_value(&writer, &v);
    REQUIRE(!writer.wire.error);
    REQUIRE(read_table());
    pin_value decoded = { 0 };
    prepared_value(&reader, &decoded);
    REQUIRE(!reader.wire.error && reader.wire.offset == reader.wire.size);
    arena_free(&producer);
    execution_module_plan_free(write_plan);
    write_plan = NULL;
    free(writer.records);
    writer.records = NULL;
    afree(writer.wire.data);
    pin_union *copy = decoded.ptr;
    REQUIRE(copy && copy->count == 4);
    collect(&read_plan->storage, copy);
    const pin_record *owned = copy->values[0].record;
    REQUIRE(owned && pin_record_valid(owned));
    for (int i = 0; i < 4; i++) {
        REQUIRE(copy->values[i].record == owned);
    }
    REQUIRE(!copy->values[1].ptr);
    REQUIRE(((pin_array *)copy->values[2].ptr)->count == 0);
    pin_array *rows = copy->values[3].ptr;
    REQUIRE(rows->count == 3 && rows->record == owned);
    for (int i = 0; i < 3; i++) {
        REQUIRE(pin_array_get(rows, i).record == owned);
    }
    pin_value raw = copy->values[0];
    collect(&read_plan->storage, raw.ptr);
    REQUIRE(pin_record_valid(raw.record));
    for (int i = 0; i < 5; i++) {
        REQUIRE(!strcmp(pin_text(member(&raw, i)).bytes, "owned!"));
    }
    collect(&read_plan->storage, NULL);
    size_t bytes, capacity;
    arena_usage(&read_plan->storage, &bytes, &capacity);
    REQUIRE(bytes == 0);
    return true;
}
TEST_CASE(record_prepared_packed_rows_survive_collection)
{
    const type_field field = { .name = "count", .type = PIN_I32 };
    REQUIRE(table_fields(&field, 1));
    const pin_record *r = write_plan->types[0].record;
    const int32_t values[] = { 11, 29 };
    REQUIRE(r->type.size == sizeof values[0]);
    void *data = arena_alloc(&producer, sizeof values);
    memcpy(data, values, sizeof values);
    pin_array array = { .element_type = PIN_STRUCT,
                        .type_name = r->type.name,
                        .record = r,
                        .count = 2,
                        .capacity = 2,
                        .data = data,
                        .stride = r->type.size };
    pin_value value = { .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = r->type.name,
                        .record = r,
                        .ptr = &array };
    prepared_value(&writer, &value);
    REQUIRE(!writer.wire.error);
    REQUIRE(read_table());
    pin_value decoded = { 0 };
    prepared_value(&reader, &decoded);
    REQUIRE(!reader.wire.error && reader.wire.offset == reader.wire.size);
    arena_free(&producer);
    execution_module_plan_free(write_plan);
    write_plan = NULL;
    free(writer.records);
    writer.records = NULL;
    afree(writer.wire.data);
    pin_array *rows = decoded.ptr;
    collect(&read_plan->storage, rows);
    REQUIRE(rows && pin_array_valid(rows) && rows->count == 2);
    REQUIRE(rows->stride == sizeof values[0] && !rows->items);
    REQUIRE(rows->record == decoded.record && pin_record_valid(rows->record));
    REQUIRE(arena_contains(&read_plan->storage, rows->record));
    REQUIRE(arena_contains(&read_plan->storage, rows->data));
    for (int i = 0; i < 2; i++) {
        pin_value row = pin_array_get(rows, i), field_value;
        REQUIRE(row.record == rows->record);
        REQUIRE(!pin_read_field(&producer, row.ptr, &row.record->type.fields[0], &field_value));
        REQUIRE(field_value.type == PIN_I32 && field_value.i == values[i]);
    }
    collect(&read_plan->storage, NULL);
    size_t bytes, capacity;
    arena_usage(&read_plan->storage, &bytes, &capacity);
    REQUIRE(bytes == 0);
    return true;
}
/* One twelve-field immutable layout and twenty-four payload-free headers.
 * A 32 KiB work budget admits hashing/reuse; 8 KiB still stops decoding. */
TEST_CASE(record_prepared_repeated_headers_charge_hash_work)
{
    REQUIRE(table());
    const pin_record *record = write_plan->types[0].record;
    pin_value header = { .type = PIN_STRUCT,
                         .type_name = record->type.name,
                         .record = record };
    for (int i = 0; i < 24; i++) {
        prepared_value(&writer, &header);
    }
    REQUIRE(!writer.wire.error);
    for (int sufficient = 0; sufficient < 2; sufficient++) {
        REQUIRE(read_table());
        reader.record_work_limit = sufficient ? 32 * 1024 : 8 * 1024;
        int count = 0;
        for (; count < 24 && !reader.wire.error; count++) {
            pin_value decoded = { 0 };
            prepared_value(&reader, &decoded);
            if (!reader.wire.error) {
                REQUIRE(decoded.record == read_plan->types[0].record);
                REQUIRE(!decoded.ptr && pin_record_valid(decoded.record));
            }
        }
        if (sufficient) {
            REQUIRE(count == 24 && !reader.wire.error);
            REQUIRE(reader.wire.offset == reader.wire.size);
            REQUIRE(prepared_records_finalize(&reader));
        } else {
            REQUIRE(count < 24 && reader.wire.error);
            REQUIRE(strstr(reader.wire.error, "work limit"));
        }
        free(reader.records);
        reader = (prepared_context){ 0 };
        execution_module_plan_free(read_plan);
        read_plan = NULL;
    }
    return true;
}
static bool table_image(int mode)
{
    execution_wire *w = &writer.wire;
    const pin_record *r = write_plan->types[0].record;
    afree(w->data);
    *w = (execution_wire){ .storage = &write_plan->storage };
    int types = mode == 8 || mode == 9 ? 2 : 1;
    execution_wire_count(w, &types, 1);
    for (int t = 0; t < types; t++) {
        uint64_t id = mode == 0 ? 0 : 91 + (mode == 9 && t ? 1 : 0);
        uint64_t layout = r->layout + (mode == 7);
        if (mode == 5) {
            type_field duplicate[12];
            memcpy(duplicate, r->type.fields, sizeof duplicate);
            duplicate[1].name = duplicate[0].name;
            type_desc type = r->type;
            type.fields = duplicate;
            layout = type_layout_hash(&type);
            REQUIRE(layout);
        }
        int kind = mode == 1 ? PIN_BOOL : PIN_STRUCT;
        const char *name = mode == 8 && t ? "Rows.Other" : "Rows.Row";
        size_t bytes = r->type.size + (mode == 6);
        execution_wire_u64(w, &id);
        execution_wire_u64(w, &layout);
        execution_wire_int(w, &kind);
        execution_wire_string(w, &name);
        execution_wire_size(w, &bytes);
        uint64_t flag = mode == 2 ? 2 : 1;
        execution_wire_u64(w, &flag);
        int count = mode == 3 ? 0 : 12;
        execution_wire_count(w, &count, 2);
        for (int i = 0; i < count; i++) {
            name = mode == 5 && i == 1 ? fields[0].name : fields[i].name;
            kind = mode == 4 && i == 0 ? PIN_STRUCT : fields[i].type;
            execution_wire_string(w, &name);
            execution_wire_int(w, &kind);
            const char *type_name = r->type.fields[i].type_name;
            const char *unit = r->type.fields[i].unit;
            execution_wire_string(w, &type_name);
            execution_wire_string(w, &unit);
            bool descriptor = kind == PIN_STR;
            if ((mode == 12 && i == 0) || (mode == 13 && i == 5)) {
                descriptor = !descriptor;
            }
            execution_wire_bool(w, &descriptor);
        }
    }
    return true;
}
TEST_CASE(record_prepared_table_rejections)
{
    REQUIRE(table());
    for (int mode = 0; mode < 14; mode++) {
        REQUIRE(table_image(mode));
        REQUIRE(!writer.wire.error);
        read_plan = execution_module_plan_new();
        reader = (prepared_context){ .plan = read_plan,
                                     .record_work_limit = mode == 11 ? 1 : 0 };
        reader.wire = (execution_wire){ .reading = true,
                                        .data = writer.wire.data,
                                        .size = writer.wire.size - (mode == 10),
                                        .storage = &read_plan->storage };
        prepared_types(&reader);
        REQUIRE(reader.wire.error);
        free(reader.records);
        reader = (prepared_context){ 0 };
        execution_module_plan_free(read_plan);
        read_plan = NULL;
    }
    /* Four invalid owned value headers after one valid type table. */
    for (int mode = 0; mode < 4; mode++) {
        REQUIRE(table_image(-1));
        execution_wire *w = &writer.wire;
        int type = mode == 0 ? PIN_BOOL : PIN_STRUCT;
        int container = mode == 3 ? PIN_CONTAINER_ARRAY : PIN_CONTAINER_SINGLE;
        const char *name = mode == 2 ? "Missing.Row" : "Rows.Row";
        uint64_t authored = mode == 1 ? 0 : 1;
        execution_wire_int(w, &type);
        execution_wire_int(w, &container);
        execution_wire_string(w, &name);
        execution_wire_u64(w, &authored);
        if (mode == 3) {
            uint64_t present = 1;
            int count = 1;
            execution_wire_u64(w, &present);
            execution_wire_count(w, &count, 1);
            type = PIN_STRUCT;
            container = PIN_CONTAINER_SINGLE;
            authored = 0;
            execution_wire_int(w, &type);
            execution_wire_int(w, &container);
            execution_wire_string(w, &name);
            execution_wire_u64(w, &authored);
        }
        REQUIRE(read_table());
        pin_value value = { 0 };
        prepared_value(&reader, &value);
        REQUIRE(reader.wire.error);
        free(reader.records);
        reader = (prepared_context){ 0 };
        execution_module_plan_free(read_plan);
        read_plan = NULL;
    }
    /* A module requirement and one distinct function layout fit this bound;
     * a second descriptor must pay for its own validation even when its
     * nominal identity and layout hash match the first. */
    for (int mode = 0; mode < 3; mode++) {
        REQUIRE(table_image(-1));
        REQUIRE(read_table());
        const pin_record *record = read_plan->types[0].record;
        size_t names = record->bytes - sizeof *record -
                       (size_t)record->type.field_count * sizeof(type_field);
        size_t cost = ((size_t)record->type.field_count + 8) *
                      (names + (size_t)record->type.field_count + 1);
        REQUIRE(decoded_headers(record->type.fields, record->type.field_count));
        if (mode == 1) {
            pin_value *later = &decoded_function->data_values[1].initial;
            const pin_record *other = pin_record_create(
                &decoded_function->owned,
                record->type.name,
                record->id,
                record->type.fields,
                record->type.field_count
            );
            REQUIRE(
                other && other != later->record && pin_record_same(other, later->record)
            );
            later->record = other;
            later->type_name = other->type.name;
        }
        apush(reader.functions, decoded_function);
        reader.record_work_limit = reader.record_work + 2 * cost + cost / 2;
        size_t remaining = 1;
        if (mode == 2) {
            reader.wire.storage_budget = &remaining;
        }
        bool within = prepared_records_finalize(&reader);
        if (mode == 1) {
            REQUIRE(!within && reader.wire.error && strstr(reader.wire.error, "work limit"));
        } else if (mode == 2) {
            REQUIRE(!within && reader.wire.error && strstr(reader.wire.error, "storage budget"));
        } else {
            REQUIRE(within && !reader.wire.error);
        }
        behavior_function_free(decoded_function);
        decoded_function = NULL;
        afree(reader.functions);
        free(reader.records);
        reader = (prepared_context){ 0 };
        execution_module_plan_free(read_plan);
        read_plan = NULL;
    }
    REQUIRE(table_image(-1));
    pin_value null_row = { .type = PIN_STRUCT,
                           .type_name = write_plan->types[0].name,
                           .record = write_plan->types[0].record };
    prepared_value(&writer, &null_row);
    REQUIRE(!writer.wire.error && read_table());
    arena_init(&storage, 256);
    size_t capacity_before = arena_storage_bytes(&storage);
    size_t header_end = reader.wire.offset + 4 + strlen(null_row.type_name);
    size_t remaining = 1;
    reader.wire.storage = &storage;
    reader.wire.storage_budget = &remaining;
    pin_value decoded = { 0 };
    prepared_value(&reader, &decoded);
    REQUIRE(reader.wire.error && strstr(reader.wire.error, "aggregate storage budget"));
    REQUIRE(remaining == 1 && !decoded.record && !decoded.ptr);
    REQUIRE(reader.wire.offset == header_end);
    REQUIRE(arena_storage_bytes(&storage) == capacity_before);
    REQUIRE(!arena_contains(&storage, read_plan->types[0].record));
    return true;
}
static bool legacy_bytes(pin_value value, const unsigned char *expected, size_t count)
{
    execution_wire *w = &writer.wire;
    afree(w->data);
    *w = (execution_wire){ 0 };
    execution_wire_value(w, &value);
    REQUIRE(!w->error && w->size == count && !memcmp(w->data, expected, count));
    execution_wire read = { .reading = true,
                            .data = w->data,
                            .size = w->size,
                            .storage = &producer };
    pin_value copy = { 0 };
    execution_wire_value(&read, &copy);
    REQUIRE(!read.error && read.offset == read.size && pin_value_equal(&value, &copy));
    return true;
}
static bool rejected_result_reader(behavior_function *f, bool bad_name)
{
    afree(writer.wire.data);
    free(writer.records);
    writer = (prepared_context){ .plan = &module->plan };
    writer.wire.storage = &module->plan.storage;
    prepared_types(&writer);
    REQUIRE(!writer.wire.error);
    behavior_function_result original = f->results[0];
    type_desc wrong = *original.type;
    wrong.name = "Rows.Wrong";
    if (bad_name) {
        f->results[0].type = &wrong;
    } else {
        f->results[0].index = f->result_count;
    }
    prepared_function(&writer, f);
    f->results[0] = original;
    REQUIRE(
        writer.wire.error &&
        strstr(writer.wire.error, bad_name ? "result layout" : "result index")
    );
    REQUIRE(read_table());
    decoded_function = calloc(1, sizeof *decoded_function);
    REQUIRE(decoded_function);
    decoded_function->artifact = execution_artifact_new();
    reader.wire.storage = &decoded_function->owned;
    prepared_function(&reader, decoded_function);
    REQUIRE(
        reader.wire.error &&
        strstr(reader.wire.error, bad_name ? "result layout" : "result index")
    );
    behavior_function_free(decoded_function);
    decoded_function = NULL;
    free(reader.records);
    reader = (prepared_context){ 0 };
    execution_module_plan_free(read_plan);
    read_plan = NULL;
    return true;
}
TEST_CASE(record_prepared_legacy_and_failure_atomicity)
{
    const unsigned char integer[] = { 12, 0, 0, 17 };
    const unsigned char string[] = { 26, 0, 0, 1, 'a' };
    const unsigned char array_bytes[] = { 12, 2, 0,  1, 4, 12, 0,
                                          0,  1, 12, 0, 0, 2 };
    const unsigned char union_bytes[] = { 36, 0, 0, 1, 7, 2, 2, 0, 0, 1 };
    REQUIRE(legacy_bytes((pin_value){ .type = PIN_I32, .i = 17 }, integer, sizeof integer));
    REQUIRE(legacy_bytes(text_value("a"), string, sizeof string));
    pin_value items[] = { { .type = PIN_I32, .i = 1 }, { .type = PIN_I32, .i = 2 } };
    pin_array array = { .element_type = PIN_I32, .items = items, .count = 2, .capacity = 2 };
    REQUIRE(legacy_bytes(
        (pin_value){ .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = &array },
        array_bytes,
        sizeof array_bytes
    ));
    pin_union *u = arena_alloc(&producer, sizeof *u + sizeof(pin_value));
    *u = (pin_union){ .tag = 7, .count = 1 };
    u->values[0] = (pin_value){ .type = PIN_BOOL, .b = true };
    REQUIRE(legacy_bytes((pin_value){ .type = PIN_UNION, .ptr = u }, union_bytes, sizeof union_bytes));
    const type_field native_field = { .name = "value",
                                      .type = PIN_I32,
                                      .size = sizeof(int32_t) };
    const type_desc native_type = { .name = "Native",
                                    .size = sizeof(int32_t),
                                    .fields = &native_field,
                                    .field_count = 1 };
    const type_desc *native_types[] = { &native_type };
    const type_catalog_pack pack = { .types = native_types, .type_count = 1 };
    type_catalog_set_project(&pack);
    project_set = true;
    /* Pre-change native struct recipe: tag/container/name, layout, present,
     * count, scalar field. It has no authored discriminator. */
    execution_wire reference = { 0 };
    int tag = PIN_STRUCT, container = PIN_CONTAINER_SINGLE, count = 1;
    const char *name = "Native";
    uint64_t layout = type_layout_hash(&native_type), present = 1;
    execution_wire_int(&reference, &tag);
    execution_wire_int(&reference, &container);
    execution_wire_string(&reference, &name);
    execution_wire_u64(&reference, &layout);
    execution_wire_u64(&reference, &present);
    execution_wire_count(&reference, &count, 3);
    tag = PIN_I32;
    name = NULL;
    uint64_t bits = 5;
    execution_wire_int(&reference, &tag);
    execution_wire_int(&reference, &container);
    execution_wire_string(&reference, &name);
    execution_wire_u64(&reference, &bits);
    int32_t native_value = 5;
    bool native_ok = legacy_bytes(
        (pin_value){ .type = PIN_STRUCT, .type_name = "Native", .ptr = &native_value },
        reference.data,
        reference.size
    );
    afree(reference.data);
    REQUIRE(native_ok);
    type_catalog_set_project(NULL);
    project_set = false;
    static const char
        source[] = "Rows :: library { " ROW
                   "Make :: () -> Row { return {key: \"k\", label: \"L\"}; } }";
    REQUIRE(compile(source, BEHAVIOR_BYTECODE));
    /* The checked writer must fail atomically on an invalid result-buffer index. */
    behavior_function *f = (behavior_function *)body();
    REQUIRE(f && acount(f->results) == 1);
    int original = f->results[0].index;
    f->results[0].index = f->result_count;
    unsigned char *bad = NULL;
    size_t length = 0;
    char error[256];
    bool encoded = execution_prepared_write(module, &bad, &length, error, sizeof error);
    f->results[0].index = original;
    REQUIRE(!encoded && !bad && !length && strstr(error, "result index"));
    REQUIRE(rejected_result_reader(f, false));
    REQUIRE(rejected_result_reader(f, true));
    REQUIRE(encode());
    execution_wire inspect = { .data = image, .size = image_size, .reading = true };
    uint64_t value = 0;
    execution_wire_u64(&inspect, &value);
    size_t version = inspect.offset;
    execution_wire_u64(&inspect, &value);
    REQUIRE(value == EXECUTION_PREPARED_VERSION);
    size_t pointer = inspect.offset;
    /* Modes 3, 4 and 5 bound one module's decoded storage, its record-layout
     * work and its records below what this module needs. */
    static const char *const failures[] = {
        "rebuild prepared code",    "target ABI",
        "aggregate storage budget", "decoded storage limit",
        "layout work limit",        "record"
    };
    for (int mode = 0; mode < 6; mode++) {
        input = malloc(image_size);
        REQUIRE(input);
        memcpy(input, image, image_size);
        if (mode == 0) {
            input[version] = EXECUTION_PREPARED_VERSION - 1;
        }
        if (mode == 1) {
            input[pointer] = sizeof(void *) == 8 ? 4 : 8;
        }
        size_t remaining = mode == 2 ? sizeof(execution_module_plan) +
                                           sizeof(execution_module) + 128
                                     : EXECUTION_WIRE_MAX_BYTES;
        size_t before = remaining;
        execution_prepared_linker bounded = { .decoded_bytes = mode == 3 ? 1024 : 0,
                                              .decoded_records = mode == 5 ? 1 : 0,
                                              .layout_work = mode == 4 ? 1 : 0 };
        module = execution_prepared_read_budget(
            input,
            image_size,
            mode >= 3 ? &bounded : NULL,
            BEHAVIOR_BYTECODE,
            &remaining,
            error,
            sizeof error
        );
        REQUIRE(!module && remaining == before && *error);
        REQUIRE(strstr(error, failures[mode]));
        free(input);
        input = NULL;
    }
    /* Bounds above the defaults admit the same module. */
    size_t remaining = 2 * (size_t)EXECUTION_WIRE_MAX_BYTES;
    execution_prepared_linker generous = {
        .decoded_bytes = 2 * (size_t)EXECUTION_WIRE_MAX_BYTES,
        .decoded_records = 2 * (size_t)EXECUTION_WIRE_MAX_RECORDS,
        .layout_work = 2 * (size_t)EXECUTION_WIRE_MAX_BYTES
    };
    module = execution_prepared_read_budget(
        image,
        image_size,
        &generous,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    REQUIRE(module);
    return true;
}
/* A wire's record bound replaces the default, above it as below it, up to
 * the largest count. */
TEST_CASE(wire_record_limit)
{
    for (int raised = 0; raised < 2; raised++) {
        execution_wire w = { .record_limit = raised ? 2 * (size_t)EXECUTION_WIRE_MAX_RECORDS
                                                    : 0 };
        int count = EXECUTION_WIRE_MAX_RECORDS;
        execution_wire_count(&w, &count, 0);
        REQUIRE(!w.error);
        REQUIRE(execution_wire_record(&w) == (bool)raised);
        REQUIRE(!w.error == (bool)raised);
        afree(w.data);
    }
    execution_wire small = { .record_limit = 2 };
    int count = 3;
    execution_wire_count(&small, &count, 0);
    REQUIRE(small.error && strstr(small.error, "record count limit"));
    afree(small.data);
    /* The largest bound admits its last record and refuses the next without
     * wrapping the count. */
    execution_wire full = { .record_limit = SIZE_MAX, .records = SIZE_MAX - 1 };
    REQUIRE(execution_wire_record(&full) && !full.error && full.records == SIZE_MAX);
    REQUIRE(!execution_wire_record(&full) && full.error && full.records == SIZE_MAX);
    afree(full.data);
    return true;
}
TEST_CASE(string_wire_bytes)
{
    static const char bytes[] = { 'A', 0, 'B', (char)0xff };
    pin_value value;
    REQUIRE(!pin_value_text(&producer, &value, bytes, sizeof bytes));
    execution_wire encoded = { 0 };
    execution_wire_value(&encoded, &value);
    REQUIRE(!encoded.error);
    execution_wire decoded = { .reading = true,
                               .data = encoded.data,
                               .size = encoded.size,
                               .storage = &escaped };
    pin_value output = { 0 };
    execution_wire_value(&decoded, &output);
    REQUIRE(!decoded.error && decoded.offset == decoded.size);
    arena_free(&producer);
    REQUIRE(pin_text(output).length == sizeof bytes);
    REQUIRE(!memcmp(pin_text(output).bytes, bytes, sizeof bytes));
    size_t text_bytes = sizeof(pin_string) + sizeof bytes + 1;
    REQUIRE(!output.type_name && decoded.owned_bytes == text_bytes);
    pin_value previous = output;
    execution_wire constrained = { .reading = true,
                                   .data = encoded.data,
                                   .size = encoded.size,
                                   .storage = &escaped,
                                   .owned_limit = text_bytes - 1 };
    execution_wire_value(&constrained, &output);
    REQUIRE(constrained.error && !constrained.owned_bytes);
    REQUIRE(
        output.type == previous.type && output.text == previous.text &&
        output.container == previous.container &&
        output.type_name == previous.type_name && output.record == previous.record
    );
    pin_value alias = previous;
    alias.type_name = "TextAlias";
    execution_wire named = { 0 };
    execution_wire_value(&named, &alias);
    REQUIRE(!named.error);
    execution_wire named_read = { .reading = true,
                                  .data = named.data,
                                  .size = named.size,
                                  .storage = &escaped };
    execution_wire_value(&named_read, &output);
    REQUIRE(!named_read.error && named_read.offset == named_read.size);
    REQUIRE(
        output.type_name != alias.type_name && !strcmp(output.type_name, alias.type_name)
    );
    REQUIRE(pin_text_equal(pin_text(output), pin_text(previous)));
    REQUIRE(named_read.owned_bytes == sizeof "TextAlias" + text_bytes);
    afree(named.data);
    unsigned char metadata[] = { 24, 0, 0, 4, 'A', 0, 'B' };
    execution_wire invalid = { .reading = true,
                               .data = metadata,
                               .size = sizeof metadata,
                               .storage = &escaped };
    execution_wire_value(&invalid, &output);
    REQUIRE(invalid.error && strstr(invalid.error, "embedded NUL"));
    /* Pretend earlier fields consumed the byte budget. A hash sink avoids
     * allocating a large buffer while a short native string hits the bound. */
    char native_bytes[81];
    memset(native_bytes, 'x', sizeof native_bytes - 1);
    native_bytes[sizeof native_bytes - 1] = 0;
    const char *native_text = native_bytes;
    const type_field native_field = { .name = "text",
                                      .type = PIN_STR,
                                      .type_name = "NativeAlias",
                                      .size = sizeof(char *) };
    const type_desc native_type = { .name = "WireNativeText",
                                    .size = sizeof(char *),
                                    .fields = &native_field,
                                    .field_count = 1 };
    const type_desc *native_types[] = { &native_type };
    const type_catalog_pack catalog = { .types = native_types, .type_count = 1 };
    type_catalog_set_project(&catalog);
    project_set = true;
    pin_value native_value = { .type = PIN_STRUCT,
                               .type_name = native_type.name,
                               .ptr = &native_text };
    uint64_t hash = UINT64_C(1469598103934665603);
    execution_wire full = { .size = EXECUTION_WIRE_MAX_BYTES - 64, .hash = &hash };
    execution_wire_value(&full, &native_value);
    REQUIRE(full.error && !strcmp(full.error, "Prepared text exceeds remaining byte limit"));
    REQUIRE(full.size <= EXECUTION_WIRE_MAX_BYTES && !full.data && !full.owned_bytes);
    native_text = "ok";
    execution_wire native_encoded = { 0 };
    execution_wire_value(&native_encoded, &native_value);
    REQUIRE(!native_encoded.error);
    execution_wire native_read = { .reading = true,
                                   .data = native_encoded.data,
                                   .size = native_encoded.size,
                                   .storage = &escaped };
    execution_wire_value(&native_read, &output);
    REQUIRE(!native_read.error && native_read.offset == native_read.size);
    REQUIRE(output.type_name && !strcmp(output.type_name, native_type.name));
    REQUIRE(!strcmp(*(const char *const *)output.ptr, "ok"));
    afree(native_encoded.data);
    type_catalog_set_project(NULL);
    project_set = false;
    afree(encoded.data);
    return true;
}
void test_language_record_prepared_runtime()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(wire_record_limit);
    RUN_TEST_CASE(string_wire_bytes);
    RUN_TEST_CASE(record_prepared_scalar_owners);
    RUN_TEST_CASE(record_prepared_array_owners);
    RUN_TEST_CASE(record_prepared_shared_headers_fit_default_work);
    RUN_TEST_CASE(record_prepared_finds_header_types_by_name);
    RUN_TEST_CASE(record_prepared_proof_keeps_each_header_check);
    RUN_TEST_CASE(record_prepared_failed_proofs_do_not_survive_retry);
    RUN_TEST_CASE(record_prepared_proofs_do_not_outlive_native_catalog);
    RUN_TEST_CASE(record_prepared_recursive_codec);
    RUN_TEST_CASE(record_prepared_repeated_headers_charge_hash_work);
    RUN_TEST_CASE(record_prepared_packed_rows_survive_collection);
    RUN_TEST_CASE(record_prepared_table_rejections);
    RUN_TEST_CASE(record_prepared_legacy_and_failure_atomicity);
    pu_clear_setup();
}
