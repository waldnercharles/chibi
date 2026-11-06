#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

/* Name and Disabled. The ChildOf relationship is not a component. */
#define BUILT_IN_COMPONENTS 2

typedef struct
{
    float x, y;
} position;

static const type_field position_fields[] = {
    { .name = "x",
      .type = PIN_F32,
      .offset = offsetof(position, x),
      .size = sizeof(float),
      .type_name = "number" },
    { .name = "y",
      .type = PIN_F32,
      .offset = offsetof(position, y),
      .size = sizeof(float),
      .type_name = "number" },
};
static const type_desc position_type = {
    .name = "sdk.ecs.world_position",
    .size = sizeof(position),
    .fields = position_fields,
    .field_count = 2,
};
static const ecs_component_desc position_desc = {
    .id = 801,
    .key = "sdk.ecs.world_position",
    .name = "Position",
    .element_size = sizeof(position),
    .type = &position_type,
    .index = ECS_INDEX_BITSET,
};
static const type_field health_fields[] = {
    { .name = "value", .type = PIN_I32, .offset = 0, .size = sizeof(int), .type_name = "int" },
};
static const type_desc health_type = {
    .name = "sdk.ecs.world_health",
    .size = sizeof(int),
    .fields = health_fields,
    .field_count = 1,
};
static const ecs_component_desc health_desc = {
    .id = 802,
    .key = "sdk.ecs.world_health",
    .name = "Health",
    .element_size = sizeof(int),
    .type = &health_type,
    .index = ECS_INDEX_DENSE,
};
static const ecs_component_desc late_desc = {
    .id = 803,
    .key = "sdk.ecs.world_late",
    .name = "Late",
    .element_size = sizeof(int),
    .type = &health_type,
};

static ecs_world *world;
static bool schema_project;

static void prepare_world()
{
    world = ecs_world_new();
    assert(ecs_world_register_component(world, &position_desc, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &health_desc, NULL) == ECS_OK);
}

static void release_world()
{
    ecs_world_free(world);
    world = NULL;
    if (schema_project) {
        type_catalog_set_project(NULL);
        schema_project = false;
    }
}

static ecs_entity spawn()
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(entity.slot);
    return entity;
}

static int add(ecs_entity entity, ecs_component_id component, const void *value)
{
    int result = ecs_world_record_add(world, entity, component, value, 0);
    return result ? result : ecs_world_apply_commands(world);
}

static int column_count(ecs_component_id component)
{
    return ecs_execution_find_column(ecs_world_execution(world), component)->count;
}

TEST_CASE(ecs_registration_reports_columns_layouts_and_counts)
{
    REQUIRE(ecs_world_component_count(world) == BUILT_IN_COMPONENTS + 2);
    const ecs_component_desc
        *registered = ecs_world_component(world, position_desc.id);
    REQUIRE(registered && registered->column_base);
    REQUIRE(registered->layout_hash == ecs_layout_hash(&position_type));
    ecs_component_desc renamed = position_desc;
    renamed.id = 0;
    renamed.name = "Renamed";
    REQUIRE(ecs_world_register_component(world, &renamed, NULL) == ECS_ERR_DUPLICATE);
    ecs_world *other = ecs_world_new();
    REQUIRE(ecs_world_component_count(other) == BUILT_IN_COMPONENTS);
    REQUIRE(!ecs_world_component(other, position_desc.id));
    ecs_world_free(other);
    return true;
}

TEST_CASE(ecs_sparse_component_ids_survive_registry_growth)
{
    enum
    {
        SPARSE = 129
    };
    ecs_entity entity = spawn();
    ecs_component_desc desc = late_desc;
    desc.key = NULL;
    for (int i = 0; i < SPARSE; i++) {
        desc.id = UINT64_C(0xffff000000000001) + (uint64_t)i * 256;
        REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_OK);
        REQUIRE(ecs_world_record_add(world, entity, desc.id, &(int){ i + 1 }, 0) == ECS_OK);
    }
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    for (int i = SPARSE - 1; i >= 0; i--) {
        ecs_component_id id = UINT64_C(0xffff000000000001) + (uint64_t)i * 256;
        const int *value = ecs_component_data_const(world, entity, id);
        REQUIRE(value && *value == i + 1);
    }
    REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_ERR_DUPLICATE);
    REQUIRE(!ecs_world_component(world, UINT64_MAX));
    desc.id = 0;
    ecs_component_id automatic = 0;
    REQUIRE(ecs_world_register_component(world, &desc, &automatic) == ECS_OK);
    REQUIRE(automatic == 1 && ecs_world_component(world, automatic));
    REQUIRE(ecs_world_component_count(world) == BUILT_IN_COMPONENTS + 2 + SPARSE + 1);
    return true;
}

