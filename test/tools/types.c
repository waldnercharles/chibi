#include "execution/borrow.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/pointer.h"
#include "types/query.h"
#include "types/record.h"
#include "types/schema.h"
#include "types/text.h"
#include "types/trace.h"
#include "types/union.h"
#include "types/value_document.h"
#include "types/value_wire.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef CHIBI_TEST_SOURCE_RUNTIME
#include "runtime/native_types.h"
#include "test/support/reflected_types.h"
bool test_source_runtime_active();
#endif

typedef struct
{
    float x, y;
} test_position;

typedef struct
{
    bool enabled;
    test_position position;
} test_frame;

typedef struct
{
    test_frame frame;
    const char *label;
} test_record;

static const type_field position_fields[] = {
    { .name = "x",
      .type = PIN_F32,
      .offset = offsetof(test_position, x),
      .size = sizeof(float),
      .type_name = "number" },
    { .name = "y",
      .type = PIN_F32,
      .offset = offsetof(test_position, y),
      .size = sizeof(float),
      .type_name = "number" },
};
static const type_desc position_type = {
    .name = "test.position",
    .size = sizeof(test_position),
    .fields = position_fields,
    .field_count = 2,
};
static const type_field frame_fields[] = {
    { .name = "enabled",
      .type = PIN_BOOL,
      .offset = offsetof(test_frame, enabled),
      .size = sizeof(bool),
      .type_name = "bool" },
    { .name = "position",
      .type = PIN_STRUCT,
      .offset = offsetof(test_frame, position),
      .size = sizeof(test_position),
      .type_name = "test.position" },
};
static const type_desc frame_type = {
    .name = "test.frame",
    .size = sizeof(test_frame),
    .fields = frame_fields,
    .field_count = 2,
};
static const type_field record_fields[] = {
    { .name = "frame",
      .type = PIN_STRUCT,
      .offset = offsetof(test_record, frame),
      .size = sizeof(test_frame),
      .type_name = "test.frame" },
    { .name = "label",
      .type = PIN_STR,
      .offset = offsetof(test_record, label),
      .size = sizeof(const char *),
      .type_name = "const char *" },
};
static const type_desc record_type = {
    .name = "test.record",
    .size = sizeof(test_record),
    .fields = record_fields,
    .field_count = 2,
};
static const type_desc duplicate_position = {
    .name = "test.position",
    .size = sizeof(float),
    .fields = position_fields,
    .field_count = 1,
};
static const enum_value_desc modes[] = {
    { 3, "MODE_IDLE", "Idle" },
    { 7, "MODE_MOVING", "Moving" },
};
static const enum_value_desc alternate_modes[] = { { 99, "MODE_IDLE", "Other" } };
static const enum_desc mode_type = { "test.mode", modes, 2 };
static const enum_desc duplicate_mode = { "test.mode", alternate_modes, 1 };
static const type_desc *const first_types[] = { &position_type, &frame_type };
static const type_field recursive_fields[] = {
    { .name = "child", .type = PIN_STRUCT, .size = 1, .type_name = "test.recursive" },
};
static const type_desc recursive_type = {
    .name = "test.recursive",
    .size = 1,
    .fields = recursive_fields,
    .field_count = 1,
};
static const type_desc huge_type = { .name = "test.huge",
                                     .size = 64u * 1024u * 1024u + 1 };
static const type_desc *const second_types[] = {
    &record_type,
    &duplicate_position,
    &recursive_type,
    &huge_type,
};
static const enum_desc *const first_enums[] = { &mode_type };
static const enum_desc *const second_enums[] = { &duplicate_mode };

int type_catalog_pack_count()
{
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    if (test_source_runtime_active()) {
        return 2;
    }
#endif
    return 2;
}

type_catalog_pack type_catalog_pack_at(int index)
{
    assert(index >= 0 && index < type_catalog_pack_count());
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    if (test_source_runtime_active()) {
        return index ? TEST_REFLECTED_TYPES_PACK : TYPE_PACK;
    }
#endif
    if (index == 0) {
        return (type_catalog_pack){ first_types, 2, first_enums, 1 };
    }
    return (type_catalog_pack){ second_types, 4, second_enums, 1 };
}

static arena snapshots;
static arena cow_heap, cow_producer;
static pin_value cow_heap_text;
static pin_array *source;
static av_doc *source_document;
static bool pointer_project_catalog;

static void release_values()
{
    if (pointer_project_catalog) {
        type_catalog_set_project(NULL);
        pointer_project_catalog = false;
    }
    pin_array_free(source);
    source = NULL;
    arena_free(&snapshots);
    arena_free(&cow_producer);
    arena_free(&cow_heap);
    pin_value_destroy(&cow_heap_text);
    av_doc_free(source_document);
    source_document = NULL;
}

TEST_CASE(types_catalog_composes_ordered_packs)
{
    REQUIRE(type_catalog_count() == 6);
    REQUIRE(type_catalog_at(0) == &position_type);
    REQUIRE(type_catalog_at(2) == &record_type);
    REQUIRE(type_catalog_at(3) == &duplicate_position);
    REQUIRE(type_catalog_at(-1) == NULL && type_catalog_at(6) == NULL);
    REQUIRE(type_catalog_find("test.position") == &position_type);
    REQUIRE(type_catalog_find("test.record") == &record_type);
    REQUIRE(type_catalog_find("absent") == NULL);
    REQUIRE(type_catalog_enum_count() == 2);
    REQUIRE(type_catalog_enum_at(1) == &duplicate_mode);
    REQUIRE(type_catalog_enum_at(2) == NULL);
    REQUIRE(type_catalog_enum_find("test.mode") == &mode_type);
    REQUIRE(!strcmp(type_catalog_enum_label("test.mode", 7), "Moving"));
    int value = -1;
    REQUIRE(type_catalog_enum_value("test.mode", "MODE_IDLE", &value));
    REQUIRE(value == 3);
    REQUIRE(type_catalog_enum_value("test.mode", "Moving", &value));
    REQUIRE(value == 7);
    REQUIRE(!type_catalog_enum_value("test.mode", "Other", &value));
    REQUIRE(type_catalog_enum_label("test.mode", 99) == NULL);
    return true;
}

TEST_CASE(types_nested_reflection_reads_writes_and_checks_layout)
{
    test_frame frame = { .enabled = true, .position = { 1.25f, -4.0f } };
    pin_value value;
    REQUIRE(pin_type_field_path(&frame_type, "position.y") == &position_fields[1]);
    REQUIRE(pin_read_field_path(&snapshots, &frame, &frame_type, "position.y", &value) == 0);
    REQUIRE(value.type == PIN_F32 && value.f == -4.0f);
    {
        const char *error = pin_read_field(&snapshots, &frame, &frame_fields[1], &value);
        if (error) {
            abort();
        }
    };
    REQUIRE(value.type == PIN_STRUCT && value.ptr == &frame.position);
    REQUIRE(!strcmp(value.type_name, "test.position"));
    {
        const char *error = pin_write_field(
            &snapshots,
            value.ptr,
            &position_fields[0],
            (pin_value){ .type = PIN_F32, .f = 8.0f }
        );
        if (error) {
            abort();
        }
    };
    REQUIRE(frame.position.x == 8.0f && frame.position.y == -4.0f && frame.enabled);
    REQUIRE(pin_read_field_path(&snapshots, &frame, &frame_type, "enabled", &value) == 0);
    REQUIRE(value.type == PIN_BOOL && value.b);
    REQUIRE(
        pin_read_field_path(&snapshots, &frame, &frame_type, "position.absent", &value) == -1
    );
    REQUIRE(pin_type_field_path(&frame_type, "enabled.x") == NULL);
    REQUIRE(type_is_plain(&frame_type));
    REQUIRE(!type_is_plain(&record_type));
    REQUIRE(type_value_is_finite(&frame_type, &frame));
    frame.position.y = INFINITY;
    REQUIRE(!type_value_is_finite(&frame_type, &frame));
    uint64_t layout = type_layout_hash(&frame_type);
    type_field changed_fields[2];
    memcpy(changed_fields, frame_fields, sizeof changed_fields);
    type_desc changed = frame_type;
    changed.fields = changed_fields;
    changed_fields[0].description = "Presentation does not change storage";
    REQUIRE(type_layout_hash(&changed) == layout);
    changed_fields[1].offset++;
    REQUIRE(type_layout_hash(&changed) != layout);
    return true;
}

TEST_CASE(types_array_snapshot_owns_strings_and_names)
{
    char nominal_name[] = "test.text";
    char text[] = "original";
    source = pin_array_new(PIN_STR, nominal_name, 0);
    pin_value item = { .type = PIN_STR,
                       .text = &(const pin_string){ .bytes = text,
                                                    .length = (uint32_t)strlen(text) },
                       .type_name = nominal_name };
    REQUIRE(pin_array_push(source, &item) == 0);
    text[0] = 'X';
    REQUIRE(!strcmp(pin_text(pin_array_get(source, 0)).bytes, "original"));
    pin_array *snapshot = pin_array_snapshot_to_arena(&snapshots, source);
    REQUIRE(snapshot != NULL && snapshot->count == 1);
    REQUIRE(snapshot->items != source->items);
    REQUIRE(
        pin_text_equal(pin_text(pin_array_get(snapshot, 0)), pin_text(pin_array_get(source, 0)))
    );
    nominal_name[0] = 'X';
    pin_array_free(source);
    source = NULL;
    REQUIRE(!strcmp(snapshot->type_name, "test.text"));
    REQUIRE(!strcmp(pin_array_get(snapshot, 0).type_name, "test.text"));
    REQUIRE(!strcmp(pin_text(pin_array_get(snapshot, 0)).bytes, "original"));
    return true;
}

TEST_CASE(types_collection_follows_values_across_arenas)
{
    /* A root may point inside a value, and a value may reach another arena's
     * values; whatever no root reaches is swept. */
    arena first, second;
    arena_init_collectable(&first);
    arena_init_collectable(&second);
    char *text = arena_alloc(&first, sizeof "kept");
    memcpy(text, "kept", sizeof "kept");
    arena_alloc(&first, 32);
    pin_array *array = pin_array_snapshot_allocate(&second, PIN_STR, NULL, 1);
    array->items[0] = (pin_value){
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = text, .length = (uint32_t)strlen(text) }
    };
    arena_alloc(&second, 32);
    arena *arenas[] = { &first, &second };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 2);
    arena_collect_mark(&collection, &array->count);
    arena_collect_mark(&collection, NULL);
    arena_collect_end(&collection);
    size_t text_bytes, array_bytes, capacity;
    arena_usage(&first, &text_bytes, &capacity);
    arena_usage(&second, &array_bytes, &capacity);
    REQUIRE(text_bytes == sizeof "kept" && !strcmp(text, "kept"));
    REQUIRE(array_bytes == sizeof *array + sizeof array->items[0]);
    REQUIRE(first.kept == text_bytes && second.kept == array_bytes);
    REQUIRE(first.allocated == 0 && second.allocated == 0);
    arena_free(&first);
    arena_free(&second);
    return true;
}

/* A packed native-struct array whose type has left the catalog: tracing keeps
 * the array and its data without resolving an element layout. */
TEST_CASE(types_collection_traces_packed_arrays_of_unknown_native_types)
{
    arena storage;
    arena_init_collectable(&storage);
    pin_array *array = pin_array_snapshot_allocate(&storage, PIN_STRUCT, "test.trace.missing", 0);
    array->count = array->capacity = 2;
    array->items = NULL;
    array->stride = 8;
    array->data = arena_alloc(&storage, 2 * array->stride);
    memset(array->data, 0, 2 * array->stride);
    arena *arenas[] = { &storage };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    arena_collect_mark(&collection, array);
    arena_collect_end(&collection);
    REQUIRE(arena_contains(&storage, array) && arena_contains(&storage, array->data));
    arena_free(&storage);
    return true;
}

TEST_CASE(types_union_snapshots_and_heap_copies_own_nested_values)
{
    arena producer = { 0 };
    char text[] = "retained", name[] = "payload.name";
    pin_value field = { .type = PIN_STR,
                        .text = &(const pin_string){ .bytes = text,
                                                     .length = (uint32_t)strlen(text) },
                        .type_name = name };
    pin_value inner, outer, copied, snapshot;
    REQUIRE(pin_union_value(&producer, 7, &field, 1, &inner) == 0);
    REQUIRE(pin_union_value(&producer, 9, &inner, 1, &outer) == 0);
    REQUIRE(pin_value_copy(&copied, &outer) == 0);
    REQUIRE((uintptr_t)copied.ptr % _Alignof(pin_union) == 0);
    REQUIRE(pin_value_snapshot_to_arena(&snapshots, &snapshot, &outer) == 0);
    text[0] = name[0] = 'X';
    arena_free(&producer);
    REQUIRE(pin_value_equal(&copied, &snapshot));
    const pin_union *outer_copy = copied.ptr;
    const pin_union *inner_copy = outer_copy->values[0].ptr;
    REQUIRE(outer_copy->tag == 9 && inner_copy->tag == 7);
    REQUIRE(!strcmp(pin_text(inner_copy->values[0]).bytes, "retained"));
    REQUIRE(!strcmp(inner_copy->values[0].type_name, "payload.name"));
    pin_value_destroy(&copied);
    outer_copy = snapshot.ptr;
    inner_copy = outer_copy->values[0].ptr;
    REQUIRE(!strcmp(pin_text(inner_copy->values[0]).bytes, "retained"));
    pin_union_cancelled(&outer);
    REQUIRE(pin_value_copy(&copied, &outer) == 0);
    REQUIRE(pin_value_equal(&copied, &outer));
    pin_value_destroy(&copied);
    /* A failed clone must be safely destroyable, with no ownership of source. */
    pin_union *invalid = arena_alloc(&snapshots, sizeof *invalid + sizeof(pin_value));
    *invalid = (pin_union){ .tag = 11, .count = 1 };
    invalid->values[0] = (pin_value){ .type = PIN_STRUCT,
                                      .type_name = "unregistered.union.payload",
                                      .ptr = text };
    outer = (pin_value){ .type = PIN_UNION, .ptr = invalid };
    REQUIRE(pin_value_copy(&copied, &outer) != 0);
    REQUIRE(!copied.ptr);
    pin_value_destroy(&copied);
    REQUIRE(invalid->tag == 11);
    return true;
}

