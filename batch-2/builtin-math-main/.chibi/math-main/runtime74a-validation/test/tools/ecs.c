#include "ecs/ecs.h"

#include "ecs/execution.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    float amount;
    const char *label;
} sdk_record;

static const type_field record_fields[] = {
    { .name = "amount",
      .type = PIN_F32,
      .offset = offsetof(sdk_record, amount),
      .size = sizeof(float),
      .type_name = "number" },
    { .name = "label",
      .type = PIN_STR,
      .offset = offsetof(sdk_record, label),
      .size = sizeof(const char *),
      .type_name = "const char *" },
};
static const type_desc record_type = {
    .name = "sdk.ecs.record",
    .size = sizeof(sdk_record),
    .fields = record_fields,
    .field_count = 2,
};
static const ecs_component_desc record_desc = {
    .id = 101,
    .key = "sdk.ecs.record",
    .name = "Record",
    .element_size = sizeof(sdk_record),
    .type = &record_type,
};
static const type_field marker_fields[] = {
    { .name = "value", .type = PIN_I32, .offset = 0, .size = sizeof(int), .type_name = "int" },
};
static const type_desc marker_type = {
    .name = "sdk.ecs.marker",
    .size = sizeof(int),
    .fields = marker_fields,
    .field_count = 1,
};
static const ecs_component_desc tag_desc = {
    .id = 202,
    .key = "sdk.ecs.tag",
    .name = "Tag",
    .element_size = sizeof(int),
    .type = &marker_type,
    .index = ECS_INDEX_DENSE,
};
static const ecs_component_desc excluded_desc = {
    .id = 303,
    .key = "sdk.ecs.excluded",
    .name = "Excluded",
    .element_size = sizeof(int),
    .type = &marker_type,
};

typedef struct
{
    struct
    {
        bool enabled;
        struct
        {
            float x, y;
        } position;
    } frame;
    const char *label;
} managed_payload;
typedef struct
{
    float amount;
    managed_payload payload;
    const char *borrowed;
} managed_record;
static const type_field managed_fields[] = {
    { .name = "amount",
      .type = PIN_F32,
      .offset = offsetof(managed_record, amount),
      .size = sizeof(float),
      .type_name = "number" },
    { .name = "payload",
      .type = PIN_STRUCT,
      .offset = offsetof(managed_record, payload),
      .size = sizeof(managed_payload),
      .type_name = "test.record" },
    { .name = "borrowed",
      .type = PIN_STR,
      .offset = offsetof(managed_record, borrowed),
      .size = sizeof(const char *),
      .type_name = "const char *" },
};
static const type_desc managed_type = {
    .name = "sdk.ecs.managed",
    .size = sizeof(managed_record),
    .fields = managed_fields,
    .field_count = 3,
};
static const ecs_component_desc managed_desc = {
    .id = 404,
    .key = "sdk.ecs.managed",
    .name = "Managed",
    .element_size = sizeof(managed_record),
    .type = &managed_type,
};

#if defined(__clang__) && __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define REQUIRE_RETIRED(pointer) REQUIRE(__asan_address_is_poisoned(pointer))
#else
#define REQUIRE_RETIRED(pointer) ((void)(pointer))
#endif

static ecs_world *world;
static ecs_query query;
static bool querying;

static void prepare_world()
{
    world = ecs_world_new();
    assert(ecs_world_register_component(world, &record_desc, NULL) == ECS_OK);
}

static void release_world()
{
    if (querying) {
        ecs_query_end(&query);
        querying = false;
    }
    ecs_world_free(world);
    world = NULL;
}

TEST_CASE(ecs_handles_reject_destroyed_and_canceled_generations)
{
    ecs_entity original = ecs_world_create_entity(world);
    uint64_t encoded = ecs_entity_to_u64(original);
    ecs_entity decoded = ecs_entity_from_u64(encoded);
    REQUIRE(original.slot && original.generation);
    REQUIRE(decoded.slot == original.slot && decoded.generation == original.generation);
    REQUIRE(ecs_world_destroy_entity(world, original) == ECS_OK);
    REQUIRE(!ecs_entity_is_alive(world, original));
    REQUIRE(ecs_world_record_destroy(world, original, 0) == ECS_ERR_ENTITY);
    ecs_entity canceled = ecs_world_record_create(world, 0);
    REQUIRE(canceled.slot == original.slot && canceled.generation != original.generation);
    REQUIRE(!ecs_entity_is_alive(world, canceled));
    ecs_world_clear_commands(world);
    ecs_entity reused = ecs_world_create_entity(world);
    REQUIRE(reused.slot == original.slot && reused.generation != canceled.generation);
    REQUIRE(ecs_entity_is_alive(world, reused));
    REQUIRE(!ecs_entity_is_alive(world, original) && !ecs_entity_is_alive(world, canceled));
    REQUIRE(!ecs_component_has(world, reused, record_desc.id));
    REQUIRE(ecs_world_live_count(world) == 1);
    return true;
}