TEST_CASE(ecs_rows_share_one_flat_column_per_component)
{
    ecs_entity first = spawn(), second = spawn();
    REQUIRE(second.slot == first.slot + 1);
    ecs_component_id ids[] = { position_desc.id, health_desc.id };
    size_t sizes[] = { sizeof(position), sizeof(int) };
    for (int i = 0; i < 2; i++) {
        REQUIRE(ecs_world_record_add(world, first, ids[i], NULL, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add(world, second, ids[i], NULL, 0) == ECS_OK);
    }
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    for (int i = 0; i < 2; i++) {
        const char *low = ecs_component_data_const(world, first, ids[i]);
        const char *high = ecs_component_data_const(world, second, ids[i]);
        REQUIRE(low && high && (size_t)(high - low) == sizes[i]);
    }
    return true;
}

static uint32_t added_slots[2];
static int added_count;

static void trace_add(ecs_world *, ecs_event, ecs_entity entity, ecs_component_id, void *)
{
    assert(added_count < 2);
    added_slots[added_count++] = entity.slot;
}

static uint64_t sorted_world_hash(bool reversed)
{
    ecs_world *w = ecs_world_new();
    assert(ecs_world_register_component(w, &position_desc, NULL) == ECS_OK);
    assert(ecs_world_observe(w, ECS_ON_ADD, position_desc.id, trace_add, NULL) > 0);
    ecs_entity entities[] = { ecs_world_create_entity(w), ecs_world_create_entity(w) };
    position values[] = { { 1, 0 }, { 2, 0 } };
    added_count = 0;
    for (int i = 0; i < 2; i++) {
        int k = reversed ? 1 - i : i;
        assert(
            ecs_world_record_add(w, entities[k], position_desc.id, &values[k], (uint64_t)k) == ECS_OK
        );
    }
    assert(ecs_world_apply_commands(w) == ECS_OK);
    uint64_t hash = ecs_world_hash(w);
    ecs_world_free(w);
    return hash;
}

typedef struct
{
    const char *text;
    int count;
} label;
static const type_field label_fields[] = {
    { .name = "text",
      .type = PIN_STR,
      .offset = offsetof(label, text),
      .size = sizeof(const char *),
      .type_name = "const char *" },
    { .name = "count",
      .type = PIN_I32,
      .offset = offsetof(label, count),
      .size = sizeof(int),
      .type_name = "int" },
};
static const type_desc label_type = {
    .name = "sdk.ecs.world_label",
    .size = sizeof(label),
    .fields = label_fields,
    .field_count = 2,
};
static const ecs_component_desc label_desc = {
    .id = 807,
    .key = "sdk.ecs.world_label",
    .name = "Label",
    .element_size = sizeof(label),
    .type = &label_type,
    .index = ECS_INDEX_BITSET,
};
static uint64_t labeled_world_hash(const char *text)
{
    ecs_world *w = ecs_world_new();
    assert(ecs_world_register_component(w, &label_desc, NULL) == ECS_OK);
    label value = { text, 3 };
    assert(ecs_world_record_add(w, ecs_world_create_entity(w), label_desc.id, &value, 0) == ECS_OK);
    assert(ecs_world_apply_commands(w) == ECS_OK);
    uint64_t hash = ecs_world_hash(w);
    ecs_world_free(w);
    return hash;
}

/* Two worlds whose string fields hold the same text at different addresses
 * hash alike, as game-equivalence compares worlds across processes. */
TEST_CASE(ecs_world_hash_reads_strings_by_text)
{
    char first[] = "Leader", second[] = "Leader", other[] = "Lead";
    REQUIRE(first != second);
    REQUIRE(labeled_world_hash(first) == labeled_world_hash(second));
    REQUIRE(labeled_world_hash(first) != labeled_world_hash(other));
    return true;
}

TEST_CASE(ecs_sort_keys_make_results_independent_of_recording_order)
{
    uint64_t forward = sorted_world_hash(false);
    uint64_t reversed = sorted_world_hash(true);
    REQUIRE(added_count == 2 && added_slots[0] < added_slots[1]);
    REQUIRE(forward == reversed && forward != 0);
    return true;
}

TEST_CASE(ecs_structural_errors_leave_counts_and_generations_coherent)
{
    REQUIRE(sizeof(ecs_entity) == 8 && !ecs_entity_is_alive(world, (ecs_entity){ 0 }));
    ecs_entity kept = spawn(), destroyed = spawn(), last = spawn();
    REQUIRE(add(kept, health_desc.id, NULL) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, kept, health_desc.id, NULL, 0) == ECS_ERR_DUPLICATE);
    REQUIRE(ecs_world_record_remove(world, kept, position_desc.id, 0) == ECS_ERR_COMPONENT);
    REQUIRE(ecs_world_generation(world, destroyed.slot) == destroyed.generation);
    REQUIRE(ecs_world_destroy_entity(world, destroyed) == ECS_OK);
    REQUIRE(ecs_world_generation(world, destroyed.slot) != destroyed.generation);
    REQUIRE(ecs_world_command_count(world) == 0);
    REQUIRE(ecs_world_live_count(world) == 2);
    REQUIRE(ecs_world_high_water_mark(world) >= last.slot);
    return true;
}

