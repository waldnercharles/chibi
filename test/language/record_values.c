#include "runtime/arrays.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/trace.h"
#include "types/value_document.h"
#include "types/value_wire.h"

#include <stdio.h>
#include <string.h>

/* Six fixed C cases. No authored compiler, backend dispatch, I/O, worker pool,
 * environment-controlled size or timing loop is exercised by this foundation. */
static arena source, owned, other;
bool record_prepared_guard(const pin_value *);
static av_doc *document;
static bool project_set;
static pin_value borrowed;
static const type_field fields[] = {
    { .name = "key", .type = PIN_STR },
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
    { .name = "renaming", .type = PIN_BOOL },
};
static void cleanup()
{
    if (project_set) {
        type_catalog_set_project(NULL);
        project_set = false;
    }
    pin_value_destroy(&borrowed);
    if (document) {
        av_doc_free(document);
    }
    document = NULL;
    arena_free(&source);
    arena_free(&owned);
    arena_free(&other);
}
static char *text(arena *storage, const char *value)
{
    size_t size = strlen(value) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, value, size);
    return copy;
}
static const pin_record *row_layout(arena *storage, uint64_t id)
{
    type_field dynamic[12];
    for (int i = 0; i < 12; i++) {
        dynamic[i] = fields[i];
        dynamic[i].name = text(storage, fields[i].name);
    }
    return pin_record_create(storage, text(storage, "Rows.OutlinerRow"), id, dynamic, 12);
}
static pin_value row(arena *storage, const pin_record *record, int number)
{
    pin_value result = { .type = PIN_STRUCT,
                         .type_name = record->type.name,
                         .record = record,
                         .ptr = arena_alloc(storage, record->type.size) };
    memset(result.ptr, 0xa5, record->type.size);
    for (int i = 0; i < 12; i++) {
        pin_value field = { .type = fields[i].type };
        if (field.type == PIN_STR) {
            char buffer[32];
            snprintf(buffer, sizeof buffer, "row-%d-field-%d", number, i);
            field.str = text(storage, buffer);
        } else if (field.type == PIN_I32) {
            field.i = number + i;
        } else {
            field.b = (i + number) % 2;
        }
        pin_write_field(result.ptr, &record->type.fields[i], field);
    }
    return result;
}
static pin_value member(const pin_value *value, int index)
{
    pin_value field;
    pin_read_field(value->ptr, &value->record->type.fields[index], &field);
    return field;
}
static void collect(arena *storage, const void *root)
{
    arena_collection collection;
    arena *arenas[] = { storage };
    arena_collect_begin(&collection, arenas, 1);
    arena_collect_mark(&collection, root);
    arena_collect_end(&collection);
}
static size_t row_text_bytes(const pin_value *value)
{
    size_t size = 0;
    for (int i = 0; i < 5; i++) {
        size += strlen(member(value, i).str) + 1;
    }
    return size;
}
TEST_CASE(record_layout_validation)
{
    const pin_record *record = row_layout(&source, 91);
    REQUIRE(record && pin_record_valid(record));
    REQUIRE(record->id == 91 && record->type.field_count == 12);
    REQUIRE(record->layout == type_layout_hash(&record->type));
    REQUIRE(record->type.fields[5].offset == 5 * sizeof(char *));
    REQUIRE(record->type.fields[7].offset == 5 * sizeof(char *) + 2 * sizeof(int32_t));
    REQUIRE(arena_contains(&source, record->type.fields));
    REQUIRE(arena_contains(&source, record->type.name));
    for (int i = 0; i < 12; i++) {
        REQUIRE(arena_contains(&source, record->type.fields[i].name));
    }
    type_field invalid[12];
    memcpy(invalid, fields, sizeof invalid);
    size_t before, capacity, after;
    arena_usage(&source, &before, &capacity);
    invalid[11].type = PIN_STRUCT;
    REQUIRE(!pin_record_create(&source, "Bad", 1, invalid, 12));
    invalid[11] = invalid[0];
    REQUIRE(!pin_record_create(&source, "Bad", 1, invalid, 12));
    REQUIRE(!pin_record_create(&source, "Bad", 0, fields, 12));
    REQUIRE(!pin_record_create(&source, "", 1, fields, 12));
    REQUIRE(!pin_record_create(&source, "Bad", 1, fields, 0));
    arena_usage(&source, &after, &capacity);
    REQUIRE(before == after);
    return true;
}
TEST_CASE(record_array_ownership)
{
    const pin_record *record = row_layout(&source, 91);
    pin_value rows[3] = { row(&source, record, 0),
                          row(&source, record, 1),
                          row(&source, record, 2) };
    const int counts[] = { 0, 1, 3 };
    for (int pass = 0; pass < 3; pass++) {
        arena_init_collectable(&owned);
        int count = counts[pass];
        pin_array array = { .element_type = PIN_STRUCT,
                            .type_name = record->type.name,
                            .record = record,
                            .items = rows,
                            .count = count,
                            .capacity = count };
        pin_value input = { .type = PIN_STRUCT,
                            .container = PIN_CONTAINER_ARRAY,
                            .type_name = record->type.name,
                            .record = record,
                            .ptr = &array },
                  output;
        REQUIRE(!pin_value_snapshot_to_arena(&owned, &output, &input));
        const pin_array *copy = output.ptr;
        REQUIRE(copy && copy->count == count && output.record == copy->record);
        REQUIRE(copy->record != record && pin_record_same(record, copy->record));
        size_t expected = record->bytes + sizeof(pin_array) +
                          (size_t)count * sizeof(pin_value);
        for (int i = 0; i < count; i++) {
            REQUIRE(copy->items[i].record == copy->record);
            REQUIRE(copy->items[i].type_name == copy->type_name);
            REQUIRE(pin_value_equal(&rows[i], &copy->items[i]));
            expected += record->type.size + row_text_bytes(&rows[i]);
        }
        size_t bytes, capacity;
        arena_usage(&owned, &bytes, &capacity);
        REQUIRE(bytes == expected); /* Exactly one metadata closure, no per-row copy. */
        collect(&owned, copy);
        arena_usage(&owned, &bytes, &capacity);
        REQUIRE(bytes == expected);
        if (count) {
            pin_value again;
            REQUIRE(!pin_value_snapshot_to_arena(&owned, &again, &copy->items[0]));
            REQUIRE(again.record == copy->record);
        }
        arena_free(&owned);
    }
    arena_init_collectable(&owned);
    pin_array empty = { .element_type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record };
    pin_array *retained = pin_array_snapshot_to_arena(&owned, &empty);
    REQUIRE(retained);
    arena_free(&source);
    collect(&owned, retained);
    REQUIRE(retained->count == 0 && pin_record_valid(retained->record));
    REQUIRE(!strcmp(retained->type_name, "Rows.OutlinerRow"));
    return true;
}
TEST_CASE(record_raw_trace_lifetime)
{
    arena_init_collectable(&owned);
    const pin_record *record = row_layout(&source, 91);
    pin_value input = row(&source, record, 7), output;
    REQUIRE(!pin_value_snapshot_to_arena(&owned, &output, &input));
    size_t expected = output.record->bytes + output.record->type.size +
                      row_text_bytes(&output);
    arena_free(&source);
    collect(&owned, output.ptr); /* Only raw payload roots the metadata trace shape. */
    REQUIRE(pin_record_valid(output.record));
    REQUIRE(!strcmp(member(&output, 1).str, "row-7-field-1"));
    size_t bytes, capacity;
    arena_usage(&owned, &bytes, &capacity);
    REQUIRE(bytes == expected);
    for (int i = 0; i < 16; i++) {
        pin_value next;
        REQUIRE(!pin_value_snapshot_to_arena(&owned, &next, &output));
        output = next;
        collect(&owned, output.ptr);
        arena_usage(&owned, &bytes, &capacity);
        REQUIRE(bytes == expected);
    }
    collect(&owned, NULL);
    arena_usage(&owned, &bytes, &capacity);
    REQUIRE(bytes == 0);
    return true;
}
TEST_CASE(record_identity_and_borrow)
{
    const pin_record *record = row_layout(&source, 91);
    const pin_record *clone = pin_record_snapshot(&other, record);
    REQUIRE(pin_record_same(record, clone));
    pin_value input = row(&source, record, 1), equal = row(&other, clone, 1);
    REQUIRE(pin_value_equal(&input, &equal));
    REQUIRE(!pin_value_copy(&borrowed, &input));
    REQUIRE(borrowed.ptr == input.ptr && borrowed.record == record);
    pin_value_destroy(&borrowed);
    REQUIRE(!strcmp(member(&input, 1).str, "row-1-field-1"));
    pin_value shallow;
    REQUIRE(!pin_value_copy_to_arena(&owned, &shallow, &input));
    REQUIRE(shallow.ptr == input.ptr && shallow.record == record);
    pin_value saved;
    REQUIRE(!pin_value_snapshot_to_arena(&owned, &saved, &input));
    pin_write_field(input.ptr, &record->type.fields[1], (pin_value){ .type = PIN_STR, .str = "changed" });
    REQUIRE(!pin_value_equal(&input, &saved));
    REQUIRE(pin_value_equal(&equal, &saved));
    type_field changed[12];
    memcpy(changed, fields, sizeof changed);
    changed[5].type = PIN_BOOL;
    const pin_record *version = pin_record_create(&other, "Rows.OutlinerRow", 91, changed, 12);
    const pin_record *distinct = row_layout(&other, 92);
    REQUIRE(!pin_record_same(record, version));
    REQUIRE(!pin_record_same(record, distinct));
    pin_array a = { .element_type = PIN_STRUCT,
                    .type_name = record->type.name,
                    .record = record };
    pin_array b = { .element_type = PIN_STRUCT,
                    .type_name = version->type.name,
                    .record = version };
    REQUIRE(!pin_array_equal(&a, &b));
    b.record = clone;
    REQUIRE(pin_array_equal(&a, &b));
    pin_value forged = input;
    pin_record invalid = *record;
    invalid.layout ^= 1;
    forged.record = &invalid;
    pin_value sentinel = { .type = PIN_I32, .i = 123 }, output = sentinel;
    REQUIRE(pin_value_snapshot_to_arena(&owned, &output, &forged));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    pin_record *invalid_shape = (pin_record *)pin_record_snapshot(&other, record);
    invalid_shape->type.size++; /* Canonical pointers, same identity/hash/name, wrong shape. */
    forged.record = invalid_shape;
    pin_array bad_array = { .element_type = PIN_STRUCT,
                            .type_name = record->type.name,
                            .record = record,
                            .items = &forged,
                            .count = 1,
                            .capacity = 1 };
    pin_value bad_input = { .type = PIN_STRUCT,
                            .container = PIN_CONTAINER_ARRAY,
                            .type_name = record->type.name,
                            .record = record,
                            .ptr = &bad_array };
    REQUIRE(pin_value_snapshot_to_arena(&owned, &output, &bad_input));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    bad_array.items = &input;
    bad_array.record = invalid_shape;
    REQUIRE(pin_value_snapshot_to_arena(&owned, &output, &bad_input));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    bad_array.record = record;
    pin_value bad_args[2] = { bad_input, forged };
    REQUIRE(NATIVE_ARRAY_add_CALL.value(&owned, bad_args, 2, &output));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    bad_args[1] = bad_input; /* A row array cannot masquerade as one scalar row. */
    REQUIRE(NATIVE_ARRAY_add_CALL.value(&owned, bad_args, 2, &output));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    pin_record *split = arena_alloc(&owned, sizeof *split);
    *split = *record; /* Resident wrapper alone does not own producer metadata. */
    REQUIRE(!pin_record_valid(split));
    forged.record = split;
    REQUIRE(pin_value_snapshot_to_arena(&owned, &output, &forged));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    const type_field native_field = { .name = "number",
                                      .type = PIN_I32,
                                      .size = sizeof(int32_t) };
    const type_desc native_type = { .name = "Rows.OutlinerRow",
                                    .size = sizeof(int32_t),
                                    .fields = &native_field,
                                    .field_count = 1 };
    const type_desc *native_types[] = { &native_type };
    const type_catalog_pack pack = { .types = native_types, .type_count = 1 };
    type_catalog_set_project(&pack);
    project_set = true;
    int32_t number = 42;
    pin_value native = { .type = PIN_STRUCT, .type_name = native_type.name, .ptr = &number },
              native_copy;
    REQUIRE(pin_value_type(&native) == &native_type);
    REQUIRE(pin_value_type(&input) == &record->type);
    REQUIRE(!pin_value_snapshot_to_arena(&other, &native_copy, &native));
    REQUIRE(pin_value_equal(&native, &native_copy));
    REQUIRE(!pin_value_equal(&native, &input));
    type_catalog_set_project(NULL);
    project_set = false;
    REQUIRE(record_prepared_guard(&input));
    return true;
}
TEST_CASE(record_array_checked_changes)
{
    const pin_record *record = row_layout(&source, 91);
    pin_value values[3] = { row(&source, record, 0),
                            row(&source, record, 1),
                            row(&source, record, 2) };
    pin_array array = { .element_type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record,
                        .items = values,
                        .count = 2,
                        .capacity = 2 };
    pin_value inputs[3] = { { .type = PIN_STRUCT,
                              .container = PIN_CONTAINER_ARRAY,
                              .type_name = record->type.name,
                              .record = record,
                              .ptr = &array },
                            { .type = PIN_I32, .i = 3 },
                            { .type = PIN_I32, .i = 1 } };
    pin_value sentinel = { .type = PIN_I32, .i = 123 }, output = sentinel;
    size_t before, after, capacity;
    arena_usage(&owned, &before, &capacity);
    const char *error = NATIVE_ARRAY_resize_CALL.value(&owned, inputs, 2, &output);
    REQUIRE(error && !strcmp(error, "Cannot grow an authored record array without initialized rows"));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    arena_usage(&owned, &after, &capacity);
    REQUIRE(before == after && array.count == 2);
    inputs[1].i = 1;
    REQUIRE(!NATIVE_ARRAY_resize_CALL.value(&owned, inputs, 2, &output));
    REQUIRE(((pin_array *)output.ptr)->count == 1);
    REQUIRE(((pin_array *)output.ptr)->record == output.record);
    REQUIRE(!NATIVE_ARRAY_slice_CALL.value(&owned, inputs, 3, &output));
    REQUIRE(pin_value_equal(&((pin_array *)output.ptr)->items[0], &values[1]));
    REQUIRE(!NATIVE_ARRAY_clear_CALL.value(&owned, inputs, 1, &output));
    REQUIRE(((pin_array *)output.ptr)->count == 0 && output.record);
    inputs[1] = values[2];
    REQUIRE(!NATIVE_ARRAY_add_CALL.value(&owned, inputs, 2, &output));
    REQUIRE(((pin_array *)output.ptr)->count == 3);
    for (int i = 0; i < 3; i++) {
        REQUIRE(((pin_array *)output.ptr)->items[i].record == output.record);
    }
    REQUIRE(array.count == 2);
    return true;
}
TEST_CASE(record_projection_and_codec_guard)
{
    const pin_record *record = row_layout(&source, 91);
    pin_value input = row(&source, record, 4), copy;
    REQUIRE(!pin_value_snapshot_to_arena(&owned, &copy, &input));
    document = av_doc_new();
    const av_value *projected = pin_value_to_document(document, &copy);
    REQUIRE(av_is_obj(projected));
    REQUIRE(!strcmp(av_get_str(av_obj_get(projected, "label")), "row-4-field-1"));
    REQUIRE(av_get_sint(av_obj_get(projected, "depth")) == 9);
    REQUIRE(av_get_bool(av_obj_get(projected, "active")));
    execution_wire wire = { 0 };
    execution_wire_value(&wire, &copy);
    REQUIRE(wire.error && wire.size == 0 && !wire.data);
    REQUIRE(!strcmp(wire.error, "Authored record requires a prepared layout table"));
    pin_value output = { .type = PIN_I32, .i = 19 };
    char error[128];
    REQUIRE(!pin_value_read_document(&other, copy, projected, &output, error, sizeof error));
    REQUIRE(pin_value_equal(&output, &copy));
    REQUIRE(output.type == PIN_STRUCT && output.container == PIN_CONTAINER_SINGLE);
    REQUIRE(pin_record_same(output.record, copy.record));
    REQUIRE(output.type_name == output.record->type.name);
    REQUIRE(
        arena_contains(&other, output.record) && arena_contains(&other, output.ptr)
    );
    arena_free(&source);
    arena_free(&owned);
    for (int i = 0; i < 12; i++) {
        pin_value field = member(&output, i);
        if (i < 5) {
            char expected[32];
            snprintf(expected, sizeof expected, "row-4-field-%d", i);
            REQUIRE(field.str && !strcmp(field.str, expected));
            REQUIRE(arena_contains(&other, field.str));
        } else if (i < 7) {
            REQUIRE(field.i == 4 + i);
        } else {
            REQUIRE(field.b == (bool)((4 + i) % 2));
        }
    }
    REQUIRE(!strcmp(av_get_str(av_obj_get(projected, "key")), "row-4-field-0"));
    return true;
}
void test_language_record_values()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_layout_validation);
    RUN_TEST_CASE(record_array_ownership);
    RUN_TEST_CASE(record_raw_trace_lifetime);
    RUN_TEST_CASE(record_identity_and_borrow);
    RUN_TEST_CASE(record_array_checked_changes);
    RUN_TEST_CASE(record_projection_and_codec_guard);
    pu_clear_setup();
}