TEST_CASE(types_union_wire_roundtrip_traces_nested_arrays)
{
    arena decoded;
    arena_init_collectable(&decoded);
    pin_array *array = pin_array_snapshot_allocate(&snapshots, PIN_STR, NULL, 1);
    array->items[0] = (pin_value){ .type = PIN_STR,
                                   .text = &(const pin_string){
                                       .bytes = "wire-retained",
                                       .length = sizeof("wire-retained") - 1 } };
    pin_value payload = { .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
    pin_value original, restored;
    REQUIRE(pin_union_value(&snapshots, 7, &payload, 1, &original) == 0);
    execution_wire writer = { 0 };
    execution_wire_value(&writer, &original);
    REQUIRE(!writer.error && writer.size);
    execution_wire reader = { .reading = true,
                              .data = writer.data,
                              .size = writer.size,
                              .storage = &decoded };
    execution_wire_value(&reader, &restored);
    REQUIRE(!reader.error && reader.offset == reader.size);
    REQUIRE(pin_value_equal(&original, &restored));
    arena_free(&snapshots);
    afree(writer.data);
    arena_alloc(&decoded, 4096);
    arena *arenas[] = { &decoded };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    pin_value_trace(&restored, arena_collect_mark, &collection);
    arena_collect_end(&collection);
    const pin_union *value = restored.ptr;
    const pin_array *items = value->values[0].ptr;
    REQUIRE(value->tag == 7 && value->count == 1 && items->count == 1);
    REQUIRE(!strcmp(pin_text(items->items[0]).bytes, "wire-retained"));
    size_t bytes, capacity;
    arena_usage(&decoded, &bytes, &capacity);
    REQUIRE(bytes < 4096);
    arena_collect_begin(&collection, arenas, 1);
    arena_collect_end(&collection);
    arena_usage(&decoded, &bytes, &capacity);
    REQUIRE(bytes == 0);
    arena_free(&decoded);
    return true;
}

TEST_CASE(types_plain_struct_array_codecs_preserve_inline_values_and_nulls)
{
    test_frame frames[3] = {
        { .enabled = true, .position = { 1, 2 } },
        { .position = { 3, 4 } },
        { .enabled = true, .position = { 5, 6 } },
    };
    for (int null_at = -1; null_at < 3; null_at++) {
        pin_value items[3];
        for (int i = 0; i < 3; i++) {
            items[i] = (pin_value){ .type = PIN_STRUCT,
                                    .type_name = "test.frame",
                                    .ptr = i == null_at ? NULL : &frames[i] };
        }
        pin_array array = { .element_type = PIN_STRUCT,
                            .type_name = "test.frame",
                            .count = 3,
                            .capacity = 3,
                            .items = items };
        pin_value original = { .type = PIN_STRUCT,
                               .container = PIN_CONTAINER_ARRAY,
                               .type_name = "test.frame",
                               .ptr = &array };
        execution_wire writer = { 0 };
        execution_wire_value(&writer, &original);
        REQUIRE(!writer.error);
        execution_wire reader = { .reading = true,
                                  .data = writer.data,
                                  .size = writer.size,
                                  .storage = &snapshots };
        pin_value restored;
        execution_wire_value(&reader, &restored);
        afree(writer.data);
        REQUIRE(!reader.error && reader.offset == reader.size);
        REQUIRE(pin_value_equal(&original, &restored));
        const pin_array *wire_array = restored.ptr;
        REQUIRE(wire_array->stride == (null_at < 0 ? sizeof(test_frame) : 0));
        REQUIRE(pin_array_valid(wire_array));

        source_document = av_doc_new();
        av_value *document = pin_value_to_document(source_document, &restored);
        REQUIRE(document);
        char error[192];
        pin_value from_document;
        REQUIRE(
            !pin_value_read_document(&snapshots, original, document, &from_document, error, sizeof error)
        );
        REQUIRE(pin_value_equal(&original, &from_document));
        const pin_array *document_array = from_document.ptr;
        REQUIRE(document_array->stride == (null_at < 0 ? sizeof(test_frame) : 0));
        REQUIRE(pin_array_valid(document_array));
        av_doc_free(source_document);
        source_document = NULL;
    }
    return true;
}

TEST_CASE(types_union_wire_rejects_invalid_tags_counts_and_depth)
{
    pin_value item = { .type = PIN_I32, .i = 3 }, value;
    REQUIRE(pin_union_value(&snapshots, 7, &item, 1, &value) == 0);
    execution_wire writer = { 0 };
    execution_wire_value(&writer, &value);
    REQUIRE(!writer.error);
    /* Read the public field format to locate the union tag and payload count. */
    execution_wire cursor = { .reading = true,
                              .data = writer.data,
                              .size = writer.size,
                              .storage = &snapshots };
    int type = 0, container = 0;
    const char *name = NULL;
    bool present = false;
    execution_wire_int(&cursor, &type);
    execution_wire_int(&cursor, &container);
    execution_wire_string(&cursor, &name);
    execution_wire_bool(&cursor, &present);
    REQUIRE(!cursor.error && type == PIN_UNION && present);
    size_t tag_offset = cursor.offset;
    uint64_t tag = 0;
    execution_wire_u64(&cursor, &tag);
    REQUIRE(tag == 7);
    size_t count_offset = cursor.offset;
    for (int invalid = 0; invalid < 3; invalid++) {
        writer.data[tag_offset] = invalid == 0 ? 0 : 7;
        writer.data[count_offset] = invalid == 1 ? 1 : 2; /* zigzag -1 or 1 */
        execution_wire reader = { .reading = true,
                                  .data = writer.data,
                                  .size = writer.size - (invalid == 2),
                                  .storage = &snapshots };
        pin_value restored;
        execution_wire_value(&reader, &restored);
        REQUIRE(reader.error);
    }
    afree(writer.data);
    /* Source values are valid; wire depth limits still bound hostile nesting. */
    for (int i = 0; i < EXECUTION_WIRE_MAX_DEPTH; i++) {
        pin_union *nested = arena_alloc(&snapshots, sizeof *nested + sizeof(pin_value));
        *nested = (pin_union){ .tag = 9, .count = 1 };
        nested->values[0] = value;
        value = (pin_value){ .type = PIN_UNION, .ptr = nested };
    }
    writer = (execution_wire){ 0 };
    execution_wire_value(&writer, &value);
    REQUIRE(writer.error && strstr(writer.error, "nesting"));
    afree(writer.data);
    pin_union_cancelled(&value);
    writer = (execution_wire){ 0 };
    execution_wire_value(&writer, &value);
    REQUIRE(!writer.error);
    execution_wire reader = { .reading = true,
                              .data = writer.data,
                              .size = writer.size,
                              .storage = &snapshots };
    pin_value restored;
    execution_wire_value(&reader, &restored);
    REQUIRE(!reader.error && pin_value_equal(&value, &restored));
    afree(writer.data);
    return true;
}

/* Interned strings: each string is its 1-based index in a table of the
 * distinct strings in order of first use, 0 for NULL. The table follows the
 * body, then its offset in 8 bytes. Hashing streams the same bytes, writer
 * scratch is charged and released, and the reader shares one decoded string
 * between repeats. */
static void interned_strings(execution_wire *w, const char **texts, int count)
{
    uint64_t before = 7, after = 9;
    execution_wire_u64(w, &before);
    execution_wire_strings_begin(w);
    for (int i = 0; i < count; i++) {
        execution_wire_string(w, &texts[i]);
    }
    execution_wire_u64(w, &after);
    execution_wire_strings_finish(w);
}
static bool rejects_strings(const unsigned char *bytes, size_t size)
{
    const char *texts[5] = { 0 };
    execution_wire w = { .reading = true,
                         .data = (unsigned char *)bytes,
                         .size = size,
                         .storage = &snapshots };
    interned_strings(&w, texts, 5);
    return w.error != NULL;
}
TEST_CASE(types_wire_strings_store_each_string_once)
{
    const char *texts[] = { "beta", NULL, "alpha", "beta", "" };
    static const unsigned char expected[] = { 7,   1,   0,   2,   1,   3,   9,
                                              6,   5,   'b', 'e', 't', 'a', 6,
                                              'a', 'l', 'p', 'h', 'a', 1,   7,
                                              0,   0,   0,   0,   0,   0,   0 };
    size_t budget = 4096;
    execution_wire writer = { .storage_budget = &budget };
    interned_strings(&writer, texts, 5);
    REQUIRE(!writer.error && !writer.interning && !writer.strings && !writer.slots);
    REQUIRE(budget == 4096 && !writer.storage_charged && !writer.owned_bytes);
    REQUIRE(writer.size == sizeof expected && (size_t)acount(writer.data) == sizeof expected);
    REQUIRE(!memcmp(writer.data, expected, sizeof expected));
    uint64_t hash = UINT64_C(14695981039346656037), expected_hash = hash;
    for (size_t i = 0; i < sizeof expected; i++) {
        expected_hash = (expected_hash ^ expected[i]) * UINT64_C(1099511628211);
    }
    execution_wire hashed = { .hash = &hash };
    interned_strings(&hashed, texts, 5);
    REQUIRE(!hashed.error && hashed.size == sizeof expected && !hashed.data && hash == expected_hash);
    /* The table's slots and vector are reserved before they grow. */
    size_t tight = 64;
    execution_wire starved = { .hash = &hash, .storage_budget = &tight };
    interned_strings(&starved, texts, 5);
    REQUIRE(starved.error && strstr(starved.error, "budget") && tight == 64);

    const char *decoded[5] = { 0 };
    execution_wire reader = { .reading = true,
                              .data = writer.data,
                              .size = writer.size,
                              .storage = &snapshots };
    interned_strings(&reader, decoded, 5);
    REQUIRE(!reader.error && reader.offset == reader.size && reader.size == 7);
    REQUIRE(reader.string_count == 3 && reader.strings_named == 3);
    REQUIRE(
        !strcmp(decoded[0], "beta") && !decoded[1] &&
        !strcmp(decoded[2], "alpha") && !*decoded[4]
    );
    REQUIRE(decoded[0] == decoded[3]);
    afree(writer.data);

    /* An index past the table or ahead of first use, an entry never named and
     * a table offset past the end are rejected. */
    unsigned char bad[sizeof expected];
    memcpy(bad, expected, sizeof bad);
    bad[1] = 4;
    REQUIRE(rejects_strings(bad, sizeof bad));
    memcpy(bad, expected, sizeof bad);
    bad[3] = 3;
    REQUIRE(rejects_strings(bad, sizeof bad));
    memcpy(bad, expected, sizeof bad);
    bad[sizeof bad - 8] = 200;
    REQUIRE(rejects_strings(bad, sizeof bad));
    static const unsigned char unnamed[] = { 7, 1,   0, 1, 1, 1, 9, 4, 2, 'a',
                                             2, 'b', 7, 0, 0, 0, 0, 0, 0, 0 };
    REQUIRE(rejects_strings(unnamed, sizeof unnamed));
    /* Both entries are named in order, but a writer never stores one twice. */
    unsigned char repeated[] = { 7, 1,   0, 2, 1, 2, 9, 4, 2, 'a',
                                 2, 'b', 7, 0, 0, 0, 0, 0, 0, 0 };
    REQUIRE(!rejects_strings(repeated, sizeof repeated));
    repeated[11] = 'a';
    REQUIRE(rejects_strings(repeated, sizeof repeated));
    return true;
}
/* The table's scratch for a slot count, as value_wire.c charges it. */
static size_t table_scratch(size_t slots)
{
    return 2 * sizeof(dyna_hdr) + slots / 2 * sizeof(const char *) +
           slots * sizeof(int);
}
/* 33 distinct strings grow the slots from 64 to 128. Both tables are live
 * while the strings are rehashed, so the budget must cover both at once. */
TEST_CASE(types_wire_strings_charge_both_tables_while_growing)
{
    enum
    {
        DISTINCT = 33
    };
    char names[DISTINCT][4];
    const char *texts[DISTINCT];
    for (int i = 0; i < DISTINCT; i++) {
        snprintf(names[i], sizeof names[i], "s%d", i);
        texts[i] = names[i];
    }
    const size_t peak = table_scratch(64) + table_scratch(128);
    size_t budget = peak - 1;
    execution_wire writer = { .storage_budget = &budget };
    interned_strings(&writer, texts, DISTINCT);
    REQUIRE(writer.error && strstr(writer.error, "budget") && budget == peak - 1);
    REQUIRE(
        !writer.strings && !writer.slots && !writer.storage_charged && !writer.owned_bytes
    );
    afree(writer.data);
    budget = peak;
    writer = (execution_wire){ .storage_budget = &budget };
    interned_strings(&writer, texts, DISTINCT);
    REQUIRE(
        !writer.error && budget == peak && !writer.storage_charged && !writer.owned_bytes
    );
    afree(writer.data);
    return true;
}

TEST_CASE(types_struct_array_snapshot_retains_nested_payload)
{
    char label[] = "retained";
    char nominal_name[] = "test.record";
    test_record record = { .frame = { .enabled = true, .position = { 2, 5 } },
                           .label = label };
    source = pin_array_new(PIN_STRUCT, nominal_name, 1);
    pin_value item = { .type = PIN_STRUCT, .ptr = &record, .type_name = nominal_name };
    REQUIRE(pin_array_push(source, &item) == 0);
    REQUIRE(pin_array_get(source, 0).ptr == &record);
    pin_array *snapshot = pin_array_snapshot_to_arena(&snapshots, source);
    REQUIRE(snapshot != NULL && snapshot->count == 1);
    REQUIRE(pin_array_get(snapshot, 0).ptr != &record);
    test_record *copy = pin_array_get(snapshot, 0).ptr;
    REQUIRE(copy->label != record.label);
    record.frame.position.x = 99;
    record.frame.enabled = false;
    label[0] = 'X';
    nominal_name[0] = 'X';
    pin_array_free(source);
    source = NULL;
    pin_value value;
    REQUIRE(
        pin_read_field_path(&snapshots, copy, &record_type, "frame.position.x", &value) == 0
    );
    REQUIRE(value.type == PIN_F32 && value.f == 2);
    REQUIRE(copy->frame.enabled && !strcmp(copy->label, "retained"));
    REQUIRE(!strcmp(pin_array_get(snapshot, 0).type_name, "test.record"));
    REQUIRE(!strcmp(snapshot->type_name, "test.record"));
    return true;
}

TEST_CASE(types_document_snapshot_survives_source_release)
{
    source_document = av_doc_new();
    av_value *items = av_arr(source_document);
    av_value *record = av_obj(source_document), *frame = av_obj(source_document),
             *position = av_obj(source_document);
    av_obj_set(source_document, position, "x", av_real(source_document, 2.5));
    av_obj_set(source_document, position, "y", av_real(source_document, -4));
    av_obj_set(source_document, frame, "enabled", av_bool(source_document, true));
    av_obj_set(source_document, frame, "position", position);
    av_obj_set(source_document, record, "frame", frame);
    av_obj_set(source_document, record, "label", av_str(source_document, "retained document"));
    av_arr_append(items, record);
    char nominal[] = "test.record", error[192];
    pin_value value;
    REQUIRE(
        pin_value_read_document(
            &snapshots,
            (pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_ARRAY, .type_name = nominal },
            items,
            &value,
            error,
            sizeof error
        ) == 0
    );
    av_value *modes = av_arr(source_document);
    av_arr_append(modes, av_str(source_document, "Moving"));
    av_arr_append(modes, av_sint(source_document, 3));
    pin_value mode_values;
    REQUIRE(
        pin_value_read_document(
            &snapshots,
            (pin_value){ .type = PIN_ENUM, .container = PIN_CONTAINER_SET, .type_name = "test.mode" },
            modes,
            &mode_values,
            error,
            sizeof error
        ) == 0
    );
    nominal[0] = 'X';
    av_doc_free(source_document);
    source_document = NULL;
    const pin_array *array = value.ptr, *choices = mode_values.ptr;
    REQUIRE(array->count == 1 && !strcmp(array->type_name, "test.record"));
    const test_record *copy = pin_array_get(array, 0).ptr;
    REQUIRE(
        copy->frame.enabled && copy->frame.position.x == 2.5f &&
        copy->frame.position.y == -4
    );
    REQUIRE(!strcmp(copy->label, "retained document"));
    REQUIRE(mode_values.container == PIN_CONTAINER_SET && choices->count == 2);
    REQUIRE(pin_array_get(choices, 0).e == 7 && pin_array_get(choices, 1).e == 3);
    return true;
}

static bool map_document_roundtrip(pin_value original, pin_value *restored)
{
    source_document = av_doc_new();
    av_value *document = pin_value_to_document(source_document, &original);
    REQUIRE(document && av_is_obj(document));
    REQUIRE(av_obj_size(document) == 3);
    char error[192];
    REQUIRE(
        !pin_value_read_document(&snapshots, original, document, restored, error, sizeof error)
    );
    REQUIRE(pin_value_equal(&original, restored));
    REQUIRE(!strcmp(original.type_name, restored->type_name));
    av_doc_free(source_document);
    source_document = NULL;
    return true;
}

TEST_CASE(types_map_documents_own_nested_maps_arrays_and_native_structs)
{
    arena producer = { 0 };
    pin_value string = { .type = PIN_STR }, integer = { .type = PIN_I32 };
    pin_map_entry counts[] = {
        { { .type = PIN_STR,
            .text = &(const pin_string){ .bytes = "first", .length = sizeof("first") - 1 } },
          { .type = PIN_I32, .i = 7 } },
        { { .type = PIN_STR,
            .text = &(const pin_string){ .bytes = "second",
                                         .length = sizeof("second") - 1 } },
          { .type = PIN_I32, .i = 9 } },
    };
    pin_map *inner = pin_map_create(&producer, string, integer, counts, 2);
    REQUIRE(inner);
    pin_value child = { .type = PIN_UNION,
                        .container = PIN_CONTAINER_MAP,
                        .type_name = "__map_string_int",
                        .ptr = inner };
    pin_map_entry nested = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "child", .length = sizeof("child") - 1 } },
        child
    };
    pin_map *outer = pin_map_create(&producer, string, child, &nested, 1);
    REQUIRE(outer);
    pin_value values[] = { child, child };
    pin_array rows = { .element_type = PIN_UNION,
                       .type_name = "__array_of_maps",
                       .count = 2,
                       .capacity = 2,
                       .items = values };
    pin_value array = { .type = PIN_UNION,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = rows.type_name,
                        .ptr = &rows };
    pin_map_entry arrays = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "rows", .length = sizeof("rows") - 1 } },
        array
    };
    pin_map *with_arrays = pin_map_create(&producer, string, array, &arrays, 1);
    REQUIRE(with_arrays);
    test_frame frame = { .enabled = true, .position = { 2.5f, -4 } };
    pin_value record = { .type = PIN_STRUCT, .type_name = "test.frame", .ptr = &frame };
    pin_map_entry native = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "frame", .length = sizeof("frame") - 1 } },
        record
    };
    pin_map *with_structs = pin_map_create(&producer, string, record, &native, 1);
    REQUIRE(with_structs);
    pin_map *maps[] = { outer, with_arrays, with_structs };
    pin_value restored[3];
    for (int i = 0; i < 3; i++) {
        pin_value original = { .type = PIN_UNION,
                               .container = PIN_CONTAINER_MAP,
                               .type_name = "__outer_map",
                               .ptr = maps[i] };
        REQUIRE(map_document_roundtrip(original, &restored[i]));
    }
    arena_free(&producer);
    const pin_map *copy = restored[0].ptr;
    const pin_map *copied_child = copy->entries[0].value.ptr;
    REQUIRE(!strcmp(pin_text(copy->entries[0].key).bytes, "child"));
    REQUIRE(
        copied_child->entries[0].value.i == 7 && copied_child->entries[1].value.i == 9
    );
    REQUIRE(!strcmp(pin_text(copied_child->entries[1].key).bytes, "second"));
    copy = restored[1].ptr;
    const pin_array *copied_rows = copy->entries[0].value.ptr;
    REQUIRE(copied_rows->count == 2 && !strcmp(copied_rows->type_name, "__array_of_maps"));
    child = pin_array_get(copied_rows, 1);
    REQUIRE(child.container == PIN_CONTAINER_MAP && !strcmp(child.type_name, "__map_string_int"));
    copied_child = child.ptr;
    REQUIRE(copied_child->entries[0].value.i == 7);
    copy = restored[2].ptr;
    const test_frame *copied_frame = copy->entries[0].value.ptr;
    REQUIRE(
        copied_frame->enabled && copied_frame->position.x == 2.5f &&
        copied_frame->position.y == -4
    );
    return true;
}