enum
{
    GROWN = 20001
};

static void populate_grown_world()
{
    for (uint32_t slot = 1; slot <= GROWN; slot++) {
        ecs_entity entity = { slot, ecs_world_generation(world, slot) };
        assert(ecs_entity_is_alive(world, entity));
        position value = { (float)slot, -(float)slot };
        if (slot % 2) {
            assert(ecs_world_record_add(world, entity, position_desc.id, &value, 0) == ECS_OK);
        }
        if (slot != 1 && slot % 3 == 0) {
            assert(
                ecs_world_record_add(world, entity, health_desc.id, &(int){ (int)slot }, 0) == ECS_OK
            );
        }
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
}

/* Counts results, or returns -1 when one is out of order or unexpected. */
static int count_ordered(const ecs_component_id *ids, int count, bool (*expected)(uint32_t))
{
    ecs_query query;
    ecs_entity entity;
    assert(ecs_query_begin(world, &query, ids, count) == ECS_OK);
    uint32_t previous = 0;
    int found = 0;
    while (found >= 0 && ecs_query_next(&query, &entity)) {
        found = entity.slot > previous && expected(entity.slot) ? found + 1 : -1;
        previous = entity.slot;
    }
    ecs_query_end(&query);
    return found;
}

static bool holds_both(uint32_t slot)
{
    return slot == 1 || slot % 6 == 3;
}

static bool holds_health(uint32_t slot)
{
    return slot == 1 || slot % 3 == 0;
}

TEST_CASE(ecs_growth_keeps_low_rows_and_mixed_index_queries_ordered)
{
    ecs_entity first = spawn(), last = first;
    REQUIRE(add(first, health_desc.id, &(int){ 75 }) == ECS_OK);
    for (int i = 1; i < GROWN; i++) {
        last = ecs_world_record_create(world, (uint64_t)i);
        REQUIRE(last.slot == (uint32_t)i + 1 && !ecs_entity_is_alive(world, last));
    }
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_world_live_count(world) == GROWN);
    REQUIRE(ecs_world_entity_capacity(world) >= GROWN);
    REQUIRE(*(const int *)ecs_component_data_const(world, first, health_desc.id) == 75);
    populate_grown_world();
    REQUIRE(column_count(position_desc.id) == 10001 && column_count(health_desc.id) == 6668);
    const position *row = ecs_component_data_const(world, last, position_desc.id);
    REQUIRE(row->x == GROWN && row->y == -GROWN);
    REQUIRE(*(const int *)ecs_component_data_const(world, first, health_desc.id) == 75);
    ecs_component_id ids[] = { position_desc.id, health_desc.id };
    REQUIRE(count_ordered(ids, 2, holds_both) == 3335);
    REQUIRE(count_ordered(&health_desc.id, 1, holds_health) == 6668);
    return true;
}

