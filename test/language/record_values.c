#include "execution/intrinsics.h"
#include "runtime/arrays.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"
#include "types/text.h"
#include "types/trace.h"
#include "types/value_document.h"
#include "types/value_wire.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Fixed C cases. No authored compiler, backend dispatch, I/O, worker pool,
 * environment-controlled size or timing loop is exercised by this foundation. */
static arena source, owned, other, reads;
bool record_prepared_guard(const pin_value *);
bool record_prepared_type_table();
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
    arena_free(&reads);
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
            const char *error = pin_value_text(storage, &field, buffer, strlen(buffer));
            assert(!error);
            (void)error;
        } else if (field.type == PIN_I32) {
            field.i = number + i;
        } else {
            field.b = (i + number) % 2;
        }
        const char *error = pin_write_field(storage, result.ptr, &record->type.fields[i], field);
        assert(!error);
        (void)error;
    }
    return result;
}
static pin_value member(const pin_value *value, int index)
{
    pin_value field;
    if (pin_read_field(&reads, value->ptr, &value->record->type.fields[index], &field)) {
        return (pin_value){ 0 };
    }
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
        size += pin_text_footprint(pin_text(member(value, i)).length);
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
            REQUIRE(pin_array_get(copy, i).record == copy->record);
            REQUIRE(pin_array_get(copy, i).type_name == copy->type_name);
            pin_value item = pin_array_get(copy, i);
            REQUIRE(pin_value_equal(&rows[i], &item));
            expected += record->type.size + row_text_bytes(&rows[i]);
        }
        size_t bytes, capacity;
        arena_usage(&owned, &bytes, &capacity);
        REQUIRE(bytes == expected); /* Exactly one metadata closure, no per-row copy. */
        collect(&owned, copy);
        arena_usage(&owned, &bytes, &capacity);
        REQUIRE(bytes == expected);
        if (count) {
            pin_value again, item = pin_array_get(copy, 0);
            REQUIRE(!pin_value_snapshot_to_arena(&owned, &again, &item));
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
    REQUIRE(!strcmp(pin_text(member(&output, 1)).bytes, "row-7-field-1"));
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
/* A copied descriptor owns every name it holds, field types and units too. */
TEST_CASE(record_copy_owns_field_type_names_and_units)
{
    const type_field typed[] = {
        { .name = "owner", .type = PIN_HANDLE, .type_name = "ecs.entity" },
        { .name = "speed", .type = PIN_F32, .unit = "m/s" }
    };
    const pin_record *record = pin_record_create(&source, "Rows.Typed", 93, typed, 2);
    const pin_record *clone = pin_record_snapshot(&other, record);
    REQUIRE(clone && clone != record && pin_record_valid(clone));
    const type_field *owner = &clone->type.fields[0],
                     *speed = &clone->type.fields[1];
    REQUIRE(arena_contains(&other, owner->type_name) && !strcmp(owner->type_name, "ecs.entity"));
    REQUIRE(arena_contains(&other, speed->unit) && !strcmp(speed->unit, "m/s"));
    REQUIRE(!owner->unit && !speed->type_name);
    return true;
}
TEST_CASE(record_identity_and_borrow)
{
    const pin_record *record = row_layout(&source, 91);
    const pin_record *clone = pin_record_snapshot(&other, record);
    REQUIRE(pin_record_same(record, clone));
    /* The copy owns its names: it stays valid without the original. */
    REQUIRE(clone != record && pin_record_valid(clone) && clone->bytes == record->bytes);
    REQUIRE(
        arena_contains(&other, clone->type.name) &&
        !strcmp(clone->type.name, record->type.name)
    );
    for (int i = 0; i < clone->type.field_count; i++) {
        const type_field *copied = &clone->type.fields[i],
                         *original = &record->type.fields[i];
        REQUIRE(
            arena_contains(&other, copied->name) &&
            !strcmp(copied->name, original->name)
        );
        REQUIRE(copied->type == original->type && copied->offset == original->offset);
    }
    REQUIRE(pin_record_snapshot(&other, clone) == clone);
    pin_value input = row(&source, record, 1), equal = row(&other, clone, 1);
    REQUIRE(pin_value_equal(&input, &equal));
    REQUIRE(!pin_value_copy(&borrowed, &input));
    REQUIRE(borrowed.ptr == input.ptr && borrowed.record == record);
    pin_value_destroy(&borrowed);
    REQUIRE(!strcmp(pin_text(member(&input, 1)).bytes, "row-1-field-1"));
    pin_value shallow;
    REQUIRE(!pin_value_copy_to_arena(&owned, &shallow, &input));
    REQUIRE(shallow.ptr == input.ptr && shallow.record == record);
    pin_value saved;
    REQUIRE(!pin_value_snapshot_to_arena(&owned, &saved, &input));
    pin_value changed_text;
    REQUIRE(!pin_value_text(&source, &changed_text, "changed", 7));
    REQUIRE(!pin_write_field(&source, input.ptr, &record->type.fields[1], changed_text));
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
    /* Rows must carry the array's layout: a valid row does not vouch for the
     * next. */
    pin_value other_row = row(&other, distinct, 1);
    pin_value sentinel = { .type = PIN_I32, .i = 123 }, output = sentinel;
    pin_array bad_array = { .element_type = PIN_STRUCT,
                            .type_name = record->type.name,
                            .record = record,
                            .items = &other_row,
                            .count = 1,
                            .capacity = 1 };
    pin_value bad_input = { .type = PIN_STRUCT,
                            .container = PIN_CONTAINER_ARRAY,
                            .type_name = record->type.name,
                            .record = record,
                            .ptr = &bad_array };
    REQUIRE(pin_value_snapshot_to_arena(&owned, &output, &bad_input));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    pin_value mixed[2] = { input, other_row };
    bad_array.items = mixed;
    bad_array.count = bad_array.capacity = 2;
    REQUIRE(pin_value_snapshot_to_arena(&owned, &output, &bad_input));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    bad_array.items = &input;
    bad_array.count = bad_array.capacity = 1;
    pin_value bad_args[2] = { bad_input, other_row };
    REQUIRE(NATIVE_ARRAY_add_CALL.value(&owned, bad_args, 2, &output));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    bad_args[1] = bad_input; /* A row array cannot masquerade as one scalar row. */
    REQUIRE(NATIVE_ARRAY_add_CALL.value(&owned, bad_args, 2, &output));
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
/* A record lays out a nested catalog type when it is created. A project
 * catalog published later may give that name pointers at the same size, which
 * the record's bytes do not hold, so copying it fails. */
TEST_CASE(record_copy_rejects_changed_nested_layout)
{
    /* A pointer-sized integer, so both layouts have one size. */
    const pin_type word = sizeof(char *) == sizeof(int64_t) ? PIN_I64 : PIN_I32;
    const type_field plain_field = { .name = "hp", .type = word, .size = sizeof(char *) };
    const type_field text_field = { .name = "label",
                                    .type = PIN_STR,
                                    .size = sizeof(char *) };
    const type_desc plain = { .name = "Stats",
                              .size = sizeof(char *),
                              .fields = &plain_field,
                              .field_count = 1 };
    const type_desc text = { .name = "Stats",
                             .size = sizeof(char *),
                             .fields = &text_field,
                             .field_count = 1 };
    const type_desc *plain_types[] = { &plain }, *text_types[] = { &text };
    type_catalog_set_project(&(type_catalog_pack){ .types = plain_types,
                                                   .type_count = 1 });
    project_set = true;
    const type_field nested = { .name = "stats", .type = PIN_STRUCT, .type_name = "Stats" };
    const pin_record *record = pin_record_create(&source, "Rows.Holder", 93, &nested, 1);
    REQUIRE(record && pin_record_current(record));
    uintptr_t hp = 0x7a7a7a7a;
    pin_value input = { .type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = &hp },
              output;
    REQUIRE(!pin_value_snapshot_to_arena(&owned, &output, &input));
    REQUIRE(*(uintptr_t *)output.ptr == hp);
    type_catalog_set_project(NULL);
    type_catalog_set_project(&(type_catalog_pack){ .types = text_types,
                                                   .type_count = 1 });
    REQUIRE(!pin_record_current(record));
    pin_value same = input;
    REQUIRE(!pin_value_equal(&input, &same));
    pin_value operands[] = { input, same }, result = { .type = PIN_I32, .i = 123 };
    REQUIRE(EXECUTION_value_equal_call.value(&owned, operands, 2, &result));
    REQUIRE(result.type == PIN_I32 && result.i == 123);
    /* A field list fills the record's bytes under the new layout, so only the
     * stale record itself can refuse it. */
    const char *label = "label";
    pin_value header = input;
    header.ptr = NULL;
    pin_value fields[] = {
        header,
        { .type = PIN_U64, .u64 = 0 },
        { .type = PIN_STRUCT, .type_name = "Stats", .ptr = &label },
    };
    REQUIRE(EXECUTION_construct_call.value(&owned, fields, 3, &result));
    REQUIRE(result.type == PIN_I32 && result.i == 123);
    pin_value sentinel = { .type = PIN_I32, .i = 123 };
    output = sentinel;
    REQUIRE(pin_value_snapshot_to_arena(&other, &output, &input));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    pin_array rows = { .element_type = PIN_STRUCT,
                       .type_name = record->type.name,
                       .record = record,
                       .items = &input,
                       .count = 1,
                       .capacity = 1 };
    pin_value array = { .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = &rows };
    REQUIRE(pin_value_snapshot_to_arena(&other, &output, &array));
    REQUIRE(!memcmp(&output, &sentinel, sizeof output));
    return true;
}
/* Snapshots of one record into one arena share a copy until the arena resets;
 * another record at the source's address with another identity is copied. */
TEST_CASE(record_snapshot_reuses_copy_in_arena)
{
    const pin_record *record = row_layout(&source, 91);
    const pin_record *first = pin_record_snapshot(&owned, record);
    REQUIRE(first && first != record && arena_contains(&owned, first));
    REQUIRE(pin_record_snapshot(&owned, record) == first);
    arena_reset(&owned);
    const pin_record *again = pin_record_snapshot(&owned, record);
    REQUIRE(again && arena_contains(&owned, again) && pin_record_same(again, record));
    arena_reset(&source);
    const pin_record *other_record = row_layout(&source, 92);
    REQUIRE(other_record == record); /* The reset arena hands out the same address. */
    const pin_record *other_copy = pin_record_snapshot(&owned, other_record);
    REQUIRE(other_copy != again && other_copy->id == 92);
    /* One identity with a unit of the same length is still another record. */
    const type_field speed[] = { { .name = "speed", .type = PIN_F32, .unit = "m/s" } };
    const type_field rate[] = { { .name = "speed", .type = PIN_F32, .unit = "mps" } };
    arena_reset(&source);
    const pin_record *metres = pin_record_create(&source, "Rows.Speed", 93, speed, 1);
    const pin_record *first_speed = pin_record_snapshot(&owned, metres);
    arena_reset(&source);
    const pin_record *rated = pin_record_create(&source, "Rows.Speed", 93, rate, 1);
    REQUIRE(rated == metres && pin_record_same(rated, first_speed));
    const pin_record *rated_copy = pin_record_snapshot(&owned, rated);
    REQUIRE(rated_copy != first_speed && !strcmp(rated_copy->type.fields[0].unit, "mps"));
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
    pin_value sliced = pin_array_get(output.ptr, 0);
    REQUIRE(pin_value_equal(&sliced, &values[1]));
    REQUIRE(!NATIVE_ARRAY_clear_CALL.value(&owned, inputs, 1, &output));
    REQUIRE(((pin_array *)output.ptr)->count == 0 && output.record);
    inputs[1] = values[2];
    REQUIRE(!NATIVE_ARRAY_add_CALL.value(&owned, inputs, 2, &output));
    REQUIRE(((pin_array *)output.ptr)->count == 3);
    for (int i = 0; i < 3; i++) {
        REQUIRE(pin_array_get(output.ptr, i).record == output.record);
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
            REQUIRE(field.text && !strcmp(field.text->bytes, expected));
            const pin_string *stored = *(const pin_string *const
                                             *)((const char *)output.ptr +
                                                output.record->type.fields[i].offset);
            REQUIRE(
                arena_contains(&other, stored) && arena_contains(&other, stored->bytes)
            );
        } else if (i < 7) {
            REQUIRE(field.i == 4 + i);
        } else {
            REQUIRE(field.b == (bool)((4 + i) % 2));
        }
    }
    REQUIRE(!strcmp(av_get_str(av_obj_get(projected, "key")), "row-4-field-0"));
    return true;
}
TEST_CASE(text_bytes_ownership_and_c_boundary)
{
    char bytes[] = { 'a', 0, 'b', 0 };
    pin_value input, copy, prefix;
    REQUIRE(!pin_value_text(&source, &input, bytes, 3));
    REQUIRE(input.type == PIN_STR && !input.type_name);
    REQUIRE(!pin_value_snapshot_to_arena(&owned, &copy, &input));
    REQUIRE(!pin_value_text(&other, &prefix, bytes, 1));
    REQUIRE(pin_value_equal(&input, &copy));
    REQUIRE(!pin_value_equal(&copy, &prefix));
    bytes[2] = 'x';
    arena_free(&source);
    REQUIRE(pin_text(copy).length == 3 && pin_text(copy).bytes[2] == 'b');
    const char *converted = "unchanged";
    const char *unchanged = converted;
    size_t offset = SIZE_MAX;
    REQUIRE(pin_text_cstring(&other, copy, &converted, &offset));
    REQUIRE(converted == unchanged && offset == 1);
    const type_field native = { .type = PIN_STR, .size = sizeof(char *) };
    const char *field = "original";
    REQUIRE(pin_write_field(&other, &field, &native, copy));
    REQUIRE(!strcmp(field, "original"));
    const type_field member = { .name = "text", .type = PIN_STR, .size = sizeof(char *) };
    const type_desc custom = { .name = "test.unregistered.text",
                               .size = sizeof(char *),
                               .fields = &member,
                               .field_count = 1 };
    const pin_record *record = pin_record_create(&other, custom.name, 987, &member, 1);
    REQUIRE(record);
    const pin_string *stored = copy.text;
    pin_value authored = { .type = PIN_STRUCT,
                           .type_name = custom.name,
                           .record = record,
                           .ptr = &stored };
    REQUIRE(pin_write_struct(&other, &field, &custom, authored));
    REQUIRE(!strcmp(field, "original"));
    const char *native_source = "custom";
    pin_value native_record = { .type = PIN_STRUCT,
                                .type_name = custom.name,
                                .ptr = &native_source };
    REQUIRE(!pin_write_struct(&other, &field, &custom, native_record));
    REQUIRE(!strcmp(field, "custom") && field != native_source);
    native_record.type_name = "different";
    REQUIRE(pin_write_struct(&other, &field, &custom, native_record));
    REQUIRE(!strcmp(field, "custom"));
    const type_desc opaque = { .name = "test.unregistered.opaque",
                               .size = sizeof(uint64_t) };
    uint64_t opaque_source = UINT64_C(0x123456789ABCDEF0), opaque_copy = 0;
    pin_value opaque_value = { .type = PIN_STRUCT,
                               .type_name = opaque.name,
                               .ptr = &opaque_source };
    REQUIRE(!pin_write_struct(&other, &opaque_copy, &opaque, opaque_value));
    REQUIRE(opaque_copy == opaque_source);
    typedef struct
    {
        const char *text;
        uint32_t hidden;
    } partial;
    const type_field partial_field = { .name = "text",
                                       .type = PIN_STR,
                                       .offset = offsetof(partial, text),
                                       .size = sizeof(char *) };
    const type_desc partial_type = { .name = "test.unregistered.partial",
                                     .size = sizeof(partial),
                                     .fields = &partial_field,
                                     .field_count = 1 };
    char partial_text[] = "ok";
    partial partial_source = { partial_text, 37 }, partial_copy = { "before", 91 };
    pin_value partial_value = { .type = PIN_STRUCT,
                                .type_name = partial_type.name,
                                .ptr = &partial_source };
    REQUIRE(!pin_write_struct(&other, &partial_copy, &partial_type, partial_value));
    REQUIRE(partial_copy.hidden == 37 && partial_copy.text != partial_source.text);
    partial_text[0] = 'x';
    REQUIRE(!strcmp(partial_copy.text, "ok"));
    char native_bytes[] = "native";
    field = native_bytes;
    pin_value read;
    REQUIRE(!pin_read_field(&other, &field, &native, &read));
    native_bytes[0] = 'x';
    REQUIRE(pin_text_equal(pin_text(read), (pin_string){ .bytes = "native", .length = 6 }));
    const pin_string slice = { .bytes = "abc", .length = 2 };
    REQUIRE(
        !pin_text_cstring(&other, (pin_value){ .type = PIN_STR, .text = &slice }, &converted, &offset)
    );
    REQUIRE(offset == SIZE_MAX && !strcmp(converted, "ab"));
    REQUIRE(converted != slice.bytes);
    pin_value heap;
    copy.type_name = "test.text.alias";
    REQUIRE(!pin_value_copy(&heap, &copy));
    REQUIRE(pin_value_equal(&heap, &copy) && heap.text != copy.text);
    REQUIRE(heap.type_name == copy.type_name);
    pin_value_destroy(&heap);
    pin_value alias_snapshot;
    REQUIRE(!pin_value_snapshot_to_arena(&other, &alias_snapshot, &copy));
    REQUIRE(alias_snapshot.type_name != copy.type_name);
    REQUIRE(!strcmp(alias_snapshot.type_name, "test.text.alias"));
    REQUIRE(pin_text_equal(pin_text(alias_snapshot), pin_text(copy)));
    return true;
}
TEST_CASE(text_utf8_validation)
{
    static const struct
    {
        const char *bytes;
        uint32_t length;
        bool valid;
    } cases[] = {
        { "a\0b", 3, true },
        { "\xC2\x80\xE0\xA0\x80\xF4\x8F\xBF\xBF", 9, true },
        { "\xC0\x80", 2, false },
        { "\xE0\x80\x80", 3, false },
        { "\xED\xA0\x80", 3, false },
        { "\xF4\x90\x80\x80", 4, false },
        { "\xF0\x90\x80", 3, false },
        { "\xE2x\x80", 3, false },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        REQUIRE(
            pin_text_valid_utf8((pin_string){ .bytes = cases[i].bytes,
                                              .length = cases[i].length }) ==
            cases[i].valid
        );
    }
    return true;
}
static const type_desc *resolve_local(void *user, const char *name)
{
    const type_catalog_pack *types = user;
    for (int i = 0; i < types->type_count; i++) {
        if (!strcmp(types->types[i]->name, name)) {
            return types->types[i];
        }
    }
    return NULL;
}
TEST_CASE(record_native_nested_resolution_is_call_owned)
{
    typedef struct
    {
        const char *label;
        int32_t weight;
    } leaf;
    typedef struct
    {
        leaf child;
        int32_t code;
    } root;
    const type_field leaf_fields[] = {
        { .name = "label", .type = PIN_STR, .size = sizeof(char *) },
        { .name = "weight",
          .type = PIN_I32,
          .offset = offsetof(leaf, weight),
          .size = sizeof(int32_t),
          .range_contract = true,
          .min = 1,
          .max = 5,
          .range_default = 1 }
    };
    const type_desc leaf_type = { .name = "Resolved.Leaf",
                                  .size = sizeof(leaf),
                                  .fields = leaf_fields,
                                  .field_count = 2 };
    const type_field root_fields[] = {
        { .name = "child", .type = PIN_STRUCT, .size = sizeof(leaf), .type_name = "Resolved.Leaf" },
        { .name = "code", .type = PIN_I32, .offset = offsetof(root, code), .size = sizeof(int32_t) }
    };
    const type_desc root_type = { .name = "Resolved.Root",
                                  .size = sizeof(root),
                                  .fields = root_fields,
                                  .field_count = 2 };
    const type_desc *types[] = { &leaf_type, &root_type };
    const type_catalog_pack pack = { .types = types, .type_count = 2 };
    type_resolver resolver = { resolve_local, (void *)&pack };
    REQUIRE(!type_catalog_find(leaf_type.name));
    const pin_record *layout = pin_record_create(&source, leaf_type.name, 12981, leaf_fields, 2);
    REQUIRE(layout);
    void *payload = arena_alloc(&source, layout->type.size);
    pin_value text;
    REQUIRE(!pin_value_text(&source, &text, "bad\0suffix", 10));
    REQUIRE(!pin_write_field(&source, payload, &layout->type.fields[0], text));
    REQUIRE(
        !pin_write_field(&source, payload, &layout->type.fields[1], (pin_value){ .type = PIN_I32, .i = 3 })
    );
    pin_value authored = { .type = PIN_STRUCT,
                           .type_name = leaf_type.name,
                           .record = layout,
                           .ptr = payload };
    root destination = { .child = { "before", 2 }, .code = 77 };
    REQUIRE(pin_write_field_resolved(&owned, &destination, &root_fields[0], authored, resolver));
    REQUIRE(
        !strcmp(destination.child.label, "before") &&
        destination.child.weight == 2 && destination.code == 77
    );
    REQUIRE(!pin_value_text(&source, &text, "after", 5));
    REQUIRE(!pin_write_field(&source, payload, &layout->type.fields[0], text));
    REQUIRE(!pin_write_field_resolved(&owned, &destination, &root_fields[0], authored, resolver));
    REQUIRE(
        !strcmp(destination.child.label, "after") &&
        destination.child.weight == 3 && destination.code == 77
    );
    const type_field shadow_field = { .name = "plain",
                                      .type = PIN_I32,
                                      .size = sizeof(int32_t) };
    const type_desc shadow = { .name = leaf_type.name,
                               .size = sizeof(leaf),
                               .fields = &shadow_field,
                               .field_count = 1 };
    const type_desc *global_types[] = { &shadow };
    const type_catalog_pack global_pack = { .types = global_types, .type_count = 1 };
    type_catalog_set_project(&global_pack);
    project_set = true;
    REQUIRE(type_catalog_find(leaf_type.name) == &shadow && type_is_plain(&shadow));
    REQUIRE(type_resolve(resolver, leaf_type.name) == &leaf_type);
    REQUIRE(!pin_write_field_resolved(&owned, &destination, &root_fields[0], authored, resolver));
    root snapshot = destination;
    REQUIRE(!pin_struct_snapshot_fields_resolved(&other, &root_type, &snapshot, resolver));
    arena_free(&owned);
    arena_free(&source);
    REQUIRE(
        !strcmp(snapshot.child.label, "after") &&
        arena_contains(&other, snapshot.child.label)
    );
    snapshot.child.weight = 99;
    type_validate_ranges_resolved(&root_type, &snapshot, resolver);
    REQUIRE(snapshot.child.weight == 5);
    arena_init_collectable(&reads);
    root unchanged = snapshot;
    pin_value native = { .type = PIN_STRUCT, .type_name = root_type.name, .ptr = &snapshot };
    REQUIRE(pin_write_struct_resolved(&reads, &unchanged, &root_type, native, resolver));
    REQUIRE(pin_struct_snapshot_fields_resolved(&reads, &root_type, &unchanged, resolver));
    REQUIRE(unchanged.child.label == snapshot.child.label && !arena_storage_bytes(&reads));
    return true;
}
TEST_CASE(native_alias_snapshots_its_own_fields)
{
    /* A native alias declares its target's fields, so it snapshots without
     * the target in the catalog. */
    typedef struct
    {
        const char *label;
        float x;
    } tagged;
    const type_field fields[] = {
        { .name = "label", .type = PIN_STR, .size = sizeof(char *) },
        { .name = "x", .type = PIN_F32, .offset = offsetof(tagged, x), .size = sizeof(float) }
    };
    const type_desc alias = { .name = "Alias.Tagged",
                              .size = sizeof(tagged),
                              .fields = fields,
                              .field_count = 2,
                              .alias_of = "Alias.Uncataloged" };
    REQUIRE(!type_catalog_find(alias.alias_of) && !type_alias_layout(&alias));
    arena storage;
    arena_init(&storage, 256);
    char label[] = "kept";
    tagged value = { label, 2 };
    REQUIRE(!pin_struct_snapshot_fields(&storage, &alias, &value));
    REQUIRE(value.label != label && !strcmp(value.label, "kept") && value.x == 2);
    arena_free(&storage);
    return true;
}
/* A header persists only its native type, which the reader links, so it
 * writes after the type leaves the catalog; a payload needs it current. */
TEST_CASE(retired_native_header_writes_without_its_payload)
{
    typedef struct
    {
        float x;
    } retired;
    const type_field field = { .name = "x", .type = PIN_F32, .size = sizeof(float) };
    const type_desc type = { .name = "Retired.Point",
                             .size = sizeof(retired),
                             .fields = &field,
                             .field_count = 1 };
    arena storage;
    arena_init(&storage, 256);
    const pin_record *record = pin_record_from_type(&storage, &type);
    REQUIRE(record && record->native && !type_catalog_find(type.name));
    retired payload = { 2 };
    pin_value header = { .type = PIN_STRUCT, .type_name = type.name, .record = record },
              value = header;
    value.ptr = &payload;
    execution_wire wire = { 0 };
    execution_wire_value(&wire, &value);
    REQUIRE(
        wire.error && !strcmp(wire.error, "Native record type is retired or its layout is incompatible")
    );
    afree(wire.data);
    wire = (execution_wire){ 0 };
    execution_wire_value(&wire, &header);
    REQUIRE(!wire.error && wire.size > 0);
    afree(wire.data);
    arena_free(&storage);
    return true;
}
/* A module with thousands of requirements loads within the default
 * layout-work bound. */
TEST_CASE(prepared_type_table_scales_with_its_size)
{
    REQUIRE(record_prepared_type_table());
    return true;
}
void test_language_record_values()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_layout_validation);
    RUN_TEST_CASE(record_array_ownership);
    RUN_TEST_CASE(record_raw_trace_lifetime);
    RUN_TEST_CASE(record_copy_owns_field_type_names_and_units);
    RUN_TEST_CASE(record_identity_and_borrow);
    RUN_TEST_CASE(record_copy_rejects_changed_nested_layout);
    RUN_TEST_CASE(record_snapshot_reuses_copy_in_arena);
    RUN_TEST_CASE(record_array_checked_changes);
    RUN_TEST_CASE(record_projection_and_codec_guard);
    RUN_TEST_CASE(text_bytes_ownership_and_c_boundary);
    RUN_TEST_CASE(text_utf8_validation);
    RUN_TEST_CASE(record_native_nested_resolution_is_call_owned);
    RUN_TEST_CASE(native_alias_snapshots_its_own_fields);
    RUN_TEST_CASE(retired_native_header_writes_without_its_payload);
    RUN_TEST_CASE(prepared_type_table_scales_with_its_size);
    pu_clear_setup();
}