TEST_CASE(types_map_documents_require_authored_declarations_and_reject_malformed_input)
{
    const type_field fields[] = { { .name = "count", .type = PIN_I32 } };
    const pin_record *layout = pin_record_create(&snapshots, "Document.Row", 3451, fields, 1);
    REQUIRE(layout);
    void *row = arena_alloc(&snapshots, layout->type.size);
    {
        const char *error = pin_write_field(
            &snapshots,
            row,
            &layout->type.fields[0],
            (pin_value){ .type = PIN_I32, .i = 11 }
        );
        if (error) {
            abort();
        }
    };
    pin_value string = { .type = PIN_STR }, record = {
        .type = PIN_STRUCT,
        .type_name = layout->type.name,
        .record = layout,
        .ptr = row,
    };
    pin_map_entry entry = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "row", .length = sizeof("row") - 1 } },
        record
    };
    pin_map *map = pin_map_create(&snapshots, string, record, &entry, 1);
    REQUIRE(map);
    pin_value original = { .type = PIN_UNION,
                           .container = PIN_CONTAINER_MAP,
                           .type_name = "__authored_map",
                           .ptr = map },
              restored;
    REQUIRE(map_document_roundtrip(original, &restored));
    source_document = av_doc_new();
    av_value *document = pin_value_to_document(source_document, &original);
    REQUIRE(document);
    pin_value missing_layout = original;
    missing_layout.ptr = NULL;
    pin_value output = { .type = PIN_I32, .i = 73 };
    char error[192];
    REQUIRE(
        pin_value_read_document(&snapshots, missing_layout, document, &output, error, sizeof error)
    );
    REQUIRE(output.type == PIN_I32 && output.i == 73 && strstr(error, "type"));
    av_value *entries = av_obj_get(document, "entries");
    av_arr_append(entries, av_clone(source_document, av_arr_get(entries, 0)));
    REQUIRE(pin_value_read_document(&snapshots, original, document, &output, error, sizeof error));
    REQUIRE(output.type == PIN_I32 && output.i == 73 && strstr(error, "duplicate key"));
    av_arr_remove(entries, 1);
    av_obj_set(source_document, av_obj_get(document, "key_type"), "storage", av_str(source_document, "int"));
    REQUIRE(pin_value_read_document(&snapshots, original, document, &output, error, sizeof error));
    REQUIRE(output.type == PIN_I32 && output.i == 73);
    av_doc_free(source_document);
    source_document = NULL;

    pin_map_entry cycle_entry = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "loop", .length = sizeof("loop") - 1 } },
        { 0 }
    };
    pin_value recursive = { .type = PIN_UNION,
                            .container = PIN_CONTAINER_MAP,
                            .type_name = "Recursive" };
    pin_map cycle = { .key_type = string,
                      .value_type = recursive,
                      .count = 1,
                      .entries = &cycle_entry };
    cycle_entry.value = recursive;
    cycle_entry.value.ptr = &cycle;
    source_document = av_doc_new();
    REQUIRE(!pin_value_to_document(source_document, &cycle_entry.value));
    av_value *nested = av_null(source_document), *leaf = NULL;
    for (int i = 0; i < 66; i++) {
        av_value *node = av_obj(source_document),
                 *key_type = av_obj(source_document),
                 *value_type = av_obj(source_document),
                 *entries = av_arr(source_document),
                 *pair = av_arr(source_document);
        av_obj_add_str(source_document, key_type, "storage", "const char *");
        av_obj_add_str(source_document, key_type, "container", "value");
        av_obj_add_str(source_document, value_type, "storage", "union");
        av_obj_add_str(source_document, value_type, "container", "map");
        av_obj_add_str(source_document, value_type, "name", "Recursive");
        av_arr_append(pair, av_str(source_document, "loop"));
        av_arr_append(pair, nested);
        av_arr_append(entries, pair);
        av_obj_set(source_document, node, "key_type", key_type);
        av_obj_set(source_document, node, "value_type", value_type);
        av_obj_set(source_document, node, "entries", entries);
        if (!leaf) {
            leaf = node;
        }
        nested = node;
    }
    REQUIRE(pin_value_read_document(&snapshots, recursive, nested, &output, error, sizeof error));
    REQUIRE(output.type == PIN_I32 && output.i == 73);
    av_obj_set(source_document, av_obj_get(leaf, "key_type"), "storage", av_str(source_document, "f64"));
    av_arr_replace(av_arr_get(av_obj_get(leaf, "entries"), 0), 0, av_real(source_document, NAN));
    REQUIRE(pin_value_read_document(&snapshots, recursive, leaf, &output, error, sizeof error));
    REQUIRE(output.type == PIN_I32 && output.i == 73);
    return true;
}

TEST_CASE(types_document_rejects_invalid_values_without_publication)
{
    source_document = av_doc_new();
    pin_value value = { .type = PIN_I32, .i = 99 };
    char error[192];
    REQUIRE(
        pin_value_read_document(
            &snapshots,
            (pin_value){ .type = PIN_I8 },
            av_uint(source_document, 256),
            &value,
            error,
            sizeof error
        ) == -1
    );
    REQUIRE(value.type == PIN_I32 && value.i == 99 && error[0]);
    REQUIRE(
        pin_value_read_document(
            &snapshots,
            (pin_value){ .type = PIN_ENUM, .type_name = "test.mode" },
            av_str(source_document, "unknown mode"),
            &value,
            error,
            sizeof error
        ) == -1
    );
    const char embedded[] = { 'x', 0, 'y' };
    av_value *encoded_text = av_strn(source_document, embedded, sizeof embedded);
    pin_value text;
    REQUIRE(
        !pin_value_read_document(&snapshots, (pin_value){ .type = PIN_STR }, encoded_text, &text, error, sizeof error)
    );
    REQUIRE(text.type == PIN_STR && !text.type_name);
    REQUIRE(pin_text(text).length == sizeof embedded);
    REQUIRE(!memcmp(pin_text(text).bytes, embedded, sizeof embedded));
    REQUIRE(pin_text(text).bytes[sizeof embedded] == 0);
    REQUIRE(pin_text(text).bytes != av_get_str(encoded_text));
    REQUIRE(arena_contains(&snapshots, text.text));
    REQUIRE(arena_contains(&snapshots, text.text));
    REQUIRE(
        pin_value_read_document(&snapshots, (pin_value){ .type = PIN_NAME }, encoded_text, &value, error, sizeof error) ==
        -1
    );
    REQUIRE(value.type == PIN_I32 && value.i == 99);
    av_doc_free(source_document);
    source_document = av_doc_new();
    REQUIRE(pin_text(text).length == sizeof embedded);
    REQUIRE(!memcmp(pin_text(text).bytes, embedded, sizeof embedded));
    REQUIRE(
        pin_value_read_document(
            NULL,
            (pin_value){ .type = PIN_STR },
            av_str(source_document, "borrowed"),
            &value,
            error,
            sizeof error
        ) == -1
    );
    av_value *array = av_arr(source_document);
    av_arr_append(array, av_obj(source_document));
    REQUIRE(
        pin_value_read_document(
            &snapshots,
            (pin_value){ .type = PIN_STRUCT,
                         .container = PIN_CONTAINER_ARRAY,
                         .type_name = "test.record" },
            array,
            &value,
            error,
            sizeof error
        ) == -1
    );
    REQUIRE(strstr(error, "missing struct field") != NULL);
    REQUIRE(
        pin_value_read_document(
            &snapshots,
            (pin_value){ .type = PIN_I32, .container = PIN_CONTAINER_MAP },
            array,
            &value,
            error,
            sizeof error
        ) == -1
    );
    av_value *root = av_obj(source_document), *nested = root;
    for (int i = 0; i < 70; i++) {
        av_value *child = av_obj(source_document);
        av_obj_set(source_document, nested, "child", child);
        nested = child;
    }
    REQUIRE(
        pin_value_read_document(
            &snapshots,
            (pin_value){ .type = PIN_STRUCT, .type_name = "test.recursive" },
            root,
            &value,
            error,
            sizeof error
        ) == -1
    );
    REQUIRE(strstr(error, "traversal limit") != NULL);
    REQUIRE(
        pin_value_read_document(
            &snapshots,
            (pin_value){ .type = PIN_STRUCT, .type_name = "test.huge" },
            av_obj(source_document),
            &value,
            error,
            sizeof error
        ) == -1
    );
    REQUIRE(strstr(error, "byte limit") != NULL);
    REQUIRE(value.type == PIN_I32 && value.i == 99);
    return true;
}

