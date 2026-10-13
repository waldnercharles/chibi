#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/trace.h"
#include "types/union.h"
#include "types/value_document.h"
#include "types/value_wire.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
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
static pin_array *source;
static av_doc *source_document;

static void release_values()
{
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
    REQUIRE(pin_read_field_path(&frame, &frame_type, "position.y", &value) == 0);
    REQUIRE(value.type == PIN_F32 && value.f == -4.0f);
    pin_read_field(&frame, &frame_fields[1], &value);
    REQUIRE(value.type == PIN_STRUCT && value.ptr == &frame.position);
    REQUIRE(!strcmp(value.type_name, "test.position"));
    pin_write_field(value.ptr, &position_fields[0], (pin_value){ .type = PIN_F32, .f = 8.0f });
    REQUIRE(frame.position.x == 8.0f && frame.position.y == -4.0f && frame.enabled);
    REQUIRE(pin_read_field_path(&frame, &frame_type, "enabled", &value) == 0);
    REQUIRE(value.type == PIN_BOOL && value.b);
    REQUIRE(pin_read_field_path(&frame, &frame_type, "position.absent", &value) == -1);
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
    pin_value item = { .type = PIN_STR, .str = text, .type_name = nominal_name };
    REQUIRE(pin_array_push(source, &item) == 0);
    text[0] = 'X';
    REQUIRE(!strcmp(source->items[0].str, "original"));
    pin_array *snapshot = pin_array_snapshot_to_arena(&snapshots, source);
    REQUIRE(snapshot != NULL && snapshot->count == 1);
    REQUIRE(snapshot->items != source->items);
    REQUIRE(snapshot->items[0].str != source->items[0].str);
    nominal_name[0] = 'X';
    pin_array_free(source);
    source = NULL;
    REQUIRE(!strcmp(snapshot->type_name, "test.text"));
    REQUIRE(!strcmp(snapshot->items[0].type_name, "test.text"));
    REQUIRE(!strcmp(snapshot->items[0].str, "original"));
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
    array->items[0] = (pin_value){ .type = PIN_STR, .str = text };
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

TEST_CASE(types_union_snapshots_and_heap_copies_own_nested_values)
{
    arena producer = { 0 };
    char text[] = "retained", name[] = "payload.name";
    pin_value field = { .type = PIN_STR, .str = text, .type_name = name };
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
    REQUIRE(!strcmp(inner_copy->values[0].str, "retained"));
    REQUIRE(!strcmp(inner_copy->values[0].type_name, "payload.name"));
    pin_value_destroy(&copied);
    outer_copy = snapshot.ptr;
    inner_copy = outer_copy->values[0].ptr;
    REQUIRE(!strcmp(inner_copy->values[0].str, "retained"));
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
    array->items[0] = (pin_value){ .type = PIN_STR, .str = "wire-retained" };
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
    REQUIRE(!strcmp(items->items[0].str, "wire-retained"));
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
    REQUIRE(source->items[0].ptr == &record);
    pin_array *snapshot = pin_array_snapshot_to_arena(&snapshots, source);
    REQUIRE(snapshot != NULL && snapshot->count == 1);
    REQUIRE(snapshot->items[0].ptr != &record);
    test_record *copy = snapshot->items[0].ptr;
    REQUIRE(copy->label != record.label);
    record.frame.position.x = 99;
    record.frame.enabled = false;
    label[0] = 'X';
    nominal_name[0] = 'X';
    pin_array_free(source);
    source = NULL;
    pin_value value;
    REQUIRE(pin_read_field_path(copy, &record_type, "frame.position.x", &value) == 0);
    REQUIRE(value.type == PIN_F32 && value.f == 2);
    REQUIRE(copy->frame.enabled && !strcmp(copy->label, "retained"));
    REQUIRE(!strcmp(snapshot->items[0].type_name, "test.record"));
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
    const test_record *copy = array->items[0].ptr;
    REQUIRE(
        copy->frame.enabled && copy->frame.position.x == 2.5f &&
        copy->frame.position.y == -4
    );
    REQUIRE(!strcmp(copy->label, "retained document"));
    REQUIRE(mode_values.container == PIN_CONTAINER_SET && choices->count == 2);
    REQUIRE(choices->items[0].e == 7 && choices->items[1].e == 3);
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
    REQUIRE(
        pin_value_read_document(
            &snapshots,
            (pin_value){ .type = PIN_STR },
            av_strn(source_document, embedded, sizeof embedded),
            &value,
            error,
            sizeof error
        ) == -1
    );
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

void test_tool_types()
{
    pu_setup(NULL, release_values);
    RUN_TEST_CASE(types_catalog_composes_ordered_packs);
    RUN_TEST_CASE(types_nested_reflection_reads_writes_and_checks_layout);
    RUN_TEST_CASE(types_array_snapshot_owns_strings_and_names);
    RUN_TEST_CASE(types_collection_follows_values_across_arenas);
    RUN_TEST_CASE(types_union_snapshots_and_heap_copies_own_nested_values);
    RUN_TEST_CASE(types_union_wire_roundtrip_traces_nested_arrays);
    RUN_TEST_CASE(types_union_wire_rejects_invalid_tags_counts_and_depth);
    RUN_TEST_CASE(types_struct_array_snapshot_retains_nested_payload);
    RUN_TEST_CASE(types_document_snapshot_survives_source_release);
    RUN_TEST_CASE(types_document_rejects_invalid_values_without_publication);
    pu_clear_setup();
}