TEST_CASE(ecs_commands_wait_for_the_iteration_sync_point)
{
    ecs_entity entity = ecs_world_record_create(world, 0);
    sdk_record initial = { .amount = 7 };
    REQUIRE(!ecs_entity_is_alive(world, entity));
    REQUIRE(ecs_world_record_add(world, entity, record_desc.id, &initial, 1) == ECS_ERR_ENTITY);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, entity, record_desc.id, &initial, 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_has(world, entity, record_desc.id));
    REQUIRE(ecs_world_record_remove(world, entity, record_desc.id, 0) == ECS_OK);
    REQUIRE(ecs_query_begin(world, &query, &record_desc.id, 1) == ECS_OK);
    querying = true;
    REQUIRE(ecs_world_apply_commands(world) == ECS_ERR_ITERATING);
    REQUIRE(ecs_world_command_count(world) == 1);
    ecs_entity found;
    REQUIRE(ecs_query_next(&query, &found) && found.slot == entity.slot);
    REQUIRE(
        ((const sdk_record *)ecs_component_data_const(world, found, record_desc.id))
            ->amount == 7
    );
    REQUIRE(!ecs_query_next(&query, &found));
    ecs_query_end(&query);
    querying = false;
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!ecs_component_has(world, entity, record_desc.id));
    REQUIRE(ecs_world_command_count(world) == 0);
    return true;
}