TEST_CASE(types_document_array_collection_retains_split_payloads)
{
    arena_init_collectable(&snapshots);
    source_document = av_doc_new();
    for (int strings = 0; strings < 2; strings++) {
        av_value *document = av_arr(source_document);
        av_arr_append(document, strings ? av_str(source_document, "owned") : av_sint(source_document, 17));
        pin_value declaration = { .type = strings ? PIN_STR : PIN_I32,
                                  .container = PIN_CONTAINER_ARRAY },
                  value;
        char error[192];
        REQUIRE(
            !pin_value_read_document(&snapshots, declaration, document, &value, error, sizeof error)
        );
        pin_array *array = value.ptr;
        REQUIRE(array && pin_array_valid(array));
        const void *payload = array->stride ? array->data : (void *)array->items;
        arena_collection collection;
        arena *arenas[] = { &snapshots };
        arena_collect_begin(&collection, arenas, 1);
        pin_value_trace(&value, arena_collect_mark, &collection);
        arena_collect_end(&collection);
        REQUIRE(arena_contains(&snapshots, array));
        REQUIRE(arena_contains(&snapshots, payload));
        pin_value item = pin_array_get(array, 0);
        if (strings) {
            REQUIRE(arena_contains(&snapshots, item.text));
            REQUIRE(arena_contains(&snapshots, item.text));
            REQUIRE(!strcmp(pin_text(item).bytes, "owned"));
        } else {
            REQUIRE(item.i == 17);
        }
    }
    return true;
}
TEST_CASE(types_authored_record_document_uses_nested_native_layout)
{
    const type_field fields[] = {
        { .name = "frame", .type = PIN_STRUCT, .type_name = "test.frame" },
        { .name = "code", .type = PIN_I32 }
    };
    const pin_record *record = pin_record_create(&snapshots, "Authored.Frame", 9871, fields, 2);
    REQUIRE(record && pin_record_valid(record));
    test_frame frame = { .enabled = true, .position = { 3.5f, -2.25f } };
    void *data = arena_alloc(&snapshots, record->type.size);
    memset(data, 0, record->type.size);
    REQUIRE(!pin_write_field(
        &snapshots,
        data,
        &record->type.fields[0],
        (pin_value){ .type = PIN_STRUCT, .type_name = "test.frame", .ptr = &frame }
    ));
    REQUIRE(!pin_write_field(
        &snapshots,
        data,
        &record->type.fields[1],
        (pin_value){ .type = PIN_I32, .i = 19 }
    ));
    pin_value original = { .type = PIN_STRUCT,
                           .type_name = record->type.name,
                           .record = record,
                           .ptr = data };
    source_document = av_doc_new();
    av_value *document = pin_value_to_document(source_document, &original);
    REQUIRE(document);
    pin_value restored;
    char error[192];
    REQUIRE(
        !pin_value_read_document(&snapshots, original, document, &restored, error, sizeof error)
    );
    REQUIRE(pin_value_equal(&original, &restored));
    pin_value nested;
    REQUIRE(!pin_read_field(&snapshots, restored.ptr, &record->type.fields[0], &nested));
    const test_frame *result = nested.ptr;
    REQUIRE(
        result && result->enabled && result->position.x == 3.5f &&
        result->position.y == -2.25f
    );
    return true;
}
TEST_CASE(types_nested_pointer_records_require_owned_layout_support)
{
    typedef struct
    {
        pin_pointer *alias;
    } pointer_row;
    static const type_field nested_fields[] = {
        { .name = "alias",
          .type = PIN_POINTER,
          .type_name = "test.pointer.string",
          .offset = offsetof(pointer_row, alias),
          .size = sizeof(pin_pointer *) }
    };
    static const type_desc nested = { .name = "test.pointer.row",
                                      .size = sizeof(pointer_row),
                                      .fields = nested_fields,
                                      .field_count = 1 };
    static const type_desc *const types[] = { &nested };
    static const type_catalog_pack pack = { .types = types, .type_count = 1 };
    type_catalog_set_project(&pack);
    pointer_project_catalog = true;
    const type_field fields[] = {
        { .name = "nested", .type = PIN_STRUCT, .type_name = "test.pointer.row" }
    };
    REQUIRE(!pin_record_create(&snapshots, "test.pointer.outer", 9872, fields, 1));
    REQUIRE(!type_is_plain(&nested));
    /* A flat record owns its pointer-field spelling and layout without a
     * mutable nested catalog descriptor. This supported case still succeeds. */
    const pin_record *flat = pin_record_create(&snapshots, "test.pointer.flat", 9873, nested_fields, 1);
    REQUIRE(flat && pin_record_valid(flat));
    return true;
}

/* All new ownership cases use teardown-owned arenas so a failed REQUIRE still
 * releases descriptors and external buffers. */
static pin_value cow_integers(int count, int first)
{
    pin_array shape = { .element_type = PIN_I32 };
    pin_array *array = pin_array_snapshot_like(&cow_heap, &shape, count);
    if (!array) {
        return (pin_value){ 0 };
    }
    for (int i = 0; i < count; i++) {
        pin_value item = { .type = PIN_I32, .i = first + i };
        if (pin_array_set(array, i, &item)) {
            return (pin_value){ 0 };
        }
    }
    if (pin_array_manage(&cow_heap, array)) {
        return (pin_value){ 0 };
    }
    return (pin_value){ .type = PIN_I32,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = array->type_name,
                        .record = array->record,
                        .ptr = array };
}
static bool cow_append_integer(pin_value *value, int integer)
{
    pin_array *array = value->ptr;
    if (pin_array_make_unique(&cow_heap, array) ||
        pin_array_reserve_owned(&cow_heap, array, array->count + 1)) {
        return false;
    }
    int index = array->count++;
    pin_value item = { .type = PIN_I32, .i = integer };
    if (pin_array_set(array, index, &item)) {
        return false;
    }
    pin_array_mutation_end(array);
    return true;
}
static void cow_collect(const pin_value *roots, int count)
{
    arena *arenas[] = { &cow_heap };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    for (int i = 0; i < count; i++) {
        pin_value_trace(&roots[i], arena_collect_mark, &collection);
    }
    arena_collect_end(&collection);
}

TEST_CASE(types_array_value_copy_isolates_elements_and_length)
{
    arena_init_collectable(&cow_heap);
    pin_value original = cow_integers(3, 1), copy, borrowed;
    REQUIRE(original.ptr);
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copy, &original));
    REQUIRE(cow_append_integer(&copy, 4));
    REQUIRE(((pin_array *)original.ptr)->count == 3);
    REQUIRE(((pin_array *)copy.ptr)->count == 4);
    pin_value ninety = { .type = PIN_I32, .i = 90 };
    REQUIRE(!pin_array_snapshot_set(&cow_heap, copy.ptr, 0, &ninety));
    REQUIRE(pin_array_get(original.ptr, 0).i == 1);
    REQUIRE(pin_array_get(copy.ptr, 0).i == 90);
    pin_value eighty = { .type = PIN_I32, .i = 80 };
    REQUIRE(!pin_array_snapshot_set(&cow_heap, original.ptr, 1, &eighty));
    REQUIRE(pin_array_get(original.ptr, 1).i == 80);
    REQUIRE(pin_array_get(copy.ptr, 1).i == 2);
    REQUIRE(!pin_value_snapshot_to_arena(&snapshots, &borrowed, &copy));
    REQUIRE(cow_append_integer(&copy, 5));
    REQUIRE(((pin_array *)borrowed.ptr)->count == 5);
    REQUIRE(((pin_array *)original.ptr)->count == 3);
    return true;
}

TEST_CASE(types_nested_array_value_copy_isolates_child_mutation)
{
    arena_init_collectable(&cow_heap);
    pin_value external = cow_integers(2, 10);
    REQUIRE(external.ptr);
    pin_array *rows = pin_array_snapshot_allocate(&cow_heap, PIN_STRUCT, "__collection_cow_rows", 1);
    REQUIRE(rows);
    REQUIRE(!pin_value_collection_copy(&cow_heap, &rows->items[0], &external));
    REQUIRE(!pin_array_manage(&cow_heap, rows));
    pin_value original = { .type = PIN_STRUCT,
                           .container = PIN_CONTAINER_ARRAY,
                           .type_name = rows->type_name,
                           .ptr = rows },
              copy;
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copy, &original));
    REQUIRE(!pin_array_make_unique(&cow_heap, copy.ptr));
    pin_value child = pin_array_get(copy.ptr, 0);
    pin_value changed = { .type = PIN_I32, .i = 99 };
    REQUIRE(!pin_array_snapshot_set(&cow_heap, child.ptr, 0, &changed));
    REQUIRE(cow_append_integer(&child, 12));
    pin_value before = pin_array_get(original.ptr, 0);
    REQUIRE(pin_array_get(before.ptr, 0).i == 10);
    REQUIRE(((pin_array *)before.ptr)->count == 2);
    REQUIRE(pin_array_get(external.ptr, 0).i == 10);
    REQUIRE(((pin_array *)external.ptr)->count == 2);
    child = pin_array_get(copy.ptr, 0);
    REQUIRE(pin_array_get(child.ptr, 0).i == 99);
    REQUIRE(((pin_array *)child.ptr)->count == 3);
    pin_value roots[] = { original, copy, external };
    cow_collect(roots, 3);
    REQUIRE(pin_array_get(pin_array_get(copy.ptr, 0).ptr, 2).i == 12);
    REQUIRE(pin_array_get(pin_array_get(original.ptr, 0).ptr, 1).i == 11);
    return true;
}

TEST_CASE(types_map_value_copy_preserves_order_and_isolates_updates)
{
    arena_init_collectable(&cow_heap);
    pin_value integer = { .type = PIN_I32 };
    pin_map_entry entries[] = {
        { { .type = PIN_I32, .i = 1 }, { .type = PIN_I32, .i = 10 } },
        { { .type = PIN_I32, .i = 2 }, { .type = PIN_I32, .i = 20 } },
    };
    pin_map *map = pin_map_create(&cow_heap, integer, integer, entries, 2);
    REQUIRE(map);
    pin_value original = { .type = PIN_I32, .container = PIN_CONTAINER_MAP, .ptr = map },
              copy, borrowed;
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copy, &original));
    pin_map *updated = copy.ptr;
    pin_value first = entries[0].key, replacement = { .type = PIN_I32, .i = 77 };
    REQUIRE(!pin_map_store_owned(&cow_heap, &updated, &first, &replacement));
    copy.ptr = updated;
    REQUIRE(map->entries[0].value.i == 10);
    REQUIRE(updated->entries[0].key.i == 1 && updated->entries[0].value.i == 77);
    REQUIRE(updated->entries[1].key.i == 2 && updated->entries[1].value.i == 20);
    pin_value third = { .type = PIN_I32, .i = 3 };
    REQUIRE(!pin_map_store_owned(&cow_heap, &updated, &third, &replacement));
    copy.ptr = updated;
    REQUIRE(map->count == 2 && updated->count == 3);
    REQUIRE(updated->entries[2].key.i == 3);
    REQUIRE(!pin_value_snapshot_to_arena(&snapshots, &borrowed, &copy));
    REQUIRE(borrowed.ptr == copy.ptr);
    REQUIRE(!pin_map_delete_owned(&cow_heap, map, &entries[1].key));
    REQUIRE(map->count == 1 && updated->count == 3);
    REQUIRE(pin_map_find(updated, &entries[1].key) == 1);
    pin_value roots[] = { original, copy };
    cow_collect(roots, 2);
    REQUIRE(((pin_map *)copy.ptr)->entries[2].value.i == 77);
    return true;
}

TEST_CASE(types_map_value_copy_isolates_nested_array_values)
{
    arena_init_collectable(&cow_heap);
    pin_value external = cow_integers(2, 4);
    REQUIRE(external.ptr);
    pin_value key = { .type = PIN_I32, .i = 1 }, integer = { .type = PIN_I32 };
    pin_value arrays = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY };
    pin_map_entry entry = { key, external };
    pin_map *map = pin_map_create(&cow_heap, integer, arrays, &entry, 1);
    REQUIRE(map);
    pin_value original = { .type = PIN_I32, .container = PIN_CONTAINER_MAP, .ptr = map },
              copy;
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copy, &original));
    pin_map *updated = copy.ptr;
    REQUIRE(!pin_map_make_unique(&cow_heap, updated));
    pin_value child = updated->entries[0].value;
    pin_value replacement = { .type = PIN_I32, .i = 88 };
    REQUIRE(!pin_array_snapshot_set(&cow_heap, child.ptr, 0, &replacement));
    REQUIRE(cow_append_integer(&child, 6));
    REQUIRE(pin_array_get(external.ptr, 0).i == 4);
    REQUIRE(pin_array_get(map->entries[0].value.ptr, 0).i == 4);
    REQUIRE(((pin_array *)map->entries[0].value.ptr)->count == 2);
    REQUIRE(pin_array_get(updated->entries[0].value.ptr, 0).i == 88);
    REQUIRE(((pin_array *)updated->entries[0].value.ptr)->count == 3);
    return true;
}

