#include "execution/borrow.h"
#include "execution/intrinsics.h"
#include "runtime/arrays.h"
#include "runtime/text_format.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/copy.h"
#include "types/map.h"
#include "types/pointer.h"
#include "types/record.h"
#include "types/text.h"
#include "types/trace.h"
#include "types/union.h"
#include "types/value_document.h"
#include "types/value_wire.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
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
      .type_name = "float" },
    { .name = "y",
      .type = PIN_F32,
      .offset = offsetof(test_position, y),
      .size = sizeof(float),
      .type_name = "float" },
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
static arena cow_heap, cow_producer;
static pin_value cow_heap_text;
static pin_array *source;
static av_doc *source_document;
static bool pointer_project_catalog;

static pin_copy_scope ownership_scope;

static void release_values()
{
    if (ownership_scope.active) {
        pin_copy_scope_end(&ownership_scope);
    }
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
    av_obj_set(source_document, av_obj_get(leaf, "key_type"), "storage", av_str(source_document, "double"));
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
        REQUIRE(arena_contains(&snapshots, array));
        REQUIRE(arena_contains(&snapshots, payload));
        pin_value item = pin_array_get(array, 0);
        if (strings) {
            REQUIRE(arena_contains(&snapshots, item.text));
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

static size_t ownership_start;

static void ownership_begin()
{
    pin_copy_scope_reset(&ownership_scope);
    ownership_start = cow_heap.allocated;
    pin_copy_scope_begin(&ownership_scope);
}
static void ownership_end(const char *stage, const pin_array *outer, const pin_array *child)
{
    pin_copy_scope_end(&ownership_scope);
    const pin_copy_counts *c = &ownership_scope.counts;
    size_t used, capacity;
    arena_usage(&cow_heap, &used, &capacity);
    printf(
        "OWNERSHIP %s adopt=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64
        " detach=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64
        " shells=%" PRIu64 " headers=%" PRIu64 " buffer=%" PRIu64
        " record_bytes=%" PRIu64 " hits=%" PRIu64 " frozen_bypass=%" PRIu64
        " arena_allocated=%zu external_unique=%zu arena_used=%zu "
        "arena_capacity=%zu outer_refs=%zu "
        "child_refs=%zu overflow=%d\n",
        stage,
        c->array_adoption.allocations,
        c->array_adoption.allocated_bytes,
        c->array_adoption.packed_bytes,
        c->array_adoption.boxed_values,
        c->array_detach.allocations,
        c->array_detach.allocated_bytes,
        c->array_detach.packed_bytes,
        c->array_detach.boxed_values,
        c->array_owner_shells,
        c->value_header_moves,
        c->value_buffer_bytes,
        c->record_descriptor_bytes,
        c->record_cache_hits,
        c->record_frozen_cache_bypasses,
        cow_heap.allocated - ownership_start,
        cow_heap.retained_bytes,
        used,
        capacity,
        outer ? pin_array_backing_references(outer) : 0,
        child ? pin_array_backing_references(child) : 0,
        c->overflow
    );
}
static bool ownership_integers(pin_value value, const int *expected, int count)
{
    const pin_array *array = value.ptr;
    REQUIRE(value.container == PIN_CONTAINER_ARRAY && value.type == PIN_I32);
    REQUIRE(array && array->count == count);
    for (int i = 0; i < count; i++) {
        pin_value item = pin_array_get(array, i);
        REQUIRE(item.type == PIN_I32 && item.container == PIN_CONTAINER_SINGLE);
        REQUIRE(item.i == expected[i]);
    }
    return true;
}
TEST_CASE(ownership_raw_boxed_adoption_and_nested_detach)
{
    arena_init_collectable(&cow_heap);
    pin_value raw_child = cow_integers(2, 10);
    REQUIRE(raw_child.ptr);
    pin_array *rows = pin_array_snapshot_allocate(&cow_heap, PIN_STRUCT, "__collection_probe", 4);
    REQUIRE(rows);
    rows->count = 1;
    rows->items[0] = raw_child;
    REQUIRE(!pin_array_manage(&cow_heap, rows));
    pin_value original = { .type = PIN_STRUCT,
                           .container = PIN_CONTAINER_ARRAY,
                           .type_name = rows->type_name,
                           .ptr = rows },
              copy;
    REQUIRE(!rows->backing && !((pin_array *)raw_child.ptr)->backing);
    ownership_begin();
    REQUIRE(!pin_value_collection_copy(&cow_heap, &copy, &original));
    pin_value child = pin_array_get(copy.ptr, 0);
    ownership_end("boxed-first-copy", copy.ptr, child.ptr);
    const int old[] = { 10, 11 }, changed[] = { 99, 11 };
    REQUIRE(ownership_integers(raw_child, old, 2));
    REQUIRE(ownership_integers(child, old, 2));
    ownership_begin();
    REQUIRE(!pin_array_make_unique(&cow_heap, copy.ptr));
    child = pin_array_get(copy.ptr, 0);
    ownership_end("boxed-outer-detach", copy.ptr, child.ptr);
    ownership_begin();
    pin_value replacement = { .type = PIN_I32, .i = 99 };
    REQUIRE(!pin_array_snapshot_set(&cow_heap, child.ptr, 0, &replacement));
    ownership_end("boxed-child-detach", copy.ptr, child.ptr);
    REQUIRE(ownership_integers(child, changed, 2));
    REQUIRE(ownership_integers(pin_array_get(original.ptr, 0), old, 2));
    REQUIRE(ownership_integers(raw_child, old, 2));
    /* All calls have returned. These are the complete live roots. raw_child
     * borrows the retired constructor graph; never explicitly release it. */
    ownership_begin();
    ownership_end(
        "boxed-before-rooted-collection",
        original.ptr,
        pin_array_get(original.ptr, 0).ptr
    );
    pin_value roots[] = { original, copy };
    cow_collect(roots, 2);
    ownership_begin();
    ownership_end(
        "boxed-after-rooted-collection",
        original.ptr,
        pin_array_get(original.ptr, 0).ptr
    );
    REQUIRE(ownership_integers(pin_array_get(original.ptr, 0), old, 2));
    REQUIRE(ownership_integers(pin_array_get(copy.ptr, 0), changed, 2));
    return true;
}
TEST_CASE(ownership_readonly_drop_and_retained_append)
{
    const char *labels[] = { "native-length-append",
                             "copy-drop-append",
                             "retained-copy-append" };
    const int old[] = { 10, 11 }, appended[] = { 10, 11, 12 };
    for (int mode = 0; mode < 3; mode++) {
        arena_init_collectable(&cow_heap);
        pin_value original = cow_integers(2, 10), retained = { 0 };
        REQUIRE(original.ptr);
        ownership_begin();
        REQUIRE(!pin_array_make_unique(&cow_heap, original.ptr));
        ownership_end("packed-first-adoption", original.ptr, NULL);
        pin_value reference = execution_reference_value(&cow_heap, NULL, &original, original);
        ownership_begin();
        if (mode == 0) {
            pin_value length = native_array_length(&original, 1, &cow_heap);
            REQUIRE(length.type == PIN_I32 && length.i == 2);
        } else {
            REQUIRE(!pin_value_collection_copy(&cow_heap, &retained, &original));
            REQUIRE(ownership_integers(retained, old, 2));
            if (mode == 1) {
                pin_value_collection_release(&retained);
            }
        }
        pin_value arguments[] = { reference,
                                  { .type = PIN_I32, .i = EXECUTION_ARRAY_APPEND },
                                  { .type = PIN_I32, .i = 12 } },
                  result;
        REQUIRE(!EXECUTION_array_mutate_place_call.value(&cow_heap, arguments, 3, &result));
        REQUIRE(result.type == PIN_BOOL && result.b);
        ownership_end(labels[mode], original.ptr, NULL);
        REQUIRE(ownership_integers(original, appended, 3));
        if (mode == 2) {
            REQUIRE(ownership_integers(retained, old, 2));
        }
        pin_value roots[] = { original, mode == 2 ? retained : (pin_value){ 0 } };
        cow_collect(roots, 2);
        REQUIRE(ownership_integers(original, appended, 3));
        if (mode == 2) {
            REQUIRE(ownership_integers(retained, old, 2));
        }
        arena_free(&cow_heap);
    }
    return true;
}
TEST_CASE(ownership_repeated_native_slice_and_concat)
{
    const int expected[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    for (int count = 4; count <= 8; count *= 2) {
        arena_init_collectable(&cow_heap);
        pin_value source = cow_integers(count, 1), remaining = source,
                  result = cow_integers(0, 0);
        REQUIRE(source.ptr && result.ptr);
        ownership_begin();
        for (int i = 0; i < count; i++) {
            pin_value args[] = { remaining,
                                 { .type = PIN_I32, .i = 0 },
                                 { .type = PIN_I32, .i = 1 } };
            pin_value first = native_array_slice(args, 3, &cow_heap);
            REQUIRE(first.ptr && ownership_integers(first, expected + i, 1));
            pin_value concat[] = { result, first };
            result = native_array_extend(concat, 2, &cow_heap);
            REQUIRE(result.ptr && ownership_integers(result, expected, i + 1));
            args[1].i = 1;
            args[2].i = count - i - 1;
            remaining = native_array_slice(args, 3, &cow_heap);
            REQUIRE(
                remaining.ptr &&
                ownership_integers(remaining, expected + i + 1, count - i - 1)
            );
        }
        ownership_end(count == 4 ? "slice-concat-4" : "slice-concat-8", result.ptr, NULL);
        pin_value roots[] = { source, result };
        cow_collect(roots, 2);
        REQUIRE(ownership_integers(source, expected, count));
        REQUIRE(ownership_integers(result, expected, count));
        arena_free(&cow_heap);
    }
    return true;
}
TEST_CASE(ownership_record_cache_alternation)
{
    static const char *names[] = { "probe.A", "probe.B", "probe.C",
                                   "probe.D", "probe.E", "probe.F",
                                   "probe.G", "probe.H", "probe.I" };
    const type_field field = { .name = "value", .type = PIN_I32, .size = sizeof(int32_t) };
    for (int frozen = 0; frozen < 2; frozen++) {
        for (int layouts = 2; layouts <= 9; layouts += 7) {
            const pin_record *records[9], *copies[36];
            pin_value rows[36];
            arena_init_collectable(&cow_heap);
            for (int i = 0; i < layouts; i++) {
                type_desc type = { .name = names[i],
                                   .size = sizeof(int32_t),
                                   .fields = &field,
                                   .field_count = 1 };
                records[i] = frozen
                               ? pin_record_from_type(&cow_producer, &type)
                               : pin_record_create(&cow_producer, names[i], (uint64_t)i + 1, &field, 1);
                REQUIRE(records[i] && pin_record_valid(records[i]));
            }
            ownership_begin();
            for (int i = 0; i < 36; i++) {
                copies[i] = pin_record_snapshot(&cow_heap, records[i % layouts]);
                REQUIRE(copies[i] && pin_record_valid(copies[i]));
            }
            ownership_end(
                frozen ? (layouts == 2 ? "frozen-records-2" : "frozen-records-9")
                       : (layouts == 2 ? "flat-records-2" : "flat-records-9"),
                NULL,
                NULL
            );
            for (int i = 0; i < 36; i++) {
                void *data = arena_alloc(&cow_heap, copies[i]->type.size);
                pin_value value = { .type = PIN_I32, .i = 41 };
                REQUIRE(!pin_write_field(&cow_heap, data, &copies[i]->type.fields[0], value));
                rows[i] = (pin_value){ .type = PIN_STRUCT,
                                       .record = copies[i],
                                       .type_name = copies[i]->type.name,
                                       .ptr = data };
            }
            arena_free(&cow_producer);
            cow_collect(rows, 36);
            for (int i = 0; i < 36; i++) {
                REQUIRE(pin_record_valid(rows[i].record));
                REQUIRE(!strcmp(rows[i].type_name, names[i % layouts]));
                REQUIRE(!strcmp(rows[i].record->type.fields[0].name, "value"));
                pin_value value;
                REQUIRE(!pin_read_field(
                    &cow_heap,
                    rows[i].ptr,
                    &rows[i].record->type.fields[0],
                    &value
                ));
                REQUIRE(value.type == PIN_I32 && value.i == 41);
            }
            arena_free(&cow_heap);
        }
    }
    return true;
}
static bool ownership_text_value(pin_value value)
{
    static const char expected[] = { 'b', 0, 'c', 'd' };
    REQUIRE(value.type == PIN_STR && value.container == PIN_CONTAINER_SINGLE);
    pin_string text = pin_text(value);
    REQUIRE(text.owner && text.length == sizeof expected);
    REQUIRE(!memcmp(text.bytes, expected, sizeof expected));
    return true;
}
static void ownership_text_observe(const char *route, const char *phase, int count, bool retain, size_t payload)
{
    size_t used, capacity;
    arena_usage(&cow_heap, &used, &capacity);
    /* This isolated fixture stores only text descriptors. Each resource has
     * the same known backing length, including its trailing NUL. */
    printf(
        "TEXT route=%s arena=%s count=%d keep=%s phase=%s "
        "copied_text=%" PRIu64 " headers=%" PRIu64
        " resources=%zu payload_unique=%zu external_unique=%zu "
        "descriptor_used=%zu cleanup_nodes=%zu cleanup_bytes=%zu "
        "arena_used=%zu arena_capacity=%zu storage_bytes=%zu\n",
        route,
        cow_heap.collectable ? "collectable" : "ordinary",
        count,
        retain ? "all" : "last",
        phase,
        ownership_scope.counts.text_bytes,
        ownership_scope.counts.value_header_moves,
        cow_heap.resource_count,
        cow_heap.resource_count * payload,
        cow_heap.retained_bytes,
        used - cow_heap.retained_bytes,
        cow_heap.cleanup_count,
        arena_cleanup_bytes(&cow_heap),
        used,
        capacity,
        arena_storage_bytes(&cow_heap)
    );
}
static bool ownership_text_paths(bool collectable)
{
    static const char original[] = { 'a', 'b', 0,   'c', 'd',
                                     'e', 'f', 'g', 'h', 'i' };
    static const char *routes[] = { "slice-view", "value-copy", "native-core-slice" };
    for (int route = 0; route < 3; route++) {
        for (int count = 4; count <= 8; count *= 2) {
            for (int retain = 0; retain < 2; retain++) {
                if (collectable) {
                    arena_init_collectable(&cow_heap);
                }
                pin_value producer, window, roots[8] = { 0 };
                REQUIRE(!pin_value_text(&cow_producer, &producer, original, sizeof original));
                REQUIRE(!pin_value_text_slice(&cow_producer, &window, producer, 1, 4));
                REQUIRE(ownership_text_value(window));
                ownership_begin();
                for (int i = 0; i < count; i++) {
                    pin_value result;
                    if (route == 0) {
                        REQUIRE(!pin_value_text_slice(&cow_heap, &result, producer, 1, 4));
                    } else if (route == 1) {
                        REQUIRE(
                            !EXECUTION_value_copy_call.value(&cow_heap, &window, 1, &result)
                        );
                    } else {
                        pin_value arguments[] = { producer,
                                                  { .type = PIN_I32, .i = 1 },
                                                  { .type = PIN_I32, .i = 4 } };
                        REQUIRE(!NATIVE_TEXT_SLICE_CALL.value(&cow_heap, arguments, 3, &result));
                    }
                    REQUIRE(ownership_text_value(result));
                    if (retain || i == count - 1) {
                        roots[retain ? i : 0] = result;
                    } else {
                        /* This is the collection ownership API, not VM frame
                         * dispatch. Scalar text release stays arena-owned. */
                        pin_value_collection_release(&result);
                    }
                }
                pin_copy_scope_end(&ownership_scope);
                size_t payload = route == 2 ? 5 : sizeof original + 1;
                ownership_text_observe(routes[route], "returned", count, retain, payload);
                /* The only surviving values are roots. This retires both the
                 * producer descriptor and its window, after every call returns. */
                arena_free(&cow_producer);
                int root_count = retain ? count : 1;
                for (int i = 0; i < root_count; i++) {
                    REQUIRE(ownership_text_value(roots[i]));
                }
                ownership_text_observe(routes[route], "producer-freed", count, retain, payload);
                if (collectable) {
                    cow_collect(roots, root_count);
                    ownership_text_observe(routes[route], "rooted-collection", count, retain, payload);
                    for (int i = 0; i < root_count; i++) {
                        REQUIRE(ownership_text_value(roots[i]));
                    }
                    cow_collect(NULL, 0);
                } else {
                    arena_reset(&cow_heap);
                }
                ownership_text_observe(routes[route], "retired", count, retain, payload);
                size_t used, capacity;
                arena_usage(&cow_heap, &used, &capacity);
                REQUIRE(!used && !cow_heap.retained_bytes && !cow_heap.resource_count);
                REQUIRE(!cow_heap.cleanup_count && !arena_cleanup_bytes(&cow_heap));
                arena_free(&cow_heap);
            }
        }
    }
    return true;
}
TEST_CASE(ownership_text_ordinary_cleanup_growth)
{
    return ownership_text_paths(false);
}
TEST_CASE(ownership_text_collectable_rooted_lifetimes)
{
    return ownership_text_paths(true);
}
void test_tool_text_ownership_probes()
{
    printf(
        "TEXT descriptor_bytes=%zu; payload_unique is fixture backing length "
        "including NUL, "
        "external_unique includes backing metadata; cleanup_bytes excludes "
        "collectable headers\n",
        sizeof(pin_string)
    );
    pu_setup(NULL, release_values);
    RUN_TEST_CASE(ownership_text_ordinary_cleanup_growth);
    RUN_TEST_CASE(ownership_text_collectable_rooted_lifetimes);
    pu_clear_setup();
}

void test_tool_ownership_probes()
{
    printf(
        "OWNERSHIP units: "
        "adopt/detach=allocations/payload_capacity_bytes/packed_copy_bytes/"
        "boxed_child_values; pin_value_bytes=%zu pin_array_bytes=%zu\n",
        sizeof(pin_value),
        sizeof(pin_array)
    );
    pu_setup(NULL, release_values);
    RUN_TEST_CASE(ownership_raw_boxed_adoption_and_nested_detach);
    RUN_TEST_CASE(ownership_readonly_drop_and_retained_append);
    RUN_TEST_CASE(ownership_repeated_native_slice_and_concat);
    RUN_TEST_CASE(ownership_record_cache_alternation);
    pu_clear_setup();
}

void test_tool_types()
{
    pu_setup(NULL, release_values);
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
    RUN_TEST_CASE(types_nested_pointer_records_require_owned_layout_support);
    RUN_TEST_CASE(types_document_array_collection_retains_split_payloads);
    RUN_TEST_CASE(types_authored_record_document_uses_nested_native_layout);
    RUN_TEST_CASE(types_catalog_composes_ordered_packs);
    RUN_TEST_CASE(types_nested_reflection_reads_writes_and_checks_layout);
    RUN_TEST_CASE(types_array_snapshot_owns_strings_and_names);
    RUN_TEST_CASE(types_collection_follows_values_across_arenas);
    RUN_TEST_CASE(types_union_snapshots_and_heap_copies_own_nested_values);
    RUN_TEST_CASE(types_union_wire_roundtrip_traces_nested_arrays);
    RUN_TEST_CASE(types_plain_struct_array_codecs_preserve_inline_values_and_nulls);
    RUN_TEST_CASE(types_union_wire_rejects_invalid_tags_counts_and_depth);
    RUN_TEST_CASE(types_struct_array_snapshot_retains_nested_payload);
    RUN_TEST_CASE(types_document_snapshot_survives_source_release);
    RUN_TEST_CASE(types_map_documents_own_nested_maps_arrays_and_native_structs);
    RUN_TEST_CASE(types_map_documents_require_authored_declarations_and_reject_malformed_input);
    RUN_TEST_CASE(types_document_rejects_invalid_values_without_publication);
    pu_clear_setup();
}
