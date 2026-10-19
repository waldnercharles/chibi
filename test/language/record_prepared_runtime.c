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
#include "types/catalog.h"
#include "types/record.h"
#include "types/trace.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Five fixed prepared cases. Bounds are recorded beside the dispatcher before
 * independent admission. No files, frames, worker pools or benchmark loops. */
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
    pin_read_field(row->ptr, &row->record->type.fields[index], &field);
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
    static const char source[] = "library Rows { " ROW
                                 "Make :: (text: string) -> Row { return {key: "
                                 "text, label: text}; } }";
    REQUIRE(compile(source, BEHAVIOR_BYTECODE));
    REQUIRE(encode());
    for (int b = 0; b < 2; b++) {
        REQUIRE(decode(backends[b]));
        char text[] = "prepared-owner";
        pin_value arg = { .type = PIN_STR, .str = text };
        REQUIRE(run(backends[b], &arg, 1));
        REQUIRE(result.record && arena_contains(&storage, result.record));
        memset(text, 'x', sizeof text - 1);
        REQUIRE(!strcmp(member(&result, 0).str, "prepared-owner"));
        REQUIRE(!strcmp(member(&result, 2).str, "node"));
        REQUIRE(!pin_value_snapshot_to_arena(&escaped, &saved, &result));
        finish_call();
        execution_module_free(module);
        module = NULL;
        REQUIRE(pin_record_valid(saved.record));
        REQUIRE(!strcmp(member(&saved, 1).str, "prepared-owner"));
        arena_free(&escaped);
        saved = (pin_value){ 0 };
    }
    return true;
}
TEST_CASE(record_prepared_array_owners)
{
    static const char source
        [] = "library Rows { " ROW
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
                REQUIRE(a->items[j].record == a->record);
                REQUIRE(!strcmp(member(&a->items[j], 1).str, "A"));
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
static bool table()
{
    write_plan = execution_module_plan_new();
    const pin_record *r = pin_record_create(&write_plan->storage, "Rows.Row", 91, fields, 12);
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
        pin_write_field(row.ptr, &r->type.fields[i], (pin_value){ .type = PIN_STR, .str = text });
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
        REQUIRE(rows->items[i].record == owned);
    }
    pin_value raw = copy->values[0];
    collect(&read_plan->storage, raw.ptr);
    REQUIRE(pin_record_valid(raw.record));
    for (int i = 0; i < 5; i++) {
        REQUIRE(!strcmp(member(&raw, i).str, "owned!"));
    }
    collect(&read_plan->storage, NULL);
    size_t bytes, capacity;
    arena_usage(&read_plan->storage, &bytes, &capacity);
    REQUIRE(bytes == 0);
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
        }
    }
    return true;
}
TEST_CASE(record_prepared_table_rejections)
{
    REQUIRE(table());
    for (int mode = 0; mode < 12; mode++) {
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
    REQUIRE(table_image(-1));
    REQUIRE(read_table());
    const pin_record *record = read_plan->types[0].record;
    size_t names = record->bytes - sizeof *record -
                   (size_t)record->type.field_count * sizeof(type_field);
    size_t cost = ((size_t)record->type.field_count + 8) *
                  (names + (size_t)record->type.field_count + 1);
    behavior_function repeated = { 0 };
    pin_value header = { .type = PIN_STRUCT,
                         .type_name = record->type.name,
                         .record = record };
    apush(repeated.argument_defaults, header);
    apush(repeated.argument_defaults, header);
    apush(reader.functions, &repeated);
    size_t work_before = reader.record_work;
    reader.record_work_limit = work_before + 2 * cost + cost / 2;
    bool within = prepared_records_finalize(&reader);
    afree(repeated.argument_defaults);
    afree(reader.functions);
    REQUIRE(!within && reader.wire.error && strstr(reader.wire.error, "work limit"));
    REQUIRE(
        reader.record_work ==
        work_before + 2 * cost + 2 * (2 * strlen(header.type_name) + 3)
    );
    free(reader.records);
    reader = (prepared_context){ 0 };
    execution_module_plan_free(read_plan);
    read_plan = NULL;
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
    const unsigned char string[] = { 26, 0, 0, 2, 'a' };
    const unsigned char array_bytes[] = { 12, 2, 0,  1, 4, 12, 0,
                                          0,  1, 12, 0, 0, 2 };
    const unsigned char union_bytes[] = { 36, 0, 0, 1, 7, 2, 2, 0, 0, 1 };
    REQUIRE(legacy_bytes((pin_value){ .type = PIN_I32, .i = 17 }, integer, sizeof integer));
    REQUIRE(legacy_bytes((pin_value){ .type = PIN_STR, .str = "a" }, string, sizeof string));
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
        source[] = "library Rows { " ROW
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
    /* Modes 3 and 4 bound one module's decoded storage and its record-layout
     * work below what this module needs. */
    static const char *const failures[] = { "rebuild prepared code",
                                            "target ABI",
                                            "aggregate storage budget",
                                            "decoded storage limit",
                                            "layout work limit" };
    for (int mode = 0; mode < 5; mode++) {
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
void test_language_record_prepared_runtime()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_prepared_scalar_owners);
    RUN_TEST_CASE(record_prepared_array_owners);
    RUN_TEST_CASE(record_prepared_recursive_codec);
    RUN_TEST_CASE(record_prepared_table_rejections);
    RUN_TEST_CASE(record_prepared_legacy_and_failure_atomicity);
    pu_clear_setup();
}
