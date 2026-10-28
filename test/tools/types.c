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
#include "runtime/component_catalog.h"
#include "test/support/reflected_types.h"
bool test_source_runtime_active();
#endif
#ifdef CHIBI_TEST_MATERIAL_GRAPHS
/* Material graphs keep colors and ramp stops in shader-typed defaults. */
bool test_material_graphs_active();
type_catalog_pack test_material_graph_types();
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
#ifdef CHIBI_TEST_NATIVE_RUNTIME
bool test_native_catalog_active();
type_catalog_pack test_native_catalog_pack();
#endif
#ifdef CHIBI_TEST_MODEL_IMPORT
/* Imports write shader-typed material defaults and game-typed prototypes. */
bool test_model_import_active();
type_catalog_pack test_model_import_types(int index);
#endif

#ifdef CHIBI_TEST_MODEL_GRAPHS
/* Model and shader graphs keep vectors and colors in shader-typed defaults,
 * and their render cases read game-typed components. */
bool test_graph_types_active();
type_catalog_pack test_graph_types(int index);
#endif
#ifdef CHIBI_TEST_SOURCE_FEATURES
#include "authoring/studio_schema.h"
bool test_shader_parameters_active();
type_catalog_pack test_shader_parameters_types();
#endif
int type_catalog_pack_count()
{
#ifdef CHIBI_TEST_MODEL_GRAPHS
    if (test_graph_types_active()) {
        return 2;
    }
#endif
#ifdef CHIBI_TEST_MODEL_IMPORT
    if (test_model_import_active()) {
        return 2;
    }
#endif
#ifdef CHIBI_TEST_MATERIAL_GRAPHS
    if (test_material_graphs_active()) {
        return 3;
    }
#endif
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    if (test_source_runtime_active()) {
#ifdef CHIBI_TEST_SOURCE_FEATURES
        return test_shader_parameters_active() ? 4 : 3;
#else
        return 2;
#endif
    }
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    if (test_native_catalog_active()) {
        return 3;
    }
#endif
    return 2;
}

type_catalog_pack type_catalog_pack_at(int index)
{
    assert(index >= 0 && index < type_catalog_pack_count());
#ifdef CHIBI_TEST_MODEL_GRAPHS
    if (test_graph_types_active()) {
        return test_graph_types(index);
    }
#endif
#ifdef CHIBI_TEST_MODEL_IMPORT
    if (test_model_import_active()) {
        return test_model_import_types(index);
    }
#endif
#ifdef CHIBI_TEST_MATERIAL_GRAPHS
    if (index == 2 && test_material_graphs_active()) {
        return test_material_graph_types();
    }
#endif
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    if (test_source_runtime_active()) {
#ifdef CHIBI_TEST_SOURCE_FEATURES
        if (index == 2) {
            return STUDIO_TYPE_PACK;
        }
        if (index == 3) {
            return test_shader_parameters_types();
        }
#endif
        return index ? TEST_REFLECTED_TYPES_PACK : TYPE_PACK;
    }
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    if (index == 2) {
        return test_native_catalog_pack();
    }
#endif
    if (index == 0) {
        return (type_catalog_pack){ first_types, 2, first_enums, 1 };
    }
    return (type_catalog_pack){ second_types, 4, second_enums, 1 };
}

static arena snapshots;
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
        pin_text(pin_array_get(snapshot, 0)).bytes !=
        pin_text(pin_array_get(source, 0)).bytes
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
    arena_collect_mark(&collection, &array->items[0]);
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
    REQUIRE(arena_contains(&snapshots, pin_text(text).bytes));
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
        REQUIRE(arena_contains(&snapshots, payload));
        pin_value item = pin_array_get(array, 0);
        if (strings) {
            REQUIRE(arena_contains(&snapshots, pin_text(item).bytes));
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
        REQUIRE(!pin_record_current(values[i].record));
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

void test_tool_types()
{
    pu_setup(NULL, release_values);
    RUN_TEST_CASE(types_maps_reject_incompatible_carried_native_layouts);
    RUN_TEST_CASE(types_authored_wrappers_retain_native_children);
    RUN_TEST_CASE(types_owned_native_schemas_survive_catalog_and_source_retirement);
    RUN_TEST_CASE(types_queries_preserve_identity_and_managed_edges);
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