TEST_CASE(types_nested_map_value_copy_isolates_child_updates)
{
    arena_init_collectable(&cow_heap);
    pin_value integer = { .type = PIN_I32 }, first = { .type = PIN_I32, .i = 1 };
    pin_map_entry entry = { first, { .type = PIN_I32, .i = 10 } };
    pin_map *inner = pin_map_create(&cow_heap, integer, integer, &entry, 1);
    REQUIRE(inner);
    pin_value inner_value = { .type = PIN_I32, .container = PIN_CONTAINER_MAP, .ptr = inner };
    pin_value maps = { .type = PIN_I32, .container = PIN_CONTAINER_MAP };
    entry.value = inner_value;
    pin_map *outer = pin_map_create(&cow_heap, integer, maps, &entry, 1);
    REQUIRE(outer);
    pin_value original = { .type = PIN_I32, .container = PIN_CONTAINER_MAP, .ptr = outer },
              copy;
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copy, &original));
    pin_map *copied = copy.ptr;
    REQUIRE(!pin_map_make_unique(&cow_heap, copied));
    pin_map *child = copied->entries[0].value.ptr;
    pin_value replacement = { .type = PIN_I32, .i = 77 };
    REQUIRE(!pin_map_store_owned(&cow_heap, &child, &first, &replacement));
    pin_value second = { .type = PIN_I32, .i = 2 };
    REQUIRE(!pin_map_store_owned(&cow_heap, &child, &second, &replacement));
    copied->entries[0].value.ptr = child;
    pin_map_mutation_end(copied);
    REQUIRE(inner->count == 1 && inner->entries[0].value.i == 10);
    pin_map *before = outer->entries[0].value.ptr;
    REQUIRE(before->count == 1 && before->entries[0].value.i == 10);
    REQUIRE(child->count == 2 && child->entries[0].value.i == 77);
    REQUIRE(child->entries[1].key.i == 2);
    pin_value roots[] = { original, copy, inner_value };
    cow_collect(roots, 3);
    child = ((pin_map *)copy.ptr)->entries[0].value.ptr;
    REQUIRE(child->entries[1].value.i == 77);
    return true;
}

TEST_CASE(types_array_value_copy_keeps_explicit_pointer_identity)
{
    arena_init_collectable(&cow_heap);
    pin_pointer *target = arena_alloc(&cow_heap, sizeof *target);
    *target = (pin_pointer){ .value = { .type = PIN_I32, .i = 7 }, .storage = &cow_heap };
    pin_arena_trace_pointer(&cow_heap, target);
    pin_array shape = { .element_type = PIN_POINTER, .type_name = "test.pointer.int" };
    pin_array *array = pin_array_snapshot_like(&cow_heap, &shape, 1);
    REQUIRE(array);
    pin_value element = { .type = PIN_POINTER,
                          .type_name = "test.pointer.int",
                          .ptr = target };
    REQUIRE(!pin_array_set(array, 0, &element));
    REQUIRE(!pin_array_manage(&cow_heap, array));
    pin_value original = { .type = PIN_POINTER,
                           .container = PIN_CONTAINER_ARRAY,
                           .type_name = array->type_name,
                           .ptr = array },
              copy;
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copy, &original));
    target->value.i = 42;
    pin_value left = pin_array_get(original.ptr, 0),
              right = pin_array_get(copy.ptr, 0);
    REQUIRE(pin_value_equal(&left, &right));
    REQUIRE(((pin_pointer *)left.ptr)->value.i == 42);
    REQUIRE(((pin_pointer *)right.ptr)->value.i == 42);
    pin_pointer *replacement = arena_alloc(&cow_heap, sizeof *replacement);
    *replacement = (pin_pointer){ .value = { .type = PIN_I32, .i = 9 },
                                  .storage = &cow_heap };
    pin_arena_trace_pointer(&cow_heap, replacement);
    element.ptr = replacement;
    REQUIRE(!pin_array_snapshot_set(&cow_heap, copy.ptr, 0, &element));
    REQUIRE(pin_array_get(original.ptr, 0).ptr == target);
    REQUIRE(pin_array_get(copy.ptr, 0).ptr == replacement);
    pin_value roots[] = { original, copy };
    cow_collect(roots, 2);
    REQUIRE(arena_contains(&cow_heap, target) && arena_contains(&cow_heap, replacement));
    REQUIRE(((pin_pointer *)pin_array_get(original.ptr, 0).ptr)->value.i == 42);
    return true;
}

TEST_CASE(types_copied_ranges_and_pointer_views_follow_distinct_contracts)
{
    arena_init_collectable(&cow_heap);
    pin_value initial = cow_integers(4, 10);
    REQUIRE(initial.ptr);
    pin_pointer *cell = arena_alloc(&cow_heap, sizeof *cell);
    *cell = (pin_pointer){ .value = initial, .storage = &cow_heap };
    pin_arena_trace_pointer(&cow_heap, cell);
    pin_array *range = pin_array_window_copy(&cow_heap, initial.ptr, 1, 2);
    pin_array *view = pin_array_pointer_view(&cow_heap, cell, 1, 2);
    REQUIRE(range && view);
    pin_value view_value = { .type = PIN_I32,
                             .container = PIN_CONTAINER_VIEW,
                             .type_name = view->type_name,
                             .ptr = view },
              copied_view;
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copied_view, &view_value));
    pin_value changed = { .type = PIN_I32, .i = 111 };
    REQUIRE(!pin_array_snapshot_set(&cow_heap, view, 0, &changed));
    REQUIRE(pin_array_get(cell->value.ptr, 1).i == 111);
    REQUIRE(pin_array_get(range, 0).i == 11);
    REQUIRE(cow_append_integer(&cell->value, 14));
    REQUIRE(pin_array_get(view, 0).i == 111 && pin_array_get(view, 1).i == 12);
    pin_value replacement = cow_integers(3, 20);
    REQUIRE(replacement.ptr);
    pin_value_collection_release(&cell->value);
    cell->value = replacement;
    REQUIRE(pin_array_get(view, 0).i == 21 && pin_array_get(view, 1).i == 22);
    REQUIRE(pin_array_get(copied_view.ptr, 0).i == 21);
    REQUIRE(!pin_array_make_unique(&cow_heap, cell->value.ptr));
    ((pin_array *)cell->value.ptr)->count = 1;
    pin_array_mutation_end(cell->value.ptr);
    REQUIRE(!pin_array_view_valid(view) && !pin_array_view_valid(copied_view.ptr));
    REQUIRE(pin_array_snapshot_set(&cow_heap, view, 0, &changed) != 0);
    REQUIRE(cow_append_integer(&cell->value, 31));
    REQUIRE(!pin_array_view_valid(view));
    REQUIRE(cow_append_integer(&cell->value, 32));
    REQUIRE(pin_array_view_valid(view) && pin_array_view_valid(copied_view.ptr));
    REQUIRE(pin_array_get(view, 0).i == 31 && pin_array_get(view, 1).i == 32);
    REQUIRE(pin_array_get(range, 0).i == 11 && pin_array_get(range, 1).i == 12);
    REQUIRE(!pin_array_pointer_view(&cow_heap, cell, 2, 2));
    pin_array *empty = pin_array_pointer_view(&cow_heap, cell, 3, 0);
    REQUIRE(empty && pin_array_view_valid(empty));
    ((pin_array *)cell->value.ptr)->count = 2;
    pin_array_mutation_end(cell->value.ptr);
    REQUIRE(!pin_array_view_valid(empty));
    REQUIRE(cow_append_integer(&cell->value, 32));
    REQUIRE(pin_array_view_valid(empty));
    pin_value roots[] = {
        view_value,
        copied_view,
        { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = range }
    };
    cow_collect(roots, 3);
    REQUIRE(pin_array_get(view, 1).i == 32);
    REQUIRE(pin_array_get(range, 1).i == 12);
    return true;
}

TEST_CASE(types_string_views_outlive_source_and_clones_detach)
{
    const char expected[] = { 0, (char)0xff, 'b' };
    char bytes[] = { 'a', 0, (char)0xff, 'b', 'c' };
    pin_value original, slice, nested, clone, empty;
    REQUIRE(!pin_value_text(&cow_producer, &original, bytes, sizeof bytes));
    REQUIRE(!pin_value_text_slice(&snapshots, &slice, original, 1, 3));
    REQUIRE(!pin_value_text_slice(&snapshots, &nested, slice, 1, 1));
    REQUIRE(!pin_value_text_clone(&snapshots, &clone, slice));
    REQUIRE(!pin_value_text_clone(NULL, &cow_heap_text, slice));
    REQUIRE(!pin_value_text_slice(&snapshots, &empty, slice, 3, 0));
    REQUIRE(pin_text(empty).length == 0);
    REQUIRE(pin_text_equal(pin_text(slice), pin_text(clone)));
    REQUIRE(pin_text(clone).bytes != pin_text(slice).bytes);
    REQUIRE(pin_text(nested).bytes == pin_text(slice).bytes + 1);
    pin_value unchanged = { .type = PIN_I32, .i = 99 };
    REQUIRE(pin_value_text_slice(&snapshots, &unchanged, slice, SIZE_MAX, 1));
    REQUIRE(unchanged.type == PIN_I32 && unchanged.i == 99);
    bytes[2] = 'X';
    arena_reset(&cow_producer);
    arena_free(&cow_producer);
    REQUIRE(pin_text(slice).length == sizeof expected);
    REQUIRE(!memcmp(pin_text(slice).bytes, expected, sizeof expected));
    REQUIRE(pin_text(nested).length == 1 && (unsigned char)pin_text(nested).bytes[0] == 0xff);
    REQUIRE(!memcmp(pin_text(clone).bytes, expected, sizeof expected));
    arena_free(&snapshots);
    REQUIRE(pin_text(cow_heap_text).length == sizeof expected);
    REQUIRE(!memcmp(pin_text(cow_heap_text).bytes, expected, sizeof expected));
    return true;
}

TEST_CASE(types_set_value_copy_preserves_membership_through_growth)
{
    arena_init_collectable(&cow_heap);
    pin_array *set = NULL;
    for (int i = 0; i < 128; i++) {
        pin_value item = { .type = PIN_I32, .i = i };
        REQUIRE(pin_set_add_owned(&cow_heap, &set, &item) == 1);
    }
    pin_value original = { .type = PIN_I32, .container = PIN_CONTAINER_SET, .ptr = set },
              copy;
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copy, &original));
    pin_array *updated = copy.ptr;
    pin_value item = { .type = PIN_I32, .i = 128 };
    REQUIRE(pin_set_add_owned(&cow_heap, &updated, &item) == 1);
    copy.ptr = updated;
    REQUIRE(set->count == 128 && updated->count == 129);
    REQUIRE(pin_array_find(set, &item) == -1 && pin_array_find(updated, &item) == 128);
    for (int i = 0; i < 128; i++) {
        item.i = i;
        REQUIRE(pin_array_find(set, &item) == i);
        REQUIRE(pin_array_find(updated, &item) == i);
    }
    REQUIRE(pin_set_add_owned(&cow_heap, &updated, &item) == 0);
    REQUIRE(updated->count == 129 && set->count == 128);
    return true;
}

TEST_CASE(types_collection_owners_release_external_bytes_before_collection)
{
    arena_init_collectable(&cow_heap);
    pin_value original = cow_integers(8192, 0), copy;
    REQUIRE(original.ptr);
    REQUIRE(!pin_array_make_unique(&cow_heap, original.ptr));
    size_t retained = cow_heap.retained_bytes;
    REQUIRE(retained >= 8192 * sizeof(int32_t));
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copy, &original));
    REQUIRE(cow_heap.retained_bytes == retained);
    pin_value_collection_release(&original);
    REQUIRE(cow_heap.retained_bytes == retained);
    REQUIRE(pin_array_get(copy.ptr, 8191).i == 8191);
    pin_value_collection_release(&copy);
    REQUIRE(cow_heap.retained_bytes == 0);
    pin_value_collection_release(&copy);
    REQUIRE(cow_heap.retained_bytes == 0);
    cow_collect(NULL, 0);
    size_t used, capacity;
    arena_usage(&cow_heap, &used, &capacity);
    REQUIRE(used == 0);
    return true;
}

TEST_CASE(types_copy_drop_append_keeps_allocation_work_linear)
{
    arena_init_collectable(&cow_heap);
    pin_value value = cow_integers(0, 0);
    REQUIRE(value.ptr);
    REQUIRE(!pin_array_make_unique(&cow_heap, value.ptr));
    size_t allocated = cow_heap.allocated;
    enum
    {
        count = 2048
    };
    for (int i = 0; i < count; i++) {
        pin_value argument;
        REQUIRE(!pin_value_collection_copy(&cow_heap, &argument, &value));
        pin_value_collection_release(&argument);
        REQUIRE(cow_append_integer(&value, i));
    }
    REQUIRE(((pin_array *)value.ptr)->count == count);
    REQUIRE(pin_array_get(value.ptr, 0).i == 0);
    REQUIRE(pin_array_get(value.ptr, count - 1).i == count - 1);
    /* Copies charge one shell each. Dropping the temporary before append
     * permits geometric payload growth instead of copying every old prefix. */
    REQUIRE(
        cow_heap.allocated - allocated <
        (size_t)count * (sizeof(pin_array) + 16 * sizeof(int32_t))
    );
    pin_value_collection_release(&value);
    REQUIRE(cow_heap.retained_bytes == 0);
    return true;
}

