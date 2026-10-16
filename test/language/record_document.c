#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/union.h"
#include "types/value_document.h"

#include <stdio.h>
#include <string.h>

/* Three fixed C document cases. No compiler, authored backend, I/O or workers. */
static arena source, owned;
static av_doc *doc;
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
    if (doc) {
        av_doc_free(doc);
    }
    doc = NULL;
    arena_free(&source);
    arena_free(&owned);
}
static pin_value declaration()
{
    const pin_record *record = pin_record_create(&source, "Rows.Row", 91, fields, 12);
    return (pin_value){ .type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record };
}
static av_value *row(int number)
{
    av_value *object = av_obj(doc);
    for (int i = 0; i < 12; i++) {
        av_value *value;
        if (i < 5) {
            char text[32];
            snprintf(text, sizeof text, "row-%d-field-%d", number, i);
            value = av_str(doc, text);
        } else if (i < 7) {
            value = av_sint(doc, number + i);
        } else {
            value = av_bool(doc, (number + i) % 2);
        }
        av_obj_set(doc, object, fields[i].name, value);
    }
    return object;
}
static bool check_row(const pin_value *value, int number)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE);
    REQUIRE(pin_record_valid(value->record));
    REQUIRE(value->type_name == value->record->type.name);
    REQUIRE(arena_contains(&owned, value->record));
    REQUIRE(arena_contains(&owned, value->ptr));
    for (int i = 0; i < 12; i++) {
        pin_value field;
        pin_read_field(value->ptr, &value->record->type.fields[i], &field);
        if (i < 5) {
            char text[32];
            snprintf(text, sizeof text, "row-%d-field-%d", number, i);
            REQUIRE(field.str && !strcmp(field.str, text));
            REQUIRE(arena_contains(&owned, field.str));
        } else if (i < 7) {
            REQUIRE(field.i == number + i);
        } else {
            REQUIRE(field.b == (bool)((number + i) % 2));
        }
    }
    return true;
}
static void collect(const void *root)
{
    arena_collection collection;
    arena *arenas[] = { &owned };
    arena_collect_begin(&collection, arenas, 1);
    arena_collect_mark(&collection, root);
    arena_collect_end(&collection);
}
TEST_CASE(record_document_roundtrip)
{
    doc = av_doc_new();
    pin_value type = declaration(), scalar, null_value, arrays[3];
    char error[128];
    /* Expected-red baseline: the old reader rejects this valid carried layout. */
    av_value *input = row(7);
    REQUIRE(!pin_value_read_document(&owned, type, input, &scalar, error, sizeof error));
    REQUIRE(av_equals(pin_value_to_document(doc, &scalar), input));
    REQUIRE(!pin_value_read_document(&owned, type, av_null(doc), &null_value, error, sizeof error));
    REQUIRE(
        !null_value.ptr && null_value.record &&
        arena_contains(&owned, null_value.record)
    );
    REQUIRE(null_value.type == PIN_STRUCT && null_value.container == PIN_CONTAINER_SINGLE);
    REQUIRE(null_value.type_name == null_value.record->type.name);
    const int counts[] = { 0, 1, 3 };
    type.container = PIN_CONTAINER_ARRAY;
    for (int pass = 0; pass < 3; pass++) {
        av_value *array = av_arr(doc);
        for (int i = 0; i < counts[pass]; i++) {
            av_arr_append(array, row(i));
        }
        REQUIRE(!pin_value_read_document(&owned, type, array, &arrays[pass], error, sizeof error));
        const pin_array *items = arrays[pass].ptr;
        REQUIRE(items && items->count == counts[pass]);
        REQUIRE(items->record == arrays[pass].record);
        REQUIRE(arrays[pass].type == PIN_STRUCT && arrays[pass].container == PIN_CONTAINER_ARRAY);
        REQUIRE(
            arrays[pass].type_name == items->type_name &&
            items->type_name == items->record->type.name
        );
        for (int i = 0; i < items->count; i++) {
            REQUIRE(pin_array_get(items, i).record == items->record);
            pin_value item = pin_array_get(items, i);
            REQUIRE(check_row(&item, i));
        }
        av_value *output = pin_value_to_document(doc, &arrays[pass]);
        REQUIRE(output && av_is_arr(output) && av_arr_size(output) == (size_t)counts[pass]);
        REQUIRE(av_equals(output, array));
    }
    av_doc_free(doc);
    doc = NULL;
    arena_free(&source);
    REQUIRE(check_row(&scalar, 7));
    REQUIRE(pin_record_valid(null_value.record));
    for (int pass = 0; pass < 3; pass++) {
        const pin_array *items = arrays[pass].ptr;
        REQUIRE(pin_record_valid(items->record));
        for (int i = 0; i < items->count; i++) {
            pin_value item = pin_array_get(items, i);
            REQUIRE(check_row(&item, i));
        }
    }
    return true;
}
TEST_CASE(record_document_rejections)
{
    doc = av_doc_new();
    pin_value type = declaration(), sentinel = { .type = PIN_I32, .i = 123 }, out;
    char error[128];
    /* Required and defaulted fields are both mandatory in the complete codec. */
    const int missing[] = { 0, 2 };
    for (int i = 0; i < 2; i++) {
        av_value *object = row(0);
        REQUIRE(av_obj_remove_key(object, fields[missing[i]].name));
        out = sentinel;
        REQUIRE(pin_value_read_document(&owned, type, object, &out, error, sizeof error));
        REQUIRE(!memcmp(&out, &sentinel, sizeof out));
        REQUIRE(strstr(error, "missing struct field"));
    }
    const int wrong[] = { 0, 5, 7 };
    for (int i = 0; i < 3; i++) {
        av_value *object = row(0);
        av_obj_set(doc, object, fields[wrong[i]].name, av_arr(doc));
        out = sentinel;
        REQUIRE(pin_value_read_document(&owned, type, object, &out, error, sizeof error));
        REQUIRE(!memcmp(&out, &sentinel, sizeof out));
    }
    av_value *object = row(0);
    pin_record malformed = *type.record;
    malformed.type.size++;
    pin_value bad = type;
    bad.record = &malformed;
    out = sentinel;
    REQUIRE(pin_value_read_document(&owned, bad, object, &out, error, sizeof error));
    REQUIRE(!memcmp(&out, &sentinel, sizeof out));
    bad = type;
    bad.type_name = "Other.Row";
    REQUIRE(pin_value_read_document(&owned, bad, object, &out, error, sizeof error));
    REQUIRE(!memcmp(&out, &sentinel, sizeof out));
    REQUIRE(pin_value_read_document(NULL, type, object, &out, error, sizeof error));
    REQUIRE(!memcmp(&out, &sentinel, sizeof out));
    /* Invalid metadata fails even for null payloads, without overwriting a key. */
    bad = type;
    bad.record = &malformed;
    av_value *target = av_obj(doc), *saved = av_str(doc, "preserved");
    av_obj_set(doc, target, "value", saved);
    pin_value_write_document(doc, target, "value", PIN_STRUCT, type.type_name, &bad);
    REQUIRE(av_obj_get(target, "value") == saved);
    REQUIRE(!pin_value_to_document(doc, &bad));
    pin_value_write_document(doc, target, "value", PIN_STRUCT, "Other.Row", &type);
    REQUIRE(av_obj_get(target, "value") == saved);
    pin_value_write_document(doc, target, "value", PIN_I32, type.type_name, &type);
    REQUIRE(av_obj_get(target, "value") == saved);
    pin_array array = { .element_type = PIN_STRUCT,
                        .type_name = type.type_name,
                        .record = type.record,
                        .items = &bad,
                        .count = 1,
                        .capacity = 1 };
    pin_value aggregate = type;
    aggregate.container = PIN_CONTAINER_ARRAY;
    aggregate.ptr = &array;
    pin_value_write_document(doc, target, "value", PIN_STRUCT, type.type_name, &aggregate);
    REQUIRE(av_obj_get(target, "value") == saved);
    REQUIRE(!pin_value_to_document(doc, &aggregate));
    pin_union *tagged = arena_alloc(&source, sizeof *tagged + 2 * sizeof(pin_value));
    tagged->tag = 17;
    tagged->count = 2;
    tagged->values[0] = sentinel;
    tagged->values[1] = aggregate;
    pin_value nested = { .type = PIN_UNION, .ptr = tagged };
    pin_value_write_document(doc, target, "value", PIN_UNION, NULL, &nested);
    REQUIRE(av_obj_get(target, "value") == saved);
    REQUIRE(!pin_value_to_document(doc, &nested));
    const pin_record *different = pin_record_create(&source, "Rows.Row", 92, fields, 12);
    REQUIRE(different && pin_record_valid(different));
    bad = type;
    bad.record = different; /* Valid shape, wrong nominal identity at item boundary. */
    pin_value_write_document(doc, target, "value", PIN_STRUCT, type.type_name, &aggregate);
    REQUIRE(av_obj_get(target, "value") == saved);
    bad = type;
    array.record = different; /* Valid outer layout differs from declared header. */
    pin_value_write_document(doc, target, "value", PIN_STRUCT, type.type_name, &aggregate);
    REQUIRE(av_obj_get(target, "value") == saved);
    return true;
}
TEST_CASE(record_document_raw_roots)
{
    doc = av_doc_new();
    arena_init_collectable(&owned);
    pin_value type = declaration(), value;
    char error[128];
    REQUIRE(!pin_value_read_document(&owned, type, row(4), &value, error, sizeof error));
    size_t expected = value.record->bytes + value.record->type.size;
    for (int i = 0; i < 5; i++) {
        pin_value field;
        pin_read_field(value.ptr, &value.record->type.fields[i], &field);
        expected += strlen(field.str) + 1;
    }
    arena_free(&source);
    av_doc_free(doc);
    doc = NULL;
    collect(value.ptr);
    REQUIRE(check_row(&value, 4));
    size_t bytes, capacity;
    arena_usage(&owned, &bytes, &capacity);
    REQUIRE(bytes == expected);
    collect(NULL);
    arena_usage(&owned, &bytes, &capacity);
    REQUIRE(bytes == 0);
    arena_free(&owned);
    arena_init_collectable(&owned);
    doc = av_doc_new();
    type = declaration();
    type.container = PIN_CONTAINER_ARRAY;
    REQUIRE(!pin_value_read_document(&owned, type, av_arr(doc), &value, error, sizeof error));
    const pin_array *array = value.ptr;
    expected = value.record->bytes + sizeof(pin_array);
    arena_free(&source);
    av_doc_free(doc);
    doc = NULL;
    collect(array);
    REQUIRE(array && !array->count && pin_record_valid(array->record));
    REQUIRE(array->record == value.record);
    REQUIRE(value.type == PIN_STRUCT && value.container == PIN_CONTAINER_ARRAY);
    REQUIRE(
        value.type_name == array->type_name &&
        array->type_name == array->record->type.name
    );
    arena_usage(&owned, &bytes, &capacity);
    REQUIRE(bytes == expected);
    collect(NULL);
    arena_usage(&owned, &bytes, &capacity);
    REQUIRE(bytes == 0);
    return true;
}
void test_language_record_document()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_document_roundtrip);
    RUN_TEST_CASE(record_document_rejections);
    RUN_TEST_CASE(record_document_raw_roots);
    pu_setup(NULL, NULL);
}