TEST_CASE(ecs_or_chains_and_negation_match_in_slot_order)
{
    REQUIRE(ecs_world_register_component(world, &tag_desc, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &excluded_desc, NULL) == ECS_OK);
    ecs_entity entities[4];
    for (int i = 0; i < 4; i++) {
        entities[i] = ecs_world_create_entity(world);
    }
    REQUIRE(ecs_world_record_add(world, entities[0], record_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, entities[1], tag_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, entities[2], record_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, entities[2], excluded_desc.id, NULL, 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    ecs_term terms[] = {
        { .oper = ECS_OR, .id = record_desc.id },
        { .oper = ECS_OR, .id = tag_desc.id },
        { .oper = ECS_NOT, .id = excluded_desc.id },
    };
    REQUIRE(ecs_query_begin_terms(world, &query, terms, 3) == ECS_OK);
    terms[2].oper = ECS_AND; /* Begin copied the terms. */
    querying = true;
    ecs_entity found;
    REQUIRE(ecs_query_next(&query, &found) && found.slot == entities[0].slot);
    REQUIRE(ecs_query_next(&query, &found) && found.slot == entities[1].slot);
    REQUIRE(!ecs_query_next(&query, &found));
    return true;
}

TEST_CASE(ecs_execution_view_refreshes_after_growth_and_registration)
{
    ecs_entity first = ecs_world_create_entity(world);
    sdk_record initial = { .amount = 13 };
    REQUIRE(ecs_world_record_add(world, first, record_desc.id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    uint32_t previous_capacity = ecs_world_entity_capacity(world);
    REQUIRE(previous_capacity < 1024);
    ecs_entity last = first;
    for (uint32_t i = 0; i <= previous_capacity; i++) {
        last = ecs_world_record_create(world, i);
        REQUIRE(last.slot != 0);
    }
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    initial.amount = 29;
    REQUIRE(ecs_world_record_add(world, last, record_desc.id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_world_entity_capacity(world) > previous_capacity);
    const ecs_execution_view *view = ecs_world_execution(world);
    uint32_t slot = ecs_execution_entity(view, ecs_entity_to_u64(last));
    REQUIRE(slot == last.slot);
    const ecs_execution_column
        *column = ecs_execution_component(view, slot, record_desc.id);
    REQUIRE(column && column->count == 2 && column->stride == sizeof(sdk_record));
    REQUIRE(column->layout == type_layout_hash(&record_type));
    const sdk_record *row = (const sdk_record *)((const char *)column->data +
                                                 slot * column->stride);
    REQUIRE(row->amount == 29);
    REQUIRE(
        ((const sdk_record *)ecs_component_data_const(world, first, record_desc.id))
            ->amount == 13
    );
    REQUIRE(ecs_world_register_component(world, &tag_desc, NULL) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, last, tag_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    column = ecs_execution_component(view, slot, tag_desc.id);
    REQUIRE(column && column->count == 1 && column->singleton == slot);
    REQUIRE(!ecs_execution_component(view, first.slot, tag_desc.id));
    return true;
}

TEST_CASE(ecs_reflected_field_writes_retain_strings)
{
    arena storage = { 0 };
    ecs_entity entity = ecs_world_create_entity(world);
    REQUIRE(ecs_world_record_add(world, entity, record_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    char text[] = "retained";
    REQUIRE(
        ecs_component_write_field(
            world,
            entity,
            record_desc.id,
            1,
            (pin_value){ .type = PIN_STR,
                         .text = &(const pin_string){ .bytes = text,
                                                      .length = (uint32_t)strlen(text) } }
        ) == ECS_OK
    );
    text[0] = 'X';
    pin_value value;
    REQUIRE(ecs_component_read_field(&storage, world, entity, record_desc.id, 1, &value) == ECS_OK);
    REQUIRE(
        value.type == PIN_STR && pin_text(value).bytes != text &&
        !strcmp(pin_text(value).bytes, "retained")
    );
    const char *retained = pin_text(value).bytes;
    REQUIRE(
        ecs_component_write_field(world, entity, record_desc.id, 0, (pin_value){ .type = PIN_F32, .f = 31.5f }) ==
        ECS_OK
    );
    REQUIRE(ecs_component_read_field(&storage, world, entity, record_desc.id, 0, &value) == ECS_OK);
    REQUIRE(value.type == PIN_F32 && value.f == 31.5f);
    REQUIRE(
        ecs_component_write_field(
            world,
            entity,
            record_desc.id,
            1,
            (pin_value){
                .type = PIN_STR,
                .text = &(const pin_string){ .bytes = "replacement",
                                             .length = sizeof("replacement") - 1 } }
        ) == ECS_OK
    );
    REQUIRE(!strcmp(retained, "retained"));
    REQUIRE(ecs_component_read_field(&storage, world, entity, record_desc.id, 1, &value) == ECS_OK);
    REQUIRE(!strcmp(pin_text(value).bytes, "replacement"));
    arena_free(&storage);
    return true;
}

TEST_CASE(ecs_native_text_rejects_nul_without_publishing)
{
    REQUIRE(ecs_world_register_component(world, &managed_desc, NULL) == ECS_OK);
    REQUIRE(ecs_component_track_changes(world, record_desc.id) == ECS_OK);
    REQUIRE(ecs_component_track_changes(world, managed_desc.id) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    sdk_record initial = { .amount = 17, .label = "before" };
    managed_record managed = { .amount = 23,
                               .payload = { .label = "nested-before" },
                               .borrowed = "other" };
    REQUIRE(ecs_world_record_add(world, entity, record_desc.id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, entity, managed_desc.id, &managed, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    sdk_record before = *(
        const sdk_record *
    )ecs_component_data_const(world, entity, record_desc.id);
    managed_record nested_before = *(
        const managed_record *
    )ecs_component_data_const(world, entity, managed_desc.id);
    uint64_t version = ecs_component_version(world, entity, record_desc.id);
    uint64_t nested_version = ecs_component_version(world, entity, managed_desc.id);
    ecs_string_usage strings = ecs_world_string_usage(world);
    const pin_string bad_text = { .bytes = "save\0backup", .length = 11 };
    pin_value bad = { .type = PIN_STR, .text = &bad_text };
    REQUIRE(ecs_component_write_field(world, entity, record_desc.id, 1, bad) == ECS_ERR_ARGUMENT);
    REQUIRE(
        ecs_component_write_value(
            world,
            entity,
            managed_desc.id,
            offsetof(managed_record, payload) + offsetof(managed_payload, label),
            bad,
            bad
        ) == ECS_ERR_ARGUMENT
    );

    /* A carried authored layout has descriptors where native fields hold C
     * pointers. Conversion must reject its nested string before publishing any
     * of the otherwise valid frame data. */
    arena source = { 0 };
    const type_desc *native = type_catalog_find("test.record");
    REQUIRE(native);
    const pin_record *record = pin_record_create(
        &source,
        "test.record",
        1,
        native->fields,
        native->field_count
    );
    REQUIRE(record && record->type.fields[1].text_descriptor);
    void *payload = arena_alloc(&source, record->type.size);
    memset(payload, 0, record->type.size);
    memcpy(
        (char *)payload + record->type.fields[0].offset,
        &managed.payload.frame,
        sizeof managed.payload.frame
    );
    REQUIRE(!pin_write_field(&source, payload, &record->type.fields[1], bad));
    pin_value carried = { .type = PIN_STRUCT,
                          .type_name = "test.record",
                          .record = record,
                          .ptr = payload };
    pin_value declared = { .type = PIN_STRUCT, .type_name = "test.record" };
    REQUIRE(
        ecs_component_write_value(world, entity, managed_desc.id, offsetof(managed_record, payload), declared, carried) ==
        ECS_ERR_ARGUMENT
    );
    REQUIRE(
        !memcmp(ecs_component_data_const(world, entity, record_desc.id), &before, sizeof before)
    );
    REQUIRE(
        !memcmp(ecs_component_data_const(world, entity, managed_desc.id), &nested_before, sizeof nested_before)
    );
    REQUIRE(ecs_component_version(world, entity, record_desc.id) == version);
    REQUIRE(ecs_component_version(world, entity, managed_desc.id) == nested_version);
    ecs_string_usage after = ecs_world_string_usage(world);
    REQUIRE(
        after.entries == strings.entries && after.payload_bytes == strings.payload_bytes &&
        after.table_backing_bytes == strings.table_backing_bytes &&
        after.overflow == strings.overflow
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_version(world, entity, record_desc.id) == version);
    REQUIRE(ecs_component_version(world, entity, managed_desc.id) == nested_version);
    arena_free(&source);
    return true;
}

TEST_CASE(ecs_descriptor_fields_retain_every_byte)
{
    static const type_field field = { .name = "text",
                                      .type = PIN_STR,
                                      .size = sizeof(const pin_string *),
                                      .type_name = "const char *",
                                      .text_descriptor = true };
    static const type_desc type = { .name = "sdk.ecs.text",
                                    .size = sizeof(const pin_string *),
                                    .fields = &field,
                                    .field_count = 1 };
    static const ecs_component_desc desc = { .id = 505,
                                             .key = "sdk.ecs.text",
                                             .name = "Text",
                                             .element_size = sizeof(const pin_string *),
                                             .type = &type };
    REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    REQUIRE(ecs_world_record_add(world, entity, desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    char bytes[] = { 'A', 0, 'B', 0 };
    pin_string text = { .bytes = bytes, .length = 3 };
    REQUIRE(
        ecs_component_write_field(world, entity, desc.id, 0, (pin_value){ .type = PIN_STR, .text = &text }) ==
        ECS_OK
    );
    bytes[0] = 'X';
    arena storage = { 0 };
    pin_value value;
    REQUIRE(ecs_component_read_field(&storage, world, entity, desc.id, 0, &value) == ECS_OK);
    REQUIRE(pin_text(value).length == 3 && !memcmp(pin_text(value).bytes, "A\0B", 3));
    REQUIRE(value.text != &text && pin_text(value).bytes != bytes);
    ecs_string_usage usage = ecs_world_string_usage(world);
    REQUIRE(!usage.entries);
    arena_free(&storage);
    return true;
}

TEST_CASE(ecs_strings_deduplicate_collisions_and_survive_growth)
{
    const ecs_execution_view *view = ecs_world_execution(world);
    const char *empty = view->intern_string(view, NULL);
    REQUIRE(empty && !*empty && empty == view->intern_string(view, ""));
    /* These share the initial hash bucket; equality must compare full text. */
    char text[] = "a";
    const char *first = view->intern_string(view, text);
    const char *collision = view->intern_string(view, "q");
    text[0] = 'x';
    REQUIRE(first != collision && !strcmp(first, "a") && !strcmp(collision, "q"));
    const char *retained[2048];
    for (int i = 0; i < 2048; i++) {
        char value[32];
        snprintf(value, sizeof value, "world-string-%d", i);
        retained[i] = view->intern_string(view, value);
    }
    for (int i = 2047; i >= 0; i--) {
        char value[32];
        snprintf(value, sizeof value, "world-string-%d", i);
        REQUIRE(!strcmp(retained[i], value));
        REQUIRE(retained[i] == view->intern_string(view, value));
    }
    REQUIRE(first == view->intern_string(view, "a"));
    REQUIRE(collision == view->intern_string(view, "q"));
    REQUIRE(empty == view->intern_string(view, NULL));
    ecs_world *other = ecs_world_new();
    const ecs_execution_view *other_view = ecs_world_execution(other);
    const char *other_string = other_view->intern_string(other_view, "a");
    REQUIRE(other_string != first && !strcmp(other_string, first));
    ecs_world_free(other);
    REQUIRE(!strcmp(first, "a"));
    return true;
}

TEST_CASE(ecs_managed_rows_outlive_sources_and_reclaim_replacements)
{
    REQUIRE(sizeof(managed_payload) == type_catalog_find("test.record")->size);
    REQUIRE(ecs_world_register_component(world, &managed_desc, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    char borrowed[] = "borrowed";
    managed_record initial = { .borrowed = borrowed };
    REQUIRE(ecs_world_record_add(world, entity, managed_desc.id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    arena source = { 0 };
    char *text = arena_alloc(&source, 16);
    strcpy(text, "first");
    managed_payload payload = { .frame = { .enabled = true, .position = { 3, 7 } },
                                .label = text };
    pin_value declared = { .type = PIN_STRUCT, .type_name = "test.record" };
    REQUIRE(
        ecs_component_write_value(
            world,
            entity,
            managed_desc.id,
            offsetof(managed_record, payload),
            declared,
            (pin_value){ .type = PIN_STRUCT, .type_name = "test.record", .ptr = &payload }
        ) == ECS_OK
    );
    arena_free(&source);
    managed_record *row = ecs_component_data(world, entity, managed_desc.id);
    REQUIRE(row->payload.frame.enabled && row->payload.frame.position.y == 7);
    REQUIRE(!strcmp(row->payload.label, "first") && !strcmp(row->borrowed, "borrowed"));
    REQUIRE(row->borrowed != borrowed && !strcmp(borrowed, "borrowed"));
    uint64_t allocations = arena_block_allocations();
    REQUIRE(
        ecs_component_write_value(
            world,
            entity,
            managed_desc.id,
            offsetof(managed_record, payload),
            declared,
            (pin_value){ .type = PIN_STRUCT, .type_name = "test.record", .ptr = &row->payload }
        ) == ECS_OK
    );
    for (int i = 0; i < 256; i++) {
        pin_value number = { .type = PIN_F32, .f = (float)i };
        REQUIRE(
            ecs_component_write_value(world, entity, managed_desc.id, 0, number, number) == ECS_OK
        );
    }
    REQUIRE(arena_block_allocations() == allocations && row->amount == 255);
    const char *retired = row->payload.label;
    payload.label = "second";
    REQUIRE(
        ecs_component_write_value(
            world,
            entity,
            managed_desc.id,
            offsetof(managed_record, payload),
            declared,
            (pin_value){ .type = PIN_STRUCT, .type_name = "test.record", .ptr = &payload }
        ) == ECS_OK
    );
    REQUIRE_RETIRED(retired);
    REQUIRE(!strcmp(row->payload.label, "second") && row->amount == 255);
    char next[] = "nested";
    pin_value label = {
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = next, .length = (uint32_t)strlen(next) }
    };
    REQUIRE(
        ecs_component_write_value(
            world,
            entity,
            managed_desc.id,
            offsetof(managed_record, payload) + offsetof(managed_payload, label),
            label,
            label
        ) == ECS_OK
    );
    next[0] = 'X';
    REQUIRE(!strcmp(row->payload.label, "nested") && !strcmp(row->borrowed, "borrowed"));
    pin_value unsupported = label;
    unsupported.container = PIN_CONTAINER_ARRAY;
    REQUIRE(
        ecs_component_write_value(
            world,
            entity,
            managed_desc.id,
            offsetof(managed_record, payload) + offsetof(managed_payload, label),
            unsupported,
            unsupported
        ) == ECS_ERR_ARGUMENT
    );
    REQUIRE(!strcmp(row->payload.label, "nested"));
    REQUIRE(
        ecs_component_write_value(world, entity, managed_desc.id, sizeof(managed_record), label, label) ==
        ECS_ERR_ARGUMENT
    );
    return true;
}

TEST_CASE(ecs_managed_storage_retires_on_remove_destroy_and_world_free)
{
    REQUIRE(ecs_world_register_component(world, &managed_desc, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    char borrowed[] = "external";
    managed_record input = { .payload = { .label = "owned" }, .borrowed = borrowed };
    pin_value whole = { .type = PIN_STRUCT, .type_name = managed_type.name, .ptr = &input };
    const char *retired = NULL;
    for (int i = 0; i < 3; i++) {
        REQUIRE(ecs_world_record_add(world, entity, managed_desc.id, NULL, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(
            ecs_component_write_value(world, entity, managed_desc.id, 0, whole, whole) == ECS_OK
        );
        managed_record *row = ecs_component_data(world, entity, managed_desc.id);
        REQUIRE(!strcmp(row->payload.label, "owned") && row->borrowed != borrowed);
        retired = row->payload.label;
        if (i == 0) {
            REQUIRE(ecs_world_record_remove(world, entity, managed_desc.id, 0) == ECS_OK);
            REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
            REQUIRE(!ecs_component_has(world, entity, managed_desc.id));
        } else if (i == 1) {
            REQUIRE(ecs_world_destroy_entity(world, entity) == ECS_OK);
            ecs_entity next = ecs_world_create_entity(world);
            REQUIRE(next.slot == entity.slot && next.generation != entity.generation);
            entity = next;
        } else {
            ecs_world_free(world);
            world = NULL;
        }
        REQUIRE_RETIRED(retired);
        REQUIRE(!strcmp(borrowed, "external"));
    }
    return true;
}

typedef struct
{
    ecs_event event;
    uint32_t slot;
    ecs_component_id component;
    float amount; /* Record value seen by the observer, or -1 when absent. */
    bool tagged;
} observed_event;

static observed_event observed[32];
static int observed_count;

static void record_event(ecs_world *w, ecs_event event, ecs_entity entity, ecs_component_id component, void *)
{
    assert(observed_count < (int)(sizeof observed / sizeof observed[0]));
    const sdk_record *row = ecs_component_data_const(w, entity, record_desc.id);
    observed[observed_count++] = (observed_event){
        .event = event,
        .slot = entity.slot,
        .component = component,
        .amount = row ? row->amount : -1,
        .tagged = ecs_component_has(w, entity, tag_desc.id),
    };
}

static bool observed_is(int index, ecs_event event, ecs_entity entity, ecs_component_id component, float amount)
{
    const observed_event *e = &observed[index];
    return e->event == event && e->slot == entity.slot &&
           e->component == component && e->amount == amount;
}

static void observe_all(ecs_component_id component)
{
    assert(ecs_world_observe(world, ECS_ON_ADD, component, record_event, NULL) > 0);
    assert(ecs_world_observe(world, ECS_ON_SET, component, record_event, NULL) > 0);
    assert(ecs_world_observe(world, ECS_ON_REMOVE, component, record_event, NULL) > 0);
}

TEST_CASE(ecs_observers_see_add_set_and_remove_at_the_sync_point)
{
    observed_count = 0;
    REQUIRE(ecs_world_register_component(world, &tag_desc, NULL) == ECS_OK);
    observe_all(record_desc.id);
    ecs_entity entity = ecs_world_create_entity(world);
    sdk_record initial = { .amount = 3 };
    REQUIRE(ecs_world_record_add(world, entity, record_desc.id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, entity, tag_desc.id, NULL, 1) == ECS_OK);
    REQUIRE(observed_count == 0);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(observed_count == 2);
    REQUIRE(
        observed_is(0, ECS_ON_ADD, entity, record_desc.id, 3) && !observed[0].tagged
    );
    REQUIRE(observed_is(1, ECS_ON_SET, entity, record_desc.id, 3));
    pin_value amount = { .type = PIN_F32, .f = 5 };
    REQUIRE(ecs_component_write_field(world, entity, record_desc.id, 0, amount) == ECS_OK);
    amount.f = 6;
    REQUIRE(ecs_component_write_value(world, entity, record_desc.id, 0, amount, amount) == ECS_OK);
    REQUIRE(ecs_world_record_remove(world, entity, record_desc.id, 0) == ECS_OK);
    REQUIRE(observed_count == 2);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(observed_count == 4);
    REQUIRE(observed_is(2, ECS_ON_SET, entity, record_desc.id, 6));
    REQUIRE(observed_is(3, ECS_ON_REMOVE, entity, record_desc.id, 6));
    REQUIRE(!ecs_component_has(world, entity, record_desc.id));
    REQUIRE(ecs_world_record_add(world, entity, record_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(
        observed_count == 5 && observed_is(4, ECS_ON_ADD, entity, record_desc.id, 0)
    );
    return true;
}

TEST_CASE(ecs_destroy_notifies_the_entity_and_each_component_before_removal)
{
    observed_count = 0;
    REQUIRE(ecs_world_register_component(world, &tag_desc, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    sdk_record initial = { .amount = 9 };
    REQUIRE(ecs_world_record_add(world, entity, record_desc.id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, entity, tag_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_world_observe(world, ECS_ON_REMOVE, tag_desc.id, record_event, NULL) > 0);
    REQUIRE(ecs_world_observe(world, ECS_ON_DESTROY, 0, record_event, NULL) > 0);
    REQUIRE(ecs_world_observe(world, ECS_ON_REMOVE, record_desc.id, record_event, NULL) > 0);
    REQUIRE(ecs_world_destroy_entity(world, entity) == ECS_OK);
    REQUIRE(observed_count == 3);
    REQUIRE(observed_is(0, ECS_ON_DESTROY, entity, 0, 9) && observed[0].tagged);
    REQUIRE(
        observed_is(1, ECS_ON_REMOVE, entity, record_desc.id, 9) && observed[1].tagged
    );
    REQUIRE(observed_is(2, ECS_ON_REMOVE, entity, tag_desc.id, 9) && observed[2].tagged);
    REQUIRE(!ecs_entity_is_alive(world, entity));
    return true;
}

TEST_CASE(ecs_rows_destroyed_later_in_the_batch_still_report_their_set)
{
    observed_count = 0;
    observe_all(record_desc.id);
    ecs_entity entity = ecs_world_create_entity(world);
    sdk_record initial = { .amount = 4 };
    REQUIRE(ecs_world_record_add(world, entity, record_desc.id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_record_destroy(world, entity, 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(observed_count == 3);
    REQUIRE(observed_is(0, ECS_ON_ADD, entity, record_desc.id, 4));
    REQUIRE(observed_is(1, ECS_ON_SET, entity, record_desc.id, 4));
    REQUIRE(observed_is(2, ECS_ON_REMOVE, entity, record_desc.id, 4));
    return true;
}

static void seed_label(ecs_world *w, ecs_event, ecs_entity entity, ecs_component_id component, void *)
{
    pin_value label = { .type = PIN_STR,
                        .text = &(const pin_string){ .bytes = "seed",
                                                     .length = sizeof("seed") - 1 } };
    size_t offset = offsetof(sdk_record, label);
    assert(ecs_component_write_value(w, entity, component, offset, label, label) == ECS_OK);
}

/* An explicit add over a row With implied replaces its owned strings too. */
TEST_CASE(ecs_explicit_adds_replace_implied_rows_and_their_storage)
{
    REQUIRE(ecs_world_register_component(world, &tag_desc, NULL) == ECS_OK);
    REQUIRE(ecs_world_add_with(world, tag_desc.id, record_desc.id) == ECS_OK);
    REQUIRE(ecs_world_observe(world, ECS_ON_ADD, record_desc.id, seed_label, NULL) > 0);
    ecs_entity entity = ecs_world_create_entity(world);
    char mine[] = "mine";
    sdk_record explicit = { .amount = 2, .label = mine };
    REQUIRE(ecs_world_record_add(world, entity, tag_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, entity, record_desc.id, &explicit, 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    const sdk_record *row = ecs_component_data_const(world, entity, record_desc.id);
    REQUIRE(row->amount == 2 && row->label == mine);
    pin_value label = {
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = mine, .length = (uint32_t)strlen(mine) }
    };
    size_t offset = offsetof(sdk_record, label);
    REQUIRE(
        ecs_component_write_value(world, entity, record_desc.id, offset, label, label) == ECS_OK
    );
    REQUIRE(row->label != mine && !strcmp(row->label, "mine"));
    return true;
}

static ecs_entity follower;

static void tag_on_add(ecs_world *w, ecs_event, ecs_entity entity, ecs_component_id, void *)
{
    assert(ecs_world_record_add(w, entity, tag_desc.id, NULL, 0) == ECS_OK);
}

static void destroy_follower(ecs_world *w, ecs_event, ecs_entity entity, ecs_component_id, void *)
{
    if (entity.slot != follower.slot) {
        assert(ecs_world_record_destroy(w, follower, 0) == ECS_OK);
    }
}

TEST_CASE(ecs_observer_commands_apply_in_the_same_sync_point)
{
    REQUIRE(ecs_world_register_component(world, &tag_desc, NULL) == ECS_OK);
    REQUIRE(ecs_world_observe(world, ECS_ON_ADD, record_desc.id, tag_on_add, NULL) > 0);
    REQUIRE(ecs_world_observe(world, ECS_ON_DESTROY, 0, destroy_follower, NULL) > 0);
    ecs_entity leader = ecs_world_create_entity(world);
    follower = ecs_world_create_entity(world);
    REQUIRE(ecs_world_record_add(world, leader, record_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_has(world, leader, tag_desc.id));
    REQUIRE(ecs_world_command_count(world) == 0);
    REQUIRE(ecs_world_destroy_entity(world, leader) == ECS_OK);
    REQUIRE(!ecs_entity_is_alive(world, leader) && !ecs_entity_is_alive(world, follower));
    REQUIRE(ecs_world_live_count(world) == 0);
    return true;
}

static int tagged_seen;

static void count_tagged(ecs_world *w, ecs_event, ecs_entity, ecs_component_id, void *)
{
    ecs_query q;
    assert(ecs_query_begin(w, &q, &tag_desc.id, 1) == ECS_OK);
    ecs_entity found;
    while (ecs_query_next(&q, &found)) {
        tagged_seen++;
    }
    ecs_query_end(&q);
}

TEST_CASE(ecs_observer_queries_skip_rows_removed_earlier_in_the_batch)
{
    REQUIRE(ecs_world_register_component(world, &tag_desc, NULL) == ECS_OK);
    ecs_entity entities[3];
    for (int i = 0; i < 3; i++) {
        entities[i] = ecs_world_create_entity(world);
        REQUIRE(ecs_world_record_add(world, entities[i], tag_desc.id, NULL, 0) == ECS_OK);
    }
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_world_observe(world, ECS_ON_DESTROY, 0, count_tagged, NULL) > 0);
    tagged_seen = 0;
    REQUIRE(ecs_world_record_destroy(world, entities[0], 0) == ECS_OK);
    REQUIRE(ecs_world_record_destroy(world, entities[1], 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(tagged_seen == 3 + 2);
    return true;
}

static int observer_id;

static void observe_while_applying(ecs_world *w, ecs_event, ecs_entity, ecs_component_id, void *)
{
    assert(ecs_world_observe(w, ECS_ON_DESTROY, 0, record_event, NULL) == ECS_ERR_ITERATING);
    assert(ecs_world_unobserve(w, observer_id) == ECS_ERR_ITERATING);
    assert(ecs_world_apply_commands(w) == ECS_ERR_ITERATING);
}

TEST_CASE(ecs_set_marks_coalesce_in_slot_order_until_unobserved)
{
    observed_count = 0;
    ecs_entity entities[3];
    for (int i = 0; i < 3; i++) {
        entities[i] = ecs_world_create_entity(world);
        REQUIRE(ecs_world_record_add(world, entities[i], record_desc.id, NULL, 0) == ECS_OK);
    }
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(
        ecs_world_observe(world, ECS_ON_DESTROY, record_desc.id, record_event, NULL) == ECS_ERR_ARGUMENT
    );
    REQUIRE(ecs_world_observe(world, ECS_ON_SET, 0, record_event, NULL) == ECS_ERR_ARGUMENT);
    REQUIRE(ecs_world_observe(world, ECS_ON_SET, tag_desc.id, record_event, NULL) == ECS_ERR_COMPONENT);
    observer_id = ecs_world_observe(world, ECS_ON_SET, record_desc.id, record_event, NULL);
    REQUIRE(observer_id > 0);
    REQUIRE(
        ecs_world_observe(world, ECS_ON_SET, record_desc.id, observe_while_applying, NULL) > 0
    );
    pin_value amount = { .type = PIN_F32, .f = 1 };
    for (int i = 2; i >= 0; i--) {
        amount.f = (float)(i + 1);
        REQUIRE(ecs_component_write_field(world, entities[i], record_desc.id, 0, amount) == ECS_OK);
    }
    ((sdk_record *)ecs_component_data(world, entities[1], record_desc.id))->amount = 7;
    REQUIRE(ecs_component_modified(world, entities[1], record_desc.id) == ECS_OK);
    REQUIRE(ecs_component_modified(world, entities[1], tag_desc.id) == ECS_ERR_COMPONENT);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(observed_count == 3);
    REQUIRE(observed_is(0, ECS_ON_SET, entities[0], record_desc.id, 1));
    REQUIRE(observed_is(1, ECS_ON_SET, entities[1], record_desc.id, 7));
    REQUIRE(observed_is(2, ECS_ON_SET, entities[2], record_desc.id, 3));
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK && observed_count == 3);
    REQUIRE(ecs_world_unobserve(world, observer_id) == ECS_OK);
    REQUIRE(ecs_world_unobserve(world, observer_id) == ECS_ERR_ARGUMENT);
    REQUIRE(ecs_component_write_field(world, entities[0], record_desc.id, 0, amount) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK && observed_count == 3);
    return true;
}

static void clamp_amount(ecs_world *w, ecs_event, ecs_entity entity, ecs_component_id, void *)
{
    pin_value amount = { .type = PIN_F32, .f = 10 };
    assert(ecs_component_write_field(w, entity, record_desc.id, 0, amount) == ECS_OK);
}

static void reject_dying_commands(ecs_world *w, ecs_event, ecs_entity entity, ecs_component_id, void *)
{
    assert(ecs_world_record_remove(w, entity, tag_desc.id, 0) == ECS_ERR_ENTITY);
    assert(ecs_world_record_add(w, entity, excluded_desc.id, NULL, 0) == ECS_ERR_ENTITY);
    assert(!ecs_world_create_entity(w).slot);
    assert(ecs_world_destroy_entity(w, follower) == ECS_ERR_ITERATING);
}

TEST_CASE(ecs_observers_rewrite_rows_without_firing_again)
{
    observed_count = 0;
    REQUIRE(ecs_world_register_component(world, &tag_desc, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &excluded_desc, NULL) == ECS_OK);
    REQUIRE(ecs_world_observe(world, ECS_ON_ADD, record_desc.id, clamp_amount, NULL) > 0);
    REQUIRE(ecs_world_observe(world, ECS_ON_SET, record_desc.id, clamp_amount, NULL) > 0);
    REQUIRE(ecs_world_observe(world, ECS_ON_SET, record_desc.id, record_event, NULL) > 0);
    REQUIRE(
        ecs_world_observe(world, ECS_ON_REMOVE, record_desc.id, reject_dying_commands, NULL) > 0
    );
    ecs_entity entity = ecs_world_create_entity(world);
    follower = ecs_world_create_entity(world);
    sdk_record initial = { .amount = 3 };
    REQUIRE(ecs_world_record_add(world, entity, record_desc.id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, entity, tag_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(
        observed_count == 1 && observed_is(0, ECS_ON_SET, entity, record_desc.id, 10)
    );
    pin_value amount = { .type = PIN_F32, .f = 99 };
    REQUIRE(ecs_component_write_field(world, entity, record_desc.id, 0, amount) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(observed_count == 2 && observed[1].amount == 10);
    REQUIRE(ecs_world_destroy_entity(world, entity) == ECS_OK);
    REQUIRE(!ecs_entity_is_alive(world, entity) && ecs_entity_is_alive(world, follower));
    REQUIRE(ecs_world_command_count(world) == 0 && ecs_world_live_count(world) == 1);
    return true;
}

void test_tool_ecs_strings()
{
    pu_setup(prepare_world, release_world);
    RUN_TEST_CASE(ecs_reflected_field_writes_retain_strings);
    RUN_TEST_CASE(ecs_native_text_rejects_nul_without_publishing);
    RUN_TEST_CASE(ecs_descriptor_fields_retain_every_byte);
    pu_clear_setup();
}

void test_tool_ecs()
{
    pu_setup(prepare_world, release_world);
    RUN_TEST_CASE(ecs_handles_reject_destroyed_and_canceled_generations);
    RUN_TEST_CASE(ecs_commands_wait_for_the_iteration_sync_point);
    RUN_TEST_CASE(ecs_or_chains_and_negation_match_in_slot_order);
    RUN_TEST_CASE(ecs_execution_view_refreshes_after_growth_and_registration);
    RUN_TEST_CASE(ecs_reflected_field_writes_retain_strings);
    RUN_TEST_CASE(ecs_native_text_rejects_nul_without_publishing);
    RUN_TEST_CASE(ecs_descriptor_fields_retain_every_byte);
    RUN_TEST_CASE(ecs_strings_deduplicate_collisions_and_survive_growth);
    RUN_TEST_CASE(ecs_managed_rows_outlive_sources_and_reclaim_replacements);
    RUN_TEST_CASE(ecs_managed_storage_retires_on_remove_destroy_and_world_free);
    RUN_TEST_CASE(ecs_observers_see_add_set_and_remove_at_the_sync_point);
    RUN_TEST_CASE(ecs_destroy_notifies_the_entity_and_each_component_before_removal);
    RUN_TEST_CASE(ecs_rows_destroyed_later_in_the_batch_still_report_their_set);
    RUN_TEST_CASE(ecs_explicit_adds_replace_implied_rows_and_their_storage);
    RUN_TEST_CASE(ecs_observer_commands_apply_in_the_same_sync_point);
    RUN_TEST_CASE(ecs_observer_queries_skip_rows_removed_earlier_in_the_batch);
    RUN_TEST_CASE(ecs_set_marks_coalesce_in_slot_order_until_unobserved);
    RUN_TEST_CASE(ecs_observers_rewrite_rows_without_firing_again);
    pu_clear_setup();
}