TEST_CASE(types_queries_preserve_identity_and_managed_edges)
{
    arena_init_collectable(&snapshots);
    pin_query *query = arena_alloc(&snapshots, sizeof *query);
    *query = (pin_query){ .kind = PIN_QUERY_DESCRIPTION, .storage = &snapshots };
    pin_value original = { .type = PIN_QUERY, .type_name = "Query.Damage", .ptr = query };
    pin_value copy, snapshot;
    REQUIRE(!pin_value_copy(&copy, &original));
    REQUIRE(!pin_value_snapshot_to_arena(&snapshots, &snapshot, &original));
    REQUIRE(copy.ptr == query && snapshot.ptr == query);
    REQUIRE(pin_value_equal(&copy, &original));
    pin_query other = *query;
    copy.ptr = &other;
    REQUIRE(!pin_value_equal(&copy, &original));
    pin_value_destroy(&copy);

    pin_array boxed = { .element_type = PIN_QUERY,
                        .type_name = original.type_name,
                        .items = &original,
                        .count = 1,
                        .capacity = 1 };
    pin_array *packed = pin_array_snapshot_to_arena(&snapshots, &boxed);
    REQUIRE(packed && packed->stride == sizeof(void *));
    pin_value root = { .type = PIN_QUERY,
                       .container = PIN_CONTAINER_ARRAY,
                       .type_name = original.type_name,
                       .ptr = packed };
    REQUIRE(pin_value_managed_boundary_error(&root));
    arena *arenas[] = { &snapshots };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    pin_value_trace(&root, arena_collect_mark, &collection);
    arena_collect_end(&collection);
    REQUIRE(arena_contains(&snapshots, query));
    REQUIRE(pin_array_get(packed, 0).ptr == query);

    const type_field field = { .name = "matches",
                               .type = PIN_QUERY,
                               .type_name = original.type_name };
    const pin_record *record = pin_record_create(&snapshots, "QueryHolder", 9874, &field, 1);
    REQUIRE(record && record->type.size == sizeof(void *));
    pin_value holder = { .type = PIN_STRUCT,
                         .type_name = record->type.name,
                         .record = record,
                         .ptr = arena_alloc(&snapshots, record->type.size) };
    REQUIRE(!pin_write_field(&snapshots, holder.ptr, &record->type.fields[0], original));
    REQUIRE(!pin_read_field(&snapshots, holder.ptr, &record->type.fields[0], &copy));
    REQUIRE(copy.ptr == query && pin_value_equal(&copy, &original));
    REQUIRE(pin_value_managed_boundary_error(&holder));
    return true;
}
/* A query an array holds stays live while only the array's shared
 * backing reaches it. */
TEST_CASE(types_shared_backings_keep_their_queries)
{
    arena_init_collectable(&cow_heap);
    pin_query *query = arena_alloc(&cow_heap, sizeof *query);
    *query = (pin_query){ .kind = PIN_QUERY_DESCRIPTION, .storage = &cow_heap };
    pin_value item = { .type = PIN_QUERY, .type_name = "Query.Damage", .ptr = query };
    pin_array boxed = { .element_type = PIN_QUERY,
                        .type_name = item.type_name,
                        .items = &item,
                        .count = 1,
                        .capacity = 1 };
    pin_array *packed = pin_array_snapshot_to_arena(&cow_heap, &boxed);
    REQUIRE(packed && packed->stride == sizeof(void *));
    REQUIRE(!pin_array_manage(&cow_heap, packed));
    REQUIRE(!pin_array_make_unique(&cow_heap, packed) && packed->backing);
    pin_value root = { .type = PIN_QUERY,
                       .container = PIN_CONTAINER_ARRAY,
                       .type_name = packed->type_name,
                       .ptr = packed };
    cow_collect(&root, 1);
    REQUIRE(arena_contains(&cow_heap, query));
    pin_value kept = pin_array_get(packed, 0);
    REQUIRE(kept.ptr == query && ((pin_query *)kept.ptr)->kind == PIN_QUERY_DESCRIPTION);
    return true;
}
TEST_CASE(types_query_persistence_accepts_only_nil)
{
    pin_value value = { .type = PIN_QUERY, .type_name = "Query.Damage" };
    execution_wire writer = { 0 };
    execution_wire_value(&writer, &value);
    REQUIRE(!writer.error && writer.size);
    execution_wire reader = { .reading = true,
                              .data = writer.data,
                              .size = writer.size,
                              .storage = &snapshots };
    pin_value restored;
    execution_wire_value(&reader, &restored);
    REQUIRE(!reader.error && reader.offset == reader.size);
    REQUIRE(pin_value_equal(&value, &restored));
    afree(writer.data);

    source_document = av_doc_new();
    av_value *nil = pin_value_to_document(source_document, &value);
    REQUIRE(nil && av_is_null(nil));
    char error[192];
    REQUIRE(!pin_value_read_document(&snapshots, value, nil, &restored, error, sizeof error));
    REQUIRE(pin_value_equal(&value, &restored));
    REQUIRE(
        pin_value_read_document(&snapshots, value, av_obj(source_document), &restored, error, sizeof error)
    );
    REQUIRE(strstr(error, "Queries cannot be persisted"));

    pin_query query = { 0 };
    value.ptr = &query;
    writer = (execution_wire){ 0 };
    execution_wire_value(&writer, &value);
    REQUIRE(writer.error && strstr(writer.error, "Queries cannot be persisted"));
    afree(writer.data);
    REQUIRE(!pin_value_to_document(source_document, &value));
    return true;
}
TEST_CASE(types_owned_native_schemas_survive_catalog_and_source_retirement)
{
    typedef struct
    {
        int x, y;
    } pair;
    typedef struct
    {
        pair position;
        const char *text;
    } owner;
    char pair_name[] = "test.owned_pair", owner_name[] = "test.owned_owner";
    type_field pair_fields[] = {
        { .name = "x", .type = PIN_I32, .offset = offsetof(pair, x), .size = sizeof(int) },
        { .name = "y", .type = PIN_I32, .offset = offsetof(pair, y), .size = sizeof(int) }
    };
    type_desc pair_type = { .name = pair_name,
                            .size = sizeof(pair),
                            .fields = pair_fields,
                            .field_count = 2 };
    type_field owner_fields[] = { { .name = "position",
                                    .type = PIN_STRUCT,
                                    .offset = offsetof(owner, position),
                                    .size = sizeof(pair),
                                    .type_name = pair_name },
                                  { .name = "text",
                                    .type = PIN_STR,
                                    .offset = offsetof(owner, text),
                                    .size = sizeof(const char *) } };
    type_desc owner_type = { .name = owner_name,
                             .size = sizeof(owner),
                             .fields = owner_fields,
                             .field_count = 2 };
    const type_desc *types[] = { &pair_type, &owner_type };
    type_catalog_pack project = { .types = types, .type_count = 2 };
    type_catalog_set_project(&project);
    pointer_project_catalog = true;
    char text[] = "retained";
    owner original = { { 12, 34 }, text };
    arena input = { 0 }, copies = { 0 };
    const pin_schema *schema = pin_schema_create(&input, &owner_type, (type_resolver){ 0 });
    REQUIRE(schema);
    pin_value source_value = { .type = PIN_STRUCT,
                               .type_name = owner_name,
                               .schema = schema,
                               .ptr = &original };
    arena_init_collectable(&snapshots);
    pin_value retained;
    REQUIRE(!pin_value_snapshot_to_arena(&snapshots, &retained, &source_value));
    pin_value item = { .type = PIN_STRUCT,
                       .type_name = pair_name,
                       .schema = schema,
                       .ptr = &original.position };
    pin_array source_array = { .element_type = PIN_STRUCT,
                               .type_name = pair_name,
                               .count = 1,
                               .capacity = 1,
                               .items = &item };
    pin_array *packed = pin_array_snapshot_to_arena(&snapshots, &source_array);
    REQUIRE(packed && packed->schema && packed->stride == sizeof(pair));
    pin_value array = { .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = packed->type_name,
                        .schema = packed->schema,
                        .ptr = packed };
    pin_value native = source_value;
    native.schema = NULL;
    pin_map_entry entry = { .key = { .type = PIN_I32, .i = 7 }, .value = native };
    pin_map map = { .key_type = { .type = PIN_I32 },
                    .value_type = { .type = PIN_STRUCT, .type_name = owner_name },
                    .count = 1,
                    .capacity = 1,
                    .entries = &entry };
    pin_array native_rows = { .element_type = PIN_STRUCT,
                              .type_name = owner_name,
                              .count = 1,
                              .capacity = 1,
                              .items = &native };
    pin_value rows = { .type = PIN_STRUCT,
                       .container = PIN_CONTAINER_ARRAY,
                       .type_name = owner_name,
                       .ptr = &native_rows };
    pin_array nested_rows = { .element_type = PIN_STRUCT,
                              .type_name = "__collection_native_rows",
                              .count = 1,
                              .capacity = 1,
                              .items = &rows };
    pin_union
        *payload = arena_alloc(&input, sizeof *payload + 2 * sizeof *payload->values);
    *payload = (pin_union){ .tag = 99, .count = 2 };
    payload->values[0] = (pin_value){ .type = PIN_UNION,
                                      .container = PIN_CONTAINER_MAP,
                                      .type_name = "__map_int_native",
                                      .ptr = &map };
    payload->values[1] = (pin_value){ .type = PIN_STRUCT,
                                      .container = PIN_CONTAINER_ARRAY,
                                      .type_name = nested_rows.type_name,
                                      .ptr = &nested_rows };
    pin_value borrowed = { .type = PIN_UNION, .type_name = "NestedNative", .ptr = payload },
              captured;
    REQUIRE(!pin_value_snapshot_owned_to_arena(&snapshots, &captured, &borrowed));
    type_catalog_set_project(NULL);
    pointer_project_catalog = false;
    arena_free(&input);
    memset(text, 'x', sizeof text - 1);
    memset(pair_name, 'x', sizeof pair_name - 1);
    memset(owner_name, 'x', sizeof owner_name - 1);
    pair_type.field_count = owner_type.field_count = 0;
    original.position.x = -1;
    arena *arenas[] = { &snapshots };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    pin_value_trace(&retained, arena_collect_mark, &collection);
    pin_value_trace(&array, arena_collect_mark, &collection);
    pin_value_trace(&captured, arena_collect_mark, &collection);
    arena_collect_end(&collection);
    REQUIRE(pin_value_type(&retained) && pin_value_type(&retained)->field_count == 2);
    REQUIRE(pin_array_valid(packed));
    pin_value element = pin_array_get(packed, 0), nested;
    REQUIRE(element.schema && pin_value_type(&element)->size == sizeof(pair));
    REQUIRE(((pair *)element.ptr)->x == 12);
    REQUIRE(
        !pin_read_field(&copies, retained.ptr, &pin_value_type(&retained)->fields[0], &nested)
    );
    nested.schema = retained.schema;
    pin_value detached, copied_owner;
    REQUIRE(!pin_value_copy_to_arena(&copies, &detached, &nested));
    REQUIRE(!pin_value_copy_to_arena(&copies, &copied_owner, &retained));
    REQUIRE(pin_value_equal(&retained, &copied_owner));
    source_document = av_doc_new();
    av_value *document = pin_value_to_document(source_document, &copied_owner);
    REQUIRE(document && av_is_obj(document));
    REQUIRE(av_get_sint(av_obj_get(av_obj_get(document, "position"), "x")) == 12);
    execution_wire writer = { 0 };
    execution_wire_value(&writer, &copied_owner);
    REQUIRE(!writer.error && writer.size);
    afree(writer.data);
    pin_array *copied_array = pin_array_snapshot_to_arena(&copies, packed);
    REQUIRE(copied_array && pin_array_equal(packed, copied_array));
    array.schema = NULL;
    pin_value copied_array_value;
    REQUIRE(pin_value_type(&array));
    REQUIRE(!pin_value_copy_to_arena(&copies, &copied_array_value, &array));
    REQUIRE(copied_array_value.schema && pin_value_type(&copied_array_value));
    pin_value copied_capture;
    REQUIRE(!pin_value_snapshot_owned_to_arena(&copies, &copied_capture, &captured));
    REQUIRE(pin_value_equal(&captured, &copied_capture));
    REQUIRE(pin_value_to_document(source_document, &copied_capture));
    writer = (execution_wire){ 0 };
    execution_wire_value(&writer, &copied_capture);
    REQUIRE(!writer.error && writer.size);
    afree(writer.data);
    arena_free(&snapshots);
    REQUIRE(pin_value_type(&detached)->size == sizeof(pair));
    REQUIRE(((pair *)detached.ptr)->x == 12 && ((pair *)detached.ptr)->y == 34);
    REQUIRE(!strcmp(((owner *)copied_owner.ptr)->text, "retained"));
    REQUIRE(((pair *)pin_array_get(copied_array, 0).ptr)->x == 12);
    REQUIRE(((pair *)pin_array_get(copied_array_value.ptr, 0).ptr)->y == 34);
    const pin_union *kept = copied_capture.ptr;
    const pin_map *kept_map = kept->values[0].ptr;
    REQUIRE(kept_map->value_type.schema && kept_map->entries[0].value.schema);
    REQUIRE(!strcmp(((owner *)kept_map->entries[0].value.ptr)->text, "retained"));
    pin_value kept_rows = pin_array_get(kept->values[1].ptr, 0);
    pin_value kept_owner = pin_array_get(kept_rows.ptr, 0);
    REQUIRE(kept_owner.schema && pin_value_type(&kept_owner)->field_count == 2);
    REQUIRE(((owner *)kept_owner.ptr)->position.y == 34);
    arena_free(&copies);
    return true;
}
TEST_CASE(types_maps_reject_incompatible_carried_native_layouts)
{
    type_field first_field = { .name = "value", .type = PIN_I32, .size = sizeof(int32_t) };
    type_field second_field = { .name = "value", .type = PIN_F32, .size = sizeof(float) };
    type_desc first_type = { .name = "test.map_native_layout",
                             .size = sizeof(int32_t),
                             .fields = &first_field,
                             .field_count = 1 };
    type_desc second_type = { .name = first_type.name,
                              .size = sizeof(float),
                              .fields = &second_field,
                              .field_count = 1 };
    arena layouts = { 0 };
    const pin_schema *first = pin_schema_create(&layouts, &first_type, (type_resolver){ 0 });
    const pin_schema *second = pin_schema_create(&layouts, &second_type, (type_resolver){ 0 });
    REQUIRE(first && second);
    int32_t integer = 42;
    float real = 1.25f;
    pin_value key = { .type = PIN_I32, .i = 1 };
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = first_type.name,
                        .schema = first,
                        .ptr = &integer };
    pin_map_entry entry = { key, value };
    pin_map *map = pin_map_create(&snapshots, (pin_value){ .type = PIN_I32 }, value, &entry, 1);
    REQUIRE(map);
    pin_value compatible = value, incompatible = value;
    compatible.schema = pin_schema_snapshot(&snapshots, first);
    incompatible.schema = pin_schema_snapshot(&snapshots, second);
    incompatible.ptr = &real;
    REQUIRE(compatible.schema != first);
    arena_free(&layouts);
    REQUIRE(pin_map_entry_valid(map, &key, &compatible));
    REQUIRE(!pin_map_entry_valid(map, &key, &incompatible));
    REQUIRE(!pin_map_set(&snapshots, map, &key, &incompatible));
    REQUIRE(pin_map_store_owned(&snapshots, &map, &key, &incompatible));
    pin_map_entry bad = { key, incompatible };
    REQUIRE(!pin_map_create(&snapshots, map->key_type, map->value_type, &bad, 1));
    pin_map *other = pin_map_create(&snapshots, map->key_type, incompatible, &bad, 1);
    REQUIRE(other && !pin_map_equal(map, other));
    REQUIRE(map->count == 1 && *(const int32_t *)map->entries[0].value.ptr == 42);
    REQUIRE(pin_map_set(&snapshots, map, &key, &compatible));
    return true;
}

