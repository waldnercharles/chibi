#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/value_document.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#ifdef CHIBI_TEST_SOURCE_RUNTIME
#include "runtime/component_catalog.h"
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

#ifdef CHIBI_TEST_SOURCE_FEATURES
#include "authoring/studio_schema.h"
bool test_shader_parameters_active();
type_catalog_pack test_shader_parameters_types();
#endif
int type_catalog_pack_count()
{
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
        return index ? TEST_REFLECTED_TYPES_PACK : GAME_TYPE_PACK;
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
    RUN_TEST_CASE(types_struct_array_snapshot_retains_nested_payload);
    RUN_TEST_CASE(types_document_snapshot_survives_source_release);
    RUN_TEST_CASE(types_document_rejects_invalid_values_without_publication);
    pu_clear_setup();
}