TEST_CASE(ecs_iteration_rejects_growth_and_registration)
{
    spawn();
    uint32_t capacity = ecs_world_entity_capacity(world);
    ecs_query query;
    REQUIRE(ecs_query_begin(world, &query, &position_desc.id, 1) == ECS_OK);
    REQUIRE(ecs_world_reserve_entities(world, capacity + 1) == ECS_ERR_ITERATING);
    REQUIRE(!ecs_world_record_create(world, 0).slot);
    REQUIRE(ecs_world_register_component(world, &late_desc, NULL) == ECS_ERR_ITERATING);
    ecs_query_end(&query);
    REQUIRE(ecs_world_iteration_begin(world) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &late_desc, NULL) == ECS_ERR_ITERATING);
    REQUIRE(ecs_world_iteration_end(world) == ECS_OK);
    REQUIRE(ecs_world_reserve_entities(world, UINT32_MAX) == ECS_ERR_CAPACITY);
    REQUIRE(ecs_world_entity_capacity(world) == capacity);
    REQUIRE(!ecs_entity_is_alive(world, (ecs_entity){ UINT32_MAX, 1 }));
    REQUIRE(ecs_world_reserve_entities(world, capacity + 1) == ECS_OK);
    REQUIRE(ecs_world_entity_capacity(world) > capacity);
    REQUIRE(ecs_world_register_component(world, &late_desc, NULL) == ECS_OK);
    return true;
}

TEST_CASE(ecs_canceled_destroys_and_slot_reuse_keep_rows_coherent)
{
    enum
    {
        COUNT = 4
    };
    ecs_entity entities[COUNT];
    for (int i = 0; i < COUNT; i++) {
        entities[i] = spawn();
        REQUIRE(add(entities[i], health_desc.id, NULL) == ECS_OK);
    }
    ecs_entity first = entities[0], last = entities[COUNT - 1];
    REQUIRE(ecs_world_record_destroy(world, last, 0) == ECS_OK);
    REQUIRE(ecs_entity_is_retiring(world, last));
    REQUIRE(ecs_world_reserve_entities(world, ecs_world_entity_capacity(world) + 1) == ECS_OK);
    REQUIRE(ecs_entity_is_retiring(world, last));
    ecs_world_clear_commands(world);
    REQUIRE(!ecs_entity_is_retiring(world, last) && ecs_entity_is_alive(world, last));
    REQUIRE(ecs_world_destroy_entity(world, last) == ECS_OK);
    REQUIRE(ecs_world_destroy_entity(world, first) == ECS_OK);
    REQUIRE(column_count(health_desc.id) == COUNT - 2);
    ecs_entity low = spawn();
    REQUIRE(low.slot == first.slot && !ecs_component_has(world, low, health_desc.id));
    ecs_entity high = spawn();
    REQUIRE(high.slot == last.slot && high.generation != last.generation);
    return true;
}