static const pin_record *retained_record_wire_layout(execution_wire *, void *, const pin_value *value, bool)
{
    return value->record;
}
TEST_CASE(types_authored_wrappers_retain_native_children)
{
    type_field native_fields[] = {
        { .name = "x", .type = PIN_I32, .size = sizeof(int32_t) },
        { .name = "y", .type = PIN_I32, .offset = sizeof(int32_t), .size = sizeof(int32_t) }
    };
    type_desc native = { .name = "test.captured_native_child",
                         .size = 2 * sizeof(int32_t),
                         .fields = native_fields,
                         .field_count = 2 };
    const type_desc *types[] = { &native };
    type_catalog_pack project = { .types = types, .type_count = 1 };
    type_catalog_set_project(&project);
    pointer_project_catalog = true;
    arena input = { 0 }, copies = { 0 };
    const type_field fields[] = {
        { .name = "inner", .type = PIN_STRUCT, .type_name = native.name },
        { .name = "label", .type = PIN_STR },
        { .name = "alias", .type = PIN_POINTER, .type_name = "^int" }
    };
    const pin_record *record = pin_record_create(&input, "CapturedWrapper", 9961, fields, 3);
    REQUIRE(record);
    arena_init_collectable(&snapshots);
    pin_pointer *pointer = arena_alloc(&snapshots, sizeof *pointer);
    *pointer = (pin_pointer){ .storage = &snapshots,
                              .value = { .type = PIN_I32, .i = 91 } };
    pin_arena_trace_pointer(&snapshots, pointer);
    pin_value wrapper = { .type = PIN_STRUCT,
                          .type_name = record->type.name,
                          .record = record,
                          .ptr = arena_alloc(&input, record->type.size) };
    memset(wrapper.ptr, 0, record->type.size);
    int32_t coordinates[] = { 12, 34 };
    memcpy(wrapper.ptr, coordinates, sizeof coordinates);
    pin_value label;
    REQUIRE(!pin_value_text(&input, &label, "retained", 8));
    REQUIRE(!pin_write_field(&input, wrapper.ptr, &record->type.fields[1], label));
    REQUIRE(!pin_write_field(
        &input,
        wrapper.ptr,
        &record->type.fields[2],
        (pin_value){ .type = PIN_POINTER, .type_name = "^int", .ptr = pointer }
    ));
    pin_map_entry entry = { .key = { .type = PIN_I32, .i = 7 }, .value = wrapper };
    pin_map map = { .key_type = { .type = PIN_I32 },
                    .value_type = wrapper,
                    .entries = &entry,
                    .count = 1,
                    .capacity = 1 };
    map.value_type.ptr = NULL;
    pin_array array = { .element_type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record,
                        .items = &wrapper,
                        .count = 1,
                        .capacity = 1 };
    pin_array empty = array;
    empty.items = NULL;
    empty.count = empty.capacity = 0;
    pin_union
        *payload = arena_alloc(&input, sizeof *payload + 4 * sizeof *payload->values);
    *payload = (pin_union){ .tag = 9962, .count = 4 };
    payload->values[0] = wrapper;
    payload->values[1] = (pin_value){ .type = PIN_UNION,
                                      .container = PIN_CONTAINER_MAP,
                                      .type_name = "__map_int_wrapper",
                                      .ptr = &map };
    payload->values[2] = (pin_value){ .type = PIN_STRUCT,
                                      .container = PIN_CONTAINER_ARRAY,
                                      .type_name = record->type.name,
                                      .record = record,
                                      .ptr = &array };
    payload->values[3] = payload->values[2];
    payload->values[3].ptr = &empty;
    pin_value borrowed = { .type = PIN_UNION, .type_name = "WrappedCapture", .ptr = payload },
              captured;
    REQUIRE(!pin_value_snapshot_owned_to_arena(&snapshots, &captured, &borrowed));
    type_catalog_set_project(NULL);
    pointer_project_catalog = false;
    native_fields[0].type = PIN_F32;
    native.field_count = 0;
    arena_free(&input);
    arena *arenas[] = { &snapshots };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    pin_value_trace(&captured, arena_collect_mark, &collection);
    arena_collect_end(&collection);
    REQUIRE(arena_contains(&snapshots, pointer) && pointer->value.i == 91);
    pin_value copied;
    REQUIRE(!pin_value_snapshot_owned_to_arena(&copies, &copied, &captured));
    REQUIRE(pin_value_equal(&captured, &copied));
    const pin_union *kept = copied.ptr;
    const pin_array *kept_empty = kept->values[3].ptr;
    REQUIRE(kept_empty->count == 0 && kept_empty->schema && kept_empty->record);
    REQUIRE(pin_value_record_current(&kept->values[3]));
    const pin_value values[] = {
        kept->values[0],
        ((const pin_map *)kept->values[1].ptr)->entries[0].value,
        pin_array_get(kept->values[2].ptr, 0)
    };
    source_document = av_doc_new();
    for (int i = 0; i < 3; i++) {
        REQUIRE(values[i].record && values[i].schema && pin_value_record_current(&values[i]));
        /* The copy's record carries its native child's layout too. */
        REQUIRE(values[i].record->frozen && pin_record_current(values[i].record));
        pin_value nested;
        REQUIRE(
            !pin_read_field(&copies, values[i].ptr, &values[i].record->type.fields[0], &nested)
        );
        nested.schema = values[i].schema;
        REQUIRE(pin_value_type(&nested)->field_count == 2);
        REQUIRE(
            ((const int32_t *)nested.ptr)[0] == 12 &&
            ((const int32_t *)nested.ptr)[1] == 34
        );
        pin_value exported = values[i];
        *(pin_pointer **)((char *)exported.ptr +
                          exported.record->type.fields[2].offset) = NULL;
        REQUIRE(pin_value_to_document(source_document, &exported));
        execution_wire writer = { 0 };
        execution_wire_value_owned(&writer, &exported, retained_record_wire_layout, NULL);
        REQUIRE(!writer.error && writer.size);
        afree(writer.data);
    }
    REQUIRE(pin_value_to_document(source_document, &copied));
    execution_wire writer = { 0 };
    execution_wire_value_owned(&writer, &copied, retained_record_wire_layout, NULL);
    REQUIRE(!writer.error && writer.size);
    afree(writer.data);
    arena_free(&copies);
    return true;
}

TEST_CASE(types_compositional_layout_preflight_bounds_repeated_children)
{
    enum
    {
        LEVELS = 9,
        FANOUT = 10
    };
    type_desc types[LEVELS] = { 0 };
    type_field fields[LEVELS][FANOUT] = { 0 };
    char names[LEVELS][32];
    for (int level = 0; level < LEVELS; level++) {
        snprintf(names[level], sizeof names[level], "Bounded.Level%d", level);
        types[level] = (type_desc){ .name = names[level],
                                    .fields = fields[level],
                                    .field_count = level ? FANOUT : 1,
                                    .size = level ? FANOUT * sizeof(pin_array *)
                                                  : sizeof(int32_t) };
        for (int field = 0; field < types[level].field_count; field++) {
            static const char *const labels[] = { "a", "b", "c", "d", "e",
                                                  "f", "g", "h", "i", "j" };
            fields[level][field] = (type_field){
                .name = labels[field],
                .type = level ? PIN_STRUCT : PIN_I32,
                .container = level ? PIN_CONTAINER_ARRAY : PIN_CONTAINER_SINGLE,
                .offset = level ? (size_t)field * sizeof(pin_array *) : 0,
                .size = level ? sizeof(pin_array *) : sizeof(int32_t),
                .type_name = level ? types[level - 1].name : NULL,
                .layout = level ? &types[level - 1] : NULL
            };
        }
    }
    const pin_record *small = pin_record_from_type(&snapshots, &types[2]);
    REQUIRE(small && pin_record_valid(small));
    REQUIRE(small->type.size == FANOUT * sizeof(pin_array *));
    type_field field = { .name = "rows",
                         .type = PIN_STRUCT,
                         .container = PIN_CONTAINER_ARRAY,
                         .type_name = types[LEVELS - 1].name,
                         .layout = &types[LEVELS - 1] };
    size_t before = arena_storage_bytes(&snapshots);
    REQUIRE(!pin_record_from_type(&snapshots, &types[LEVELS - 1]));
    REQUIRE(!pin_record_create_bytes("Bounded.Parent", 9998, &field, 1));
    REQUIRE(!pin_record_create(&snapshots, "Bounded.Parent", 9998, &field, 1));
    REQUIRE(arena_storage_bytes(&snapshots) == before);
    return true;
}

TEST_CASE(types_native_boundary_rejects_unsupported_container_fields)
{
    type_field field = { .name = "values", .type = PIN_I32, .size = sizeof(int32_t) };
    type_desc type = { .name = "test.unsupported_container_field",
                       .size = sizeof(int32_t),
                       .fields = &field,
                       .field_count = 1 };
    const type_desc *types[] = { &type };
    type_catalog_pack pack = { .types = types, .type_count = 1 };
    type_catalog_set_project(&pack);
    pointer_project_catalog = true;
    int32_t payload = 0;
    pin_value value = { .type = PIN_STRUCT, .type_name = type.name, .ptr = &payload };
    const pin_container unsupported[] = { PIN_CONTAINER_SET,
                                          PIN_CONTAINER_MAP,
                                          PIN_CONTAINER_VIEW };
    for (int i = 0; i < 3; i++) {
        field.container = unsupported[i];
        REQUIRE(pin_value_managed_boundary_error(&value));
    }
    type_catalog_set_project(NULL);
    pointer_project_catalog = false;
    return true;
}

TEST_CASE(types_compositional_record_depth_counts_records_once)
{
    for (int arrays_only = 0; arrays_only < 2; arrays_only++) {
        type_field scalar = { .name = "value", .type = PIN_I32 };
        const pin_record *record = pin_record_create(&snapshots, "Depth.Leaf", 9980, &scalar, 1);
        REQUIRE(record && pin_record_valid(record));
        for (int level = 1; level < TYPE_MAX_NESTING; level++) {
            char name[64];
            snprintf(name, sizeof name, "Depth.%d.Level%d", arrays_only, level);
            type_field field = { .name = "next",
                                 .type = PIN_STRUCT,
                                 .container = arrays_only || level % 2
                                                ? PIN_CONTAINER_ARRAY
                                                : PIN_CONTAINER_SINGLE,
                                 .type_name = record->type.name,
                                 .layout = &record->type };
            record = pin_record_create(&snapshots, name, 9980 + (uint64_t)level, &field, 1);
            REQUIRE(record && pin_record_valid(record));
        }
        type_field extra = { .name = "next",
                             .type = PIN_STRUCT,
                             .container = PIN_CONTAINER_ARRAY,
                             .type_name = record->type.name,
                             .layout = &record->type };
        REQUIRE(!pin_record_create_bytes("Depth.TooDeep", 9999, &extra, 1));
        REQUIRE(!pin_record_create(&snapshots, "Depth.TooDeep", 9999, &extra, 1));
        const pin_record *copy = pin_record_snapshot(&cow_producer, record);
        REQUIRE(copy && pin_record_valid(copy) && pin_record_same(copy, record));
    }
    return true;
}

TEST_CASE(types_compositional_records_preserve_child_nominal_identity)
{
    const type_field scalar = { .name = "value", .type = PIN_I32 };
    const pin_record *first = pin_record_create(&snapshots, "Nominal.Child", 92, &scalar, 1);
    const pin_record *second = pin_record_create(&snapshots, "Nominal.Child", 93, &scalar, 1);
    REQUIRE(first && second && first->layout == second->layout);
    const pin_record *parents[2] = { 0 };
    for (int array = 0; array < 2; array++) {
        type_field field = { .name = "child",
                             .type = PIN_STRUCT,
                             .container = array ? PIN_CONTAINER_ARRAY : PIN_CONTAINER_SINGLE,
                             .type_name = first->type.name,
                             .layout = &first->type };
        const pin_record *left = pin_record_create(&snapshots, "Nominal.Parent", 94, &field, 1);
        field.layout = &second->type;
        const pin_record *right = pin_record_create(&snapshots, "Nominal.Parent", 94, &field, 1);
        REQUIRE(left && right && pin_record_valid(left) && pin_record_valid(right));
        REQUIRE(left->frozen && right->frozen && left->layout == right->layout);
        REQUIRE(!pin_record_same(left, right));
        REQUIRE(!pin_value_layout_same(
            &(pin_value){ .type = PIN_STRUCT, .type_name = left->type.name, .record = left },
            &(pin_value){ .type = PIN_STRUCT, .type_name = right->type.name, .record = right }
        ));
        if (!array) {
            parents[0] = left;
            parents[1] = right;
        }
    }
    type_desc native = first->type;
    native.carrier = NULL;
    const type_desc *native_types[] = { &native };
    const type_catalog_pack pack = { .types = native_types, .type_count = 1 };
    type_catalog_set_project(&pack);
    pointer_project_catalog = true;
    type_field field = { .name = "child",
                         .type = PIN_STRUCT,
                         .type_name = native.name };
    const pin_record *legacy = pin_record_create(&snapshots, "Nominal.Parent", 94, &field, 1);
    REQUIRE(legacy && !legacy->frozen && pin_record_valid(legacy));
    for (int i = 0; i < 2; i++) {
        REQUIRE(parents[i]->layout == legacy->layout);
        REQUIRE(!pin_record_same(parents[i], legacy));
        REQUIRE(!pin_record_same(legacy, parents[i]));
        REQUIRE(!pin_value_layout_same(
            &(pin_value){ .type = PIN_STRUCT,
                          .type_name = parents[i]->type.name,
                          .record = parents[i] },
            &(pin_value){ .type = PIN_STRUCT, .type_name = legacy->type.name, .record = legacy }
        ));
    }
    const pin_record *owned_native = pin_record_from_type(&snapshots, &native);
    REQUIRE(owned_native && owned_native->native && pin_record_valid(owned_native));
    REQUIRE(pin_value_layout_same(
        &(pin_value){ .type = PIN_STRUCT, .type_name = native.name, .record = owned_native },
        &(pin_value){ .type = PIN_STRUCT, .type_name = native.name }
    ));
    field.layout = &owned_native->type;
    const pin_record *native_parent = pin_record_create(&snapshots, "Nominal.Parent", 94, &field, 1);
    REQUIRE(
        native_parent && native_parent->frozen &&
        native_parent->layout == legacy->layout
    );
    REQUIRE(pin_record_same(native_parent, legacy) && pin_record_same(legacy, native_parent));
    type_catalog_set_project(NULL);
    pointer_project_catalog = false;
    return true;
}

TEST_CASE(types_compositional_records_copy_arrays_and_retain_pointer_identity)
{
    arena_init_collectable(&cow_heap);
    const type_field child_fields[] = {
        { .name = "values", .type = PIN_I32, .container = PIN_CONTAINER_ARRAY },
        { .name = "shared", .type = PIN_POINTER, .type_name = "^int" }
    };
    const pin_record *child = pin_record_create(&snapshots, "Composition.Child", 9971, child_fields, 2);
    REQUIRE(child && pin_record_valid(child));
    const type_field fields[] = { { .name = "child",
                                    .type = PIN_STRUCT,
                                    .type_name = child->type.name,
                                    .layout = &child->type },
                                  { .name = "rows",
                                    .type = PIN_STRUCT,
                                    .container = PIN_CONTAINER_ARRAY,
                                    .type_name = child->type.name,
                                    .layout = &child->type } };
    const pin_record *record = pin_record_create(&cow_heap, "Composition.Parent", 9972, fields, 2);
    REQUIRE(record && pin_record_valid(record) && record->frozen);
    REQUIRE(record->type.fields[0].layout != &child->type);
    REQUIRE(record->type.fields[0].layout->carrier->id == child->id);
    REQUIRE(!record->type.fields[0].layout->carrier->native);
    pin_pointer *pointer = arena_alloc(&cow_heap, sizeof *pointer);
    *pointer = (pin_pointer){ .value = { .type = PIN_I32, .i = 91 },
                              .storage = &cow_heap };
    pin_arena_trace_pointer(&cow_heap, pointer);
    pin_value integers = cow_integers(2, 10);
    REQUIRE(integers.ptr);
    void *child_data = arena_alloc(&snapshots, child->type.size);
    memset(child_data, 0, child->type.size);
    REQUIRE(!pin_write_field(&snapshots, child_data, &child->type.fields[0], integers));
    REQUIRE(!pin_write_field(
        &snapshots,
        child_data,
        &child->type.fields[1],
        (pin_value){ .type = PIN_POINTER, .type_name = "^int", .ptr = pointer }
    ));
    pin_value child_value = { .type = PIN_STRUCT,
                              .type_name = child->type.name,
                              .record = child,
                              .ptr = child_data };
    pin_array shape = { .element_type = PIN_STRUCT,
                        .type_name = child->type.name,
                        .record = child };
    pin_array *rows = pin_array_snapshot_like(&cow_heap, &shape, 1);
    REQUIRE(rows && !pin_array_set(rows, 0, &child_value));
    void *data = arena_alloc(&cow_heap, record->type.size);
    memset(data, 0, record->type.size);
    REQUIRE(!pin_write_field(&cow_heap, data, &record->type.fields[0], child_value));
    REQUIRE(!pin_write_field(
        &cow_heap,
        data,
        &record->type.fields[1],
        (pin_value){ .type = PIN_STRUCT,
                     .container = PIN_CONTAINER_ARRAY,
                     .type_name = child->type.name,
                     .record = child,
                     .ptr = rows }
    ));
    pin_arena_trace_struct(&cow_heap, data, &record->type);
    pin_value original = { .type = PIN_STRUCT,
                           .type_name = record->type.name,
                           .record = record,
                           .ptr = data },
              copy;
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copy, &original));
    arena_free(&snapshots);
    pin_value first_child, copied_child, before, after, first_pointer, copied_pointer;
    REQUIRE(!pin_read_field(&cow_heap, original.ptr, &record->type.fields[0], &first_child));
    REQUIRE(!pin_read_field(&cow_heap, copy.ptr, &record->type.fields[0], &copied_child));
    REQUIRE(
        !pin_read_field(&cow_heap, first_child.ptr, &first_child.record->type.fields[0], &before)
    );
    REQUIRE(
        !pin_read_field(&cow_heap, copied_child.ptr, &copied_child.record->type.fields[0], &after)
    );
    REQUIRE(cow_append_integer(&after, 77));
    REQUIRE(
        ((pin_array *)before.ptr)->count == 2 && pin_array_get(before.ptr, 1).i == 11
    );
    REQUIRE(((pin_array *)after.ptr)->count == 3 && pin_array_get(after.ptr, 2).i == 77);
    REQUIRE(
        !pin_read_field(&cow_heap, first_child.ptr, &first_child.record->type.fields[1], &first_pointer)
    );
    REQUIRE(
        !pin_read_field(&cow_heap, copied_child.ptr, &copied_child.record->type.fields[1], &copied_pointer)
    );
    REQUIRE(first_pointer.ptr == pointer && copied_pointer.ptr == pointer);
    pointer->value.i = 103;
    pin_value roots[] = { original, copy };
    cow_collect(roots, 2);
    REQUIRE(
        arena_contains(&cow_heap, pointer) &&
        ((pin_pointer *)copied_pointer.ptr)->value.i == 103
    );
    pin_value original_rows, copied_rows;
    REQUIRE(!pin_read_field(&cow_heap, original.ptr, &record->type.fields[1], &original_rows));
    REQUIRE(!pin_read_field(&cow_heap, copy.ptr, &record->type.fields[1], &copied_rows));
    REQUIRE(
        ((pin_array *)original_rows.ptr)->count == 1 &&
        ((pin_array *)copied_rows.ptr)->count == 1
    );
    pin_value_collection_release(&copy);
    REQUIRE(
        ((pin_array *)before.ptr)->count == 2 && pin_array_get(before.ptr, 0).i == 10
    );
    cow_collect(&original, 1);
    REQUIRE(arena_contains(&cow_heap, pointer) && pointer->value.i == 103);
    return true;
}

static const pin_record *compositional_wire_layout(
    execution_wire *wire,
    void *user,
    const pin_value *value,
    bool authored
)
{
    if (!wire->reading) {
        return value->record;
    }
    type_resolver resolver = pin_record_resolver(user);
    const type_desc *type = authored ? type_resolve(resolver, value->type_name) : NULL;
    return type ? type->carrier : NULL;
}
TEST_CASE(types_compositional_record_documents_keep_owned_child_layouts)
{
    type_field scalar = { .name = "value", .type = PIN_I32 };
    const pin_record *child = pin_record_create(&snapshots, "Document.Child", 9973, &scalar, 1);
    REQUIRE(child);
    const type_field fields[] = {
        { .name = "child",
          .type = PIN_STRUCT,
          .type_name = child->type.name,
          .layout = &child->type },
        { .name = "values", .type = PIN_I32, .container = PIN_CONTAINER_ARRAY }
    };
    const pin_record *parent = pin_record_create(&cow_producer, "Document.Parent", 9974, fields, 2);
    REQUIRE(parent && pin_record_valid(parent));
    int32_t integer = 37;
    pin_value row = { .type = PIN_STRUCT,
                      .record = child,
                      .type_name = child->type.name,
                      .ptr = &integer };
    pin_array *values = pin_array_allocate(&cow_producer, PIN_I32, NULL, 2);
    REQUIRE(values);
    pin_value item = { .type = PIN_I32, .i = 12 };
    REQUIRE(!pin_array_set(values, 0, &item));
    item.i = 34;
    REQUIRE(!pin_array_set(values, 1, &item));
    void *data = arena_alloc(&cow_producer, parent->type.size);
    memset(data, 0, parent->type.size);
    REQUIRE(!pin_write_field(&cow_producer, data, &parent->type.fields[0], row));
    REQUIRE(!pin_write_field(
        &cow_producer,
        data,
        &parent->type.fields[1],
        (pin_value){ .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = values }
    ));
    pin_value original = { .type = PIN_STRUCT,
                           .record = parent,
                           .type_name = parent->type.name,
                           .ptr = data },
              kept;
    REQUIRE(!pin_value_collection_copy(&cow_heap, &kept, &original));
    arena_free(&snapshots);
    arena_free(&cow_producer);
    source_document = av_doc_new();
    av_value *document = pin_value_to_document(source_document, &kept);
    REQUIRE(document);
    pin_value restored;
    char error[192];
    REQUIRE(!pin_value_read_document(&cow_heap, kept, document, &restored, error, sizeof error));
    REQUIRE(pin_value_equal(&kept, &restored));
    pin_value nested, numbers;
    REQUIRE(
        !pin_read_field(&cow_heap, restored.ptr, &restored.record->type.fields[0], &nested)
    );
    REQUIRE(nested.record && !nested.record->native && *(int32_t *)nested.ptr == 37);
    REQUIRE(
        !pin_read_field(&cow_heap, restored.ptr, &restored.record->type.fields[1], &numbers)
    );
    REQUIRE(
        ((pin_array *)numbers.ptr)->count == 2 &&
        pin_array_get(numbers.ptr, 0).i == 12 && pin_array_get(numbers.ptr, 1).i == 34
    );
    execution_wire writer = { 0 };
    execution_wire_value_owned(
        &writer,
        &kept,
        compositional_wire_layout,
        (void *)kept.record
    );
    REQUIRE(!writer.error && writer.size);
    execution_wire reader = { .data = writer.data,
                              .size = writer.size,
                              .reading = true,
                              .storage = &cow_heap };
    pin_value decoded = { 0 };
    execution_wire_value_owned(
        &reader,
        &decoded,
        compositional_wire_layout,
        (void *)kept.record
    );
    afree(writer.data);
    REQUIRE(!reader.error && reader.offset == reader.size);
    REQUIRE(pin_value_equal(&kept, &decoded));
    pin_value snapshot;
    REQUIRE(!pin_value_snapshot_owned_to_arena(&cow_heap, &snapshot, &kept));
    REQUIRE(pin_value_equal(&kept, &snapshot));

    return true;
}

void test_tool_types()
{
    pu_setup(NULL, release_values);
    RUN_TEST_CASE(types_compositional_layout_preflight_bounds_repeated_children);
    RUN_TEST_CASE(types_native_boundary_rejects_unsupported_container_fields);
    RUN_TEST_CASE(types_compositional_record_depth_counts_records_once);
    RUN_TEST_CASE(types_compositional_records_preserve_child_nominal_identity);
    RUN_TEST_CASE(types_compositional_records_copy_arrays_and_retain_pointer_identity);
    RUN_TEST_CASE(types_compositional_record_documents_keep_owned_child_layouts);
    RUN_TEST_CASE(types_array_value_copy_isolates_elements_and_length);
    RUN_TEST_CASE(types_nested_array_value_copy_isolates_child_mutation);
    RUN_TEST_CASE(types_map_value_copy_preserves_order_and_isolates_updates);
    RUN_TEST_CASE(types_map_value_copy_isolates_nested_array_values);
    RUN_TEST_CASE(types_nested_map_value_copy_isolates_child_updates);
    RUN_TEST_CASE(types_array_value_copy_keeps_explicit_pointer_identity);
    RUN_TEST_CASE(types_copied_ranges_and_pointer_views_follow_distinct_contracts);
    RUN_TEST_CASE(types_string_views_outlive_source_and_clones_detach);
    RUN_TEST_CASE(types_set_value_copy_preserves_membership_through_growth);
    RUN_TEST_CASE(types_collection_owners_release_external_bytes_before_collection);
    RUN_TEST_CASE(types_copy_drop_append_keeps_allocation_work_linear);
    RUN_TEST_CASE(types_maps_reject_incompatible_carried_native_layouts);
    RUN_TEST_CASE(types_authored_wrappers_retain_native_children);
    RUN_TEST_CASE(types_owned_native_schemas_survive_catalog_and_source_retirement);
    RUN_TEST_CASE(types_queries_preserve_identity_and_managed_edges);
    RUN_TEST_CASE(types_shared_backings_keep_their_queries);
    RUN_TEST_CASE(types_query_persistence_accepts_only_nil);
    RUN_TEST_CASE(types_nested_pointer_records_require_owned_layout_support);
    RUN_TEST_CASE(types_document_array_collection_retains_split_payloads);
    RUN_TEST_CASE(types_authored_record_document_uses_nested_native_layout);
    RUN_TEST_CASE(types_catalog_composes_ordered_packs);
    RUN_TEST_CASE(types_nested_reflection_reads_writes_and_checks_layout);
    RUN_TEST_CASE(types_array_snapshot_owns_strings_and_names);
    RUN_TEST_CASE(types_collection_follows_values_across_arenas);
    RUN_TEST_CASE(types_collection_traces_packed_arrays_of_unknown_native_types);
    RUN_TEST_CASE(types_union_snapshots_and_heap_copies_own_nested_values);
    RUN_TEST_CASE(types_union_wire_roundtrip_traces_nested_arrays);
    RUN_TEST_CASE(types_plain_struct_array_codecs_preserve_inline_values_and_nulls);
    RUN_TEST_CASE(types_union_wire_rejects_invalid_tags_counts_and_depth);
    RUN_TEST_CASE(types_wire_strings_store_each_string_once);
    RUN_TEST_CASE(types_wire_strings_charge_both_tables_while_growing);
    RUN_TEST_CASE(types_struct_array_snapshot_retains_nested_payload);
    RUN_TEST_CASE(types_document_snapshot_survives_source_release);
    RUN_TEST_CASE(types_map_documents_own_nested_maps_arrays_and_native_structs);
    RUN_TEST_CASE(types_map_documents_require_authored_declarations_and_reject_malformed_input);
    RUN_TEST_CASE(types_document_rejects_invalid_values_without_publication);
    pu_clear_setup();
}