typedef struct
{
    double value, positive;
    float fraction, hint;
    int count;
    const char *label;
} bounded_row;
static const type_field bounded_fields[] = {
    { .name = "value",
      .type = PIN_F64,
      .size = sizeof(double),
      .range_contract = true,
      .min = 0,
      .max = 10,
      .range_default = 4.25 },
    { .name = "positive",
      .type = PIN_F64,
      .offset = offsetof(bounded_row, positive),
      .size = sizeof(double),
      .range_contract = true,
      .min = 2,
      .max = 5 },
    { .name = "fraction",
      .type = PIN_F32,
      .offset = offsetof(bounded_row, fraction),
      .size = sizeof(float),
      .range_contract = true,
      .min = .1,
      .max = .9 },
    { .name = "hint",
      .type = PIN_F32,
      .offset = offsetof(bounded_row, hint),
      .size = sizeof(float),
      .min = 0,
      .max = 1 },
    { .name = "count",
      .type = PIN_I32,
      .offset = offsetof(bounded_row, count),
      .size = sizeof(int),
      .range_contract = true,
      .min = 0,
      .max = 10 },
    { .name = "label",
      .type = PIN_STR,
      .offset = offsetof(bounded_row, label),
      .size = sizeof(const char *) },
};
static const type_desc bounded_type = {
    .name = "BoundedRow",
    .size = sizeof(bounded_row),
    .fields = bounded_fields,
    .field_count = 6,
};
static const ecs_component_desc bounded_desc = {
    .id = 804,
    .key = "test.BoundedRow",
    .name = "BoundedRow",
    .element_size = sizeof(bounded_row),
    .type = &bounded_type,
};
TEST_CASE(ecs_range_contracts_cover_native_publication_and_pointer_commits)
{
    REQUIRE(ecs_world_register_component(world, &bounded_desc, NULL) == ECS_OK);
    ecs_entity entity = spawn();
    REQUIRE(add(entity, bounded_desc.id, NULL) == ECS_OK);
    bounded_row *row = ecs_component_data(world, entity, bounded_desc.id);
    REQUIRE(row && row->value == 0 && row->positive == 2 && row->fraction >= .1);
    bounded_row initial = { .value = -12,
                            .positive = INFINITY,
                            .fraction = .375f,
                            .hint = 12,
                            .count = INT32_MAX };
    entity = spawn();
    REQUIRE(ecs_world_record_add(world, entity, bounded_desc.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, entity, bounded_desc.id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    row = ecs_component_data(world, entity, bounded_desc.id);
    REQUIRE(row->value == 0 && row->positive == 5 && row->fraction == .375f);
    REQUIRE(row->hint == 12 && row->count == 10);
    pin_value number = { .type = PIN_F64, .d = 1.75 };
    REQUIRE(ecs_component_write_field(world, entity, bounded_desc.id, 0, number) == ECS_OK);
    REQUIRE(row->value == 1.75);
    number.d = INFINITY;
    REQUIRE(ecs_component_write_value(world, entity, bounded_desc.id, 0, number, number) == ECS_OK);
    REQUIRE(row->value == 10);
    number.d = -INFINITY;
    REQUIRE(ecs_component_write_field(world, entity, bounded_desc.id, 0, number) == ECS_OK);
    REQUIRE(row->value == 0);
    number.d = NAN;
    REQUIRE(ecs_component_write_field(world, entity, bounded_desc.id, 0, number) == ECS_OK);
    REQUIRE(row->value == 4.25);
    initial = (bounded_row){ .value = 100,
                             .positive = NAN,
                             .fraction = INFINITY,
                             .count = -4 };
    pin_value whole = { .type = PIN_STRUCT, .type_name = bounded_type.name, .ptr = &initial };
    REQUIRE(ecs_component_write_value(world, entity, bounded_desc.id, 0, whole, whole) == ECS_OK);
    REQUIRE(row->value == 10 && row->positive == 2 && row->fraction <= .9 && row->count == 0);
    /* A borrowed pointer deliberately exposes uncommitted storage. Its owner
     * must commit before consumers or compiler range proofs may rely on it. */
    row->value = -90;
    row->fraction = NAN;
    REQUIRE(row->value == -90);
    REQUIRE(ecs_component_modified(world, entity, bounded_desc.id) == ECS_OK);
    REQUIRE(row->value == 0 && row->fraction >= .1 && row->fraction <= .9);
    /* A whole-value commit must also validate the equal managed-row fast
     * path when its source aliases a row modified through a native pointer. */
    row->value = 100;
    whole.ptr = row;
    REQUIRE(ecs_component_write_value(world, entity, bounded_desc.id, 0, whole, whole) == ECS_OK);
    REQUIRE(row->value == 10);
    return true;
}

TEST_CASE(range_metadata_rejects_unsafe_integer_endpoints)
{
    type_field field = { .type = PIN_I64, .range_contract = true, .min = 0, .max = 0x1p63 };
    REQUIRE(!type_field_range_valid(&field));
    field.max = 10.5;
    REQUIRE(!type_field_range_valid(&field));
    field.max = 10;
    REQUIRE(type_field_range_valid(&field));
    pin_value value = { .type = PIN_I64, .i64 = INT64_MAX };
    REQUIRE(pin_clamp_field(&field, value).i64 == 10);
    field.type = PIN_U64;
    field.max = 0x1p64;
    REQUIRE(!type_field_range_valid(&field));
    field.type = PIN_F32;
    field.max = INFINITY;
    REQUIRE(!type_field_range_valid(&field));
    return true;
}

TEST_CASE(ecs_registration_rejects_nested_invalid_ranges_and_unowned_values)
{
    static type_field leaf;
    leaf = (type_field){ .name = "value",
                         .type = PIN_F64,
                         .size = sizeof(double),
                         .range_contract = true,
                         .min = 0,
                         .max = 10 };
    static const type_desc nested = { .name = "test.ecs.range_leaf",
                                      .size = sizeof(double),
                                      .fields = &leaf,
                                      .field_count = 1 };
    static const type_field child = { .name = "nested",
                                      .type = PIN_STRUCT,
                                      .size = sizeof(double),
                                      .type_name = "test.ecs.range_leaf" };
    static const type_desc outer = { .name = "test.ecs.range_parent",
                                     .size = sizeof(double),
                                     .fields = &child,
                                     .field_count = 1 };
    static const type_desc *types[] = { &nested };
    type_catalog_pack pack = { .types = types, .type_count = 1 };
    type_catalog_set_project(&pack);
    schema_project = true;
    ecs_component_desc desc = { .id = 805,
                                .name = "NestedRanges",
                                .element_size = sizeof(double),
                                .type = &outer };
    int before = ecs_world_component_count(world);
    leaf.max = -1;
    REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_ERR_ARGUMENT);
    leaf.max = NAN;
    REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_ERR_ARGUMENT);
    leaf.max = 10;
    leaf.type = PIN_STR;
    REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_ERR_ARGUMENT);
    leaf.range_contract = false;
    const pin_type unowned[] = { PIN_TASK, PIN_UNION };
    for (int i = 0; i < 2; i++) {
        leaf.type = unowned[i];
        REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_ERR_ARGUMENT);
        desc.type = &nested;
        REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_ERR_ARGUMENT);
        desc.type = &outer;
    }
    REQUIRE(ecs_world_component_count(world) == before);
    leaf.type = PIN_F64;
    leaf.range_contract = true;
    REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_OK);
    ecs_entity entity = spawn();
    REQUIRE(add(entity, desc.id, &(double){ 100 }) == ECS_OK);
    const double *value = ecs_component_data_const(world, entity, desc.id);
    REQUIRE(value && *value == 10);
    return true;
}

TEST_CASE(ecs_registration_bounds_nested_range_traversal)
{
    enum
    {
        COUNT = TYPE_MAX_NESTING + 1
    };
    static type_field fields[COUNT];
    static type_desc types[COUNT];
    static char names[COUNT][48];
    static const type_desc *items[COUNT];
    for (int i = 0; i < COUNT; i++) {
        snprintf(names[i], sizeof names[i], "test.ecs.range_layer_%d", i);
        types[i] = (type_desc){ .name = names[i],
                                .size = sizeof(double),
                                .fields = &fields[i],
                                .field_count = 1 };
        items[i] = &types[i];
        fields[i] = (type_field){ .name = "value",
                                  .type = PIN_STRUCT,
                                  .size = sizeof(double),
                                  .type_name = i + 1 < COUNT ? names[i + 1] : NULL };
    }
    fields[COUNT - 1] = (type_field){ .name = "value",
                                      .type = PIN_F64,
                                      .size = sizeof(double),
                                      .range_contract = true,
                                      .min = 2,
                                      .max = 5 };
    type_catalog_pack pack = { .types = items, .type_count = COUNT };
    type_catalog_set_project(&pack);
    schema_project = true;
    ecs_component_desc desc = { .id = 806,
                                .name = "DeepRanges",
                                .element_size = sizeof(double),
                                .type = &types[0] };
    int before = ecs_world_component_count(world);
    REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_ERR_ARGUMENT);
    fields[2].type_name = names[1];
    REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_ERR_ARGUMENT);
    REQUIRE(ecs_world_component_count(world) == before);
    fields[2].type_name = names[3];
    fields[COUNT - 2] = fields[COUNT - 1];
    REQUIRE(ecs_world_register_component(world, &desc, NULL) == ECS_OK);
    ecs_entity entity = spawn();
    REQUIRE(add(entity, desc.id, NULL) == ECS_OK);
    const double *value = ecs_component_data_const(world, entity, desc.id);
    REQUIRE(value && *value == 2);
    return true;
}

void test_tool_ecs_world()
{
    pu_setup(prepare_world, release_world);
    RUN_TEST_CASE(ecs_registration_reports_columns_layouts_and_counts);
    RUN_TEST_CASE(ecs_sparse_component_ids_survive_registry_growth);
    RUN_TEST_CASE(ecs_rows_share_one_flat_column_per_component);
    RUN_TEST_CASE(ecs_sort_keys_make_results_independent_of_recording_order);
    RUN_TEST_CASE(ecs_world_hash_reads_strings_by_text);
    RUN_TEST_CASE(ecs_structural_errors_leave_counts_and_generations_coherent);
    RUN_TEST_CASE(ecs_growth_keeps_low_rows_and_mixed_index_queries_ordered);
    RUN_TEST_CASE(ecs_iteration_rejects_growth_and_registration);
    RUN_TEST_CASE(ecs_canceled_destroys_and_slot_reuse_keep_rows_coherent);
    RUN_TEST_CASE(ecs_range_contracts_cover_native_publication_and_pointer_commits);
    RUN_TEST_CASE(range_metadata_rejects_unsafe_integer_endpoints);
    RUN_TEST_CASE(ecs_registration_rejects_nested_invalid_ranges_and_unowned_values);
    RUN_TEST_CASE(ecs_registration_bounds_nested_range_traversal);
    pu_clear_setup();
}
