#include "ecs/component_catalog.h"
#include "runtime/scenes/prepared.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/value_wire.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    float x, y;
} position;
typedef struct
{
    bool enabled;
    position position;
} frame;
typedef struct
{
    frame frame;
    const char *label;
} record;
static scene *document, *decoded;
static scene_instance *instance;
static ecs_world *world;
static unsigned char *bytes, *changed;
static size_t length;
static ecs_component_desc component;
static bool project_types;
static void cleanup()
{
    scene_instance_free(instance);
    instance = NULL;
    ecs_world_free(world);
    world = NULL;
    scene_free(document);
    scene_free(decoded);
    document = decoded = NULL;
    free(bytes);
    free(changed);
    bytes = changed = NULL;
    length = 0;
    if (project_types) {
        type_catalog_set_project(NULL);
        project_types = false;
    }
}
static bool prepare()
{
    const type_desc *type = type_catalog_find("test.record");
    if (!type || type->size != sizeof(record)) {
        return false;
    }
    component = (ecs_component_desc){ .id = 970,
                                      .name = "PreparedRecord",
                                      .key = "test.record",
                                      .type = type,
                                      .element_size = sizeof(record) };
    char label[] = "owned scene label";
    record value = { .frame = { .enabled = true, .position = { 2, 3 } }, .label = label };
    scene_component row = { &component, &value };
    const char *behaviors[] = { "Move", "Observe" };
    scene_object objects[] = {
        { .name = "First",
          .components = &row,
          .component_count = 1,
          .behaviors = behaviors,
          .behavior_count = 2 },
        { .name = "Second", .components = &row, .component_count = 1 },
        /* Sibling names need only differ under one parent. */
        { .name = "First", .components = &row, .component_count = 1 },
    };
    const int parents[] = { -1, 0, 1 };
    scene_spec spec = { .name = "PreparedScene",
                        .ui = "interface",
                        .perspective = true,
                        .physics = physics_defaults(),
                        .objects = objects,
                        .object_count = 3,
                        .parents = parents };
    char error[256];
    document = scene_prepare(&spec, error, sizeof error);
    memset(label, 0, sizeof label);
    memset(&value, 0, sizeof value);
    return document &&
           scene_prepared_write(document, &bytes, &length, error, sizeof error);
}
TEST_CASE(prepared_scene_survives_input_retirement_and_instantiation)
{
    REQUIRE(prepare());
    scene_free(document);
    document = NULL;
    const ecs_component_desc *components[] = { &component };
    ecs_component_catalog catalog = { components, 1 };
    char error[256];
    size_t budget = 64u * 1024u * 1024u;
    decoded = scene_prepared_read_budget(bytes, length, catalog, &budget, error, sizeof error);
    REQUIRE(decoded != NULL);
    REQUIRE(budget == 64u * 1024u * 1024u - scene_storage_bytes(decoded));
    free(bytes);
    bytes = NULL;
    REQUIRE(!strcmp(scene_name(decoded), "PreparedScene"));
    REQUIRE(!strcmp(scene_ui(decoded), "interface") && scene_perspective(decoded));
    REQUIRE(physics_settings_equal(scene_physics(decoded), physics_defaults()));
    REQUIRE(scene_object_count(decoded) == 3);
    REQUIRE(scene_object_parent(decoded, 0) == -1 && scene_object_parent(decoded, 2) == 1);
    REQUIRE(scene_object_behavior_count(decoded, 0) == 2);
    REQUIRE(!strcmp(scene_object_behavior(decoded, 0, 1), "Observe"));
    scene_component row = scene_object_component_at(decoded, 1, 0);
    const record *value = row.data;
    REQUIRE(
        value->frame.enabled && value->frame.position.x == 2 &&
        value->frame.position.y == 3
    );
    REQUIRE(!strcmp(value->label, "owned scene label"));
    world = ecs_world_new();
    REQUIRE(scene_register_components(decoded, world, error, sizeof error) == ECS_OK);
    instance = scene_instantiate_components(decoded, world, error, sizeof error);
    REQUIRE(instance != NULL && ecs_world_live_count(world) == 3);
    ecs_entity middle = scene_instance_entity(instance, 1);
    REQUIRE(!ecs_parent(world, scene_instance_entity(instance, 0)).slot);
    REQUIRE(ecs_parent(world, middle).slot == scene_instance_entity(instance, 0).slot);
    REQUIRE(ecs_parent(world, scene_instance_entity(instance, 2)).slot == middle.slot);
    scene_free(decoded);
    decoded = NULL;
    ecs_entity entity = scene_instance_entity(instance, 0);
    value = ecs_component_data(world, entity, component.id);
    REQUIRE(value && !strcmp(value->label, "owned scene label"));
    REQUIRE(value->frame.position.y == 3);
    return true;
}
/* Repeated authored objects can exceed 4096 total rows without exceeding any
 * table, encoded-record or owned-storage budget. */
TEST_CASE(prepared_scene_roundtrips_repeated_component_rows)
{
    enum
    {
        OBJECTS = 512,
        COMPONENTS = 8,
        STORAGE_BYTES = 4 * 1024 * 1024
    };
    const type_desc *type = type_catalog_find("test.record");
    REQUIRE(type && type->size == sizeof(record));
    ecs_component_desc descriptions[COMPONENTS];
    const ecs_component_desc *catalog[COMPONENTS];
    scene_component rows[COMPONENTS];
    char component_names[COMPONENTS][32];
    record value = { .frame = { .enabled = true, .position = { 7, 9 } },
                     .label = "repeated component" };
    for (int i = 0; i < COMPONENTS; i++) {
        snprintf(component_names[i], sizeof component_names[i], "RepeatedRecord%d", i);
        descriptions[i] = (ecs_component_desc){ .id = (ecs_component_id)(970 + i),
                                                .name = component_names[i],
                                                .key = component_names[i],
                                                .type = type,
                                                .element_size = sizeof value };
        catalog[i] = &descriptions[i];
        rows[i] = (scene_component){ &descriptions[i], &value };
    }
    scene_object objects[OBJECTS];
    char names[OBJECTS][32];
    for (int i = 0; i < OBJECTS; i++) {
        snprintf(names[i], sizeof names[i], "Object%d", i);
        objects[i] = (scene_object){ .name = names[i],
                                     .components = rows,
                                     .component_count = COMPONENTS };
    }
    scene_spec spec = { .name = "RepeatedScene",
                        .physics = physics_defaults(),
                        .objects = objects,
                        .object_count = OBJECTS };
    char error[256];
    document = scene_prepare(&spec, error, sizeof error);
    REQUIRE(document);
    REQUIRE(scene_prepared_write(document, &bytes, &length, error, sizeof error));
    REQUIRE(length < 1024 * 1024);
    size_t budget = STORAGE_BYTES;
    decoded = scene_prepared_read_budget(
        bytes,
        length,
        (ecs_component_catalog){ catalog, COMPONENTS },
        &budget,
        error,
        sizeof error
    );
    REQUIRE(decoded && scene_object_count(decoded) == OBJECTS);
    REQUIRE(budget == STORAGE_BYTES - scene_storage_bytes(decoded));
    for (int i = 0; i < OBJECTS; i++) {
        REQUIRE(!strcmp(scene_object_name(decoded, i), names[i]));
        REQUIRE(scene_object_component_count(decoded, i) == COMPONENTS);
        for (int j = 0; j < COMPONENTS; j++) {
            scene_component row = scene_object_component_at(decoded, i, j);
            const record *restored = row.data;
            REQUIRE(row.desc->id == descriptions[j].id);
            REQUIRE(
                restored->frame.enabled && restored->frame.position.x == 7 &&
                restored->frame.position.y == 9
            );
            REQUIRE(!strcmp(restored->label, value.label));
        }
    }
    scene_free(decoded);
    decoded = NULL;
    budget = 1;
    REQUIRE(!scene_prepared_read_budget(
        bytes,
        length,
        (ecs_component_catalog){ catalog, COMPONENTS },
        &budget,
        error,
        sizeof error
    ));
    REQUIRE(budget == 1 && error[0]);
    return true;
}
TEST_CASE(prepared_scene_rejects_component_identity_conflicts)
{
    const type_desc *type = type_catalog_find("test.record");
    REQUIRE(type && type->size == sizeof(record));
    ecs_component_desc first = { .id = 970,
                                 .name = "FirstRecord",
                                 .key = "first.record",
                                 .type = type,
                                 .element_size = sizeof(record) };
    ecs_component_desc second;
    record value = { 0 };
    scene_component rows[] = { { &first, &value }, { &second, &value } };
    scene_object objects[] = {
        { .name = "First", .components = &rows[0], .component_count = 1 },
        { .name = "Second", .components = &rows[1], .component_count = 1 }
    };
    scene_spec spec = { .name = "ConflictingScene",
                        .physics = physics_defaults(),
                        .objects = objects,
                        .object_count = 2 };
    char error[256];
    for (int identity = 0; identity < 4; identity++) {
        second = (ecs_component_desc){ .id = 971,
                                       .name = "SecondRecord",
                                       .key = "second.record",
                                       .type = type,
                                       .element_size = sizeof(record) };
        switch (identity) {
            case 0: second.id = first.id; break;
            case 1: second.name = first.name; break;
            case 2: second.key = first.key; break;
            case 3:
                second = first;
                second.index = ECS_INDEX_DENSE;
                break;
        }
        document = scene_prepare(&spec, error, sizeof error);
        REQUIRE(!document);
        REQUIRE(strstr(error, "identity has conflicting layouts"));
    }
    /* Separate descriptors with the same layout remain interchangeable. */
    type_desc equivalent = *type;
    second = first;
    second.type = &equivalent;
    document = scene_prepare(&spec, error, sizeof error);
    REQUIRE(document);
    scene_free(document);
    document = NULL;
    type_field fields[2];
    REQUIRE(type->field_count == 2);
    memcpy(fields, type->fields, sizeof fields);
    fields[1].name = "renamed_label";
    equivalent.fields = fields;
    document = scene_prepare(&spec, error, sizeof error);
    REQUIRE(!document);
    REQUIRE(strstr(error, "identity has conflicting layouts"));
    /* Missing optional keys do not make unrelated components identical. */
    first.key = NULL;
    second = first;
    second.id++;
    second.name = "SecondRecord";
    document = scene_prepare(&spec, error, sizeof error);
    REQUIRE(document);
    scene_free(document);
    document = NULL;
    second = first;
    objects[0].component_count = 2;
    spec.object_count = 1;
    document = scene_prepare(&spec, error, sizeof error);
    REQUIRE(!document);
    REQUIRE(strstr(error, "object component is duplicated"));
    return true;
}

TEST_CASE(prepared_scene_rechecks_shared_layouts_after_nested_catalog_replacement)
{
    const type_field wide_field = { .name = "amount",
                                    .type = PIN_F64,
                                    .size = sizeof(double),
                                    .range_contract = true,
                                    .min = 0,
                                    .max = 10 };
    type_field narrow_field = wide_field;
    narrow_field.max = 5;
    const type_desc wide = { .name = "scene.test.amount",
                             .size = sizeof(double),
                             .fields = &wide_field,
                             .field_count = 1 };
    type_desc narrow = wide;
    narrow.fields = &narrow_field;
    const type_field nested = { .name = "nested",
                                .type = PIN_STRUCT,
                                .type_name = wide.name,
                                .size = sizeof(double) };
    const type_desc outer = { .name = "scene.test.nested",
                              .size = sizeof(double),
                              .fields = &nested,
                              .field_count = 1 };
    const type_desc *types[] = { &wide };
    type_catalog_pack pack = { .types = types, .type_count = 1 };
    type_catalog_set_project(&pack);
    project_types = true;
    ecs_component_desc first = { .id = 970,
                                 .name = "NestedRange",
                                 .type = &outer,
                                 .element_size = sizeof(double),
                                 .layout_hash = ecs_layout_hash(&outer) };
    ecs_component_desc second = first;
    double amounts[] = { 9, 12 };
    scene_component rows[] = { { &first, &amounts[0] }, { &second, &amounts[1] } };
    scene_object objects[] = {
        { .name = "First", .components = &rows[0], .component_count = 1 },
        { .name = "Second", .components = &rows[1], .component_count = 1 },
    };
    scene_spec spec = { .name = "NestedRanges",
                        .physics = physics_defaults(),
                        .objects = objects,
                        .object_count = 2 };
    char error[256];
    document = scene_prepare(&spec, error, sizeof error);
    REQUIRE(document);
    REQUIRE(*(const double *)scene_object_component_at(document, 0, 0).data == 9);
    REQUIRE(*(const double *)scene_object_component_at(document, 1, 0).data == 10);
    /* Sharing a type never exempts a row's advertised hash from validation. */
    second.layout_hash ^= 1;
    decoded = scene_prepare(&spec, error, sizeof error);
    REQUIRE(!decoded && strstr(error, "layout hash differs"));
    second = first;
    type_catalog_set_project(NULL);
    types[0] = &narrow;
    type_catalog_set_project(&pack);
    REQUIRE(first.layout_hash != ecs_layout_hash(&outer));
    decoded = scene_prepare(&spec, error, sizeof error);
    REQUIRE(!decoded && strstr(error, "layout hash differs"));
    first.layout_hash = second.layout_hash = ecs_layout_hash(&outer);
    decoded = scene_prepare(&spec, error, sizeof error);
    REQUIRE(decoded);
    REQUIRE(*(const double *)scene_object_component_at(decoded, 0, 0).data == 5);
    REQUIRE(*(const double *)scene_object_component_at(decoded, 1, 0).data == 5);
    REQUIRE(*(const double *)scene_object_component_at(document, 0, 0).data == 9);
    cleanup();
    return true;
}

TEST_CASE(prepared_scene_rejects_incompatible_and_malformed_records)
{
    REQUIRE(prepare());
    const ecs_component_desc *components[] = { &component };
    ecs_component_catalog catalog = { components, 1 };
    char error[256];
    size_t budget = 1;
    REQUIRE(
        scene_prepared_read_budget(bytes, length, catalog, &budget, error, sizeof error) == NULL
    );
    REQUIRE(budget == 1);
    size_t cuts[] = { 0, 8, length - 1 };
    for (int i = 0; i < 3; i++) {
        REQUIRE(scene_prepared_read(bytes, cuts[i], catalog, error, sizeof error) == NULL);
        REQUIRE(error[0] != 0);
    }
    changed = xmalloc(length + 1);
    memcpy(changed, bytes, length);
    changed[length] = 0;
    REQUIRE(scene_prepared_read(changed, length + 1, catalog, error, sizeof error) == NULL);
    changed[8] = 127;
    REQUIRE(scene_prepared_read(changed, length, catalog, error, sizeof error) == NULL);
    REQUIRE(
        scene_prepared_read(bytes, 64u * 1024u * 1024u + 1, catalog, error, sizeof error) == NULL
    );
    ecs_component_desc wrong = component;
    const ecs_component_desc *wrong_components[] = { &wrong };
    ecs_component_catalog incompatible = { wrong_components, 1 };
    wrong.id++;
    REQUIRE(scene_prepared_read(bytes, length, incompatible, error, sizeof error) == NULL);
    wrong = component;
    wrong.key = "different.key";
    REQUIRE(scene_prepared_read(bytes, length, incompatible, error, sizeof error) == NULL);
    wrong = component;
    wrong.element_size++;
    REQUIRE(scene_prepared_read(bytes, length, incompatible, error, sizeof error) == NULL);
    wrong = component;
    wrong.layout_hash = ecs_layout_hash(wrong.type) ^ 1;
    REQUIRE(scene_prepared_read(bytes, length, incompatible, error, sizeof error) == NULL);
    const ecs_component_desc *duplicates[] = { &component, &component };
    REQUIRE(
        scene_prepared_read(bytes, length, (ecs_component_catalog){ duplicates, 2 }, error, sizeof error) ==
        NULL
    );
    /* A derived component is computed from its source, so a scene that
     * authors one is refused before it can be written. */
    ecs_component_desc derived = component;
    derived.derived_from = component.id + 1;
    record value = { 0 };
    scene_object object = { .name = "Derived",
                            .components = &(scene_component){ &derived, &value },
                            .component_count = 1 };
    scene_spec spec = { .name = "DerivedScene",
                        .physics = physics_defaults(),
                        .objects = &object,
                        .object_count = 1 };
    REQUIRE(scene_prepare(&spec, error, sizeof error) == NULL);
    REQUIRE(strstr(error, "derived"));
    /* Rejection never changed the retained source generation or valid record. */
    decoded = scene_prepared_read(bytes, length, catalog, error, sizeof error);
    REQUIRE(decoded != NULL && scene_object_count(decoded) == 3);
    return true;
}
/* A parent must precede its child, and siblings need distinct names. */
TEST_CASE(prepared_scene_rejects_misplaced_parents_and_sibling_names)
{
    scene_object objects[] = { { .name = "A" }, { .name = "B" }, { .name = "B" } };
    char error[256];
    int forward[] = { 1, -1, -1 }, itself[] = { -1, 1, -1 },
        siblings[] = { -1, 0, 0 };
    int *cases[] = { forward, itself, siblings };
    for (int i = 0; i < 3; i++) {
        scene_spec spec = { .name = "Nested",
                            .physics = physics_defaults(),
                            .objects = objects,
                            .object_count = 3,
                            .parents = cases[i] };
        REQUIRE(scene_prepare(&spec, error, sizeof error) == NULL && error[0]);
    }
    return true;
}
/* A link names a handle field of one of the scene's objects. */
TEST_CASE(prepared_scene_rejects_links_to_no_handle)
{
    record value = { 0 };
    scene_component row = { &component, &value };
    REQUIRE(type_catalog_find("test.record"));
    component = (ecs_component_desc){ .id = 970,
                                      .name = "PreparedRecord",
                                      .key = "test.record",
                                      .type = type_catalog_find("test.record"),
                                      .element_size = sizeof(record) };
    scene_object objects[] = { { .name = "A", .components = &row, .component_count = 1 },
                               { .name = "B" } };
    scene_link cases[] = {
        { 0, 0, 1, 1 }, /* a string field */
        { 0, 1, 0, 1 }, /* no second component */
        { 1, 0, 0, 0 }, /* B has no components */
        { 0, 0, 0, 2 }, /* no third object */
    };
    char error[256];
    for (int i = 0; i < 4; i++) {
        scene_spec spec = { .name = "Linked",
                            .physics = physics_defaults(),
                            .objects = objects,
                            .object_count = 2,
                            .links = &cases[i],
                            .link_count = 1 };
        REQUIRE(scene_prepare(&spec, error, sizeof error) == NULL);
        REQUIRE(!strcmp(error, "prepared scene link names no handle field or object"));
    }
    return true;
}
TEST_CASE(prepared_scene_roundtrips_markers_without_accepting_unknown_empty_structs)
{
    static ecs_component_desc marker = { .id = 0,
                                         .name = "PrototypeTag:test.marker",
                                         .key = "test.marker",
                                         .type = &TYPE_MARKER,
                                         .element_size = 1 };
    static const type_desc empty = { .name = "test.unknown.empty", .size = 1 };
    static const ecs_component_desc unknown = { .id = 972,
                                                .name = "UnknownEmpty",
                                                .key = "test.unknown.empty",
                                                .type = &empty,
                                                .element_size = 1 };
    marker.id = ecs_component_marker_id(marker.key);
    const type_desc *type = type_catalog_find("test.record");
    REQUIRE(type && type->size == sizeof(record));
    REQUIRE(type_catalog_find(TYPE_MARKER.name) == &TYPE_MARKER);
    REQUIRE(type_catalog_find(empty.name) == NULL);
    component = (ecs_component_desc){ .id = 970,
                                      .name = "PreparedRecord",
                                      .key = "test.record",
                                      .type = type,
                                      .element_size = sizeof(record) };
    char label[] = "tagged record";
    record value = { .frame = { .enabled = true, .position = { 7, 9 } }, .label = label };
    unsigned char tag = 0xa5;
    scene_component rows[] = { { &component, &value }, { &marker, &tag } };
    scene_object object = { .name = "Tagged", .components = rows, .component_count = 2 };
    scene_spec spec = { .name = "MarkerScene",
                        .physics = physics_defaults(),
                        .objects = &object,
                        .object_count = 1 };
    char error[256];
    document = scene_prepare(&spec, error, sizeof error);
    REQUIRE(document);
    memset(label, 0, sizeof label);
    memset(&value, 0, sizeof value);
    tag = 0xff;
    REQUIRE(scene_prepared_write(document, &bytes, &length, error, sizeof error));
    scene_free(document);
    document = NULL;
    const ecs_component_desc *components[] = { &component, &marker };
    decoded = scene_prepared_read(bytes, length, (ecs_component_catalog){ components, 2 }, error, sizeof error);
    REQUIRE(decoded && scene_object_count(decoded) == 1);
    REQUIRE(scene_object_component_count(decoded, 0) == 2);
    scene_component marked = scene_object_component_at(decoded, 0, 1);
    REQUIRE(
        marked.desc && marked.desc->id == marker.id && marked.desc->type == &TYPE_MARKER &&
        marked.data && *(const unsigned char *)marked.data == 0
    );
    const record *restored = scene_object_component_at(decoded, 0, 0).data;
    REQUIRE(
        restored && restored->frame.enabled &&
        restored->frame.position.x == 7 && restored->frame.position.y == 9
    );
    REQUIRE(!strcmp(restored->label, "tagged record"));
    scene_free(decoded);
    decoded = scene_prepared_read(bytes, length, (ecs_component_catalog){ components, 1 }, error, sizeof error);
    REQUIRE(decoded && scene_object_component_count(decoded, 0) == 2);
    marked = scene_object_component_at(decoded, 0, 1);
    REQUIRE(marked.desc->id == marker.id && marked.desc->type == &TYPE_MARKER);
    REQUIRE(
        !strcmp(marked.desc->name, marker.name) &&
        !strcmp(marked.desc->key, marker.key)
    );
    REQUIRE(*(const unsigned char *)marked.data == 0);

    /* Locate the unique field-wise marker header, then corrupt one schema field. */
    execution_wire encoded = { 0 };
    uint64_t marker_id = marker.id;
    execution_wire_u64(&encoded, &marker_id);
    size_t marker_offset = 0;
    int matches = 0;
    for (size_t i = 0; i + encoded.size <= length; i++) {
        if (!memcmp(bytes + i, encoded.data, encoded.size)) {
            marker_offset = i;
            matches++;
        }
    }
    afree(encoded.data);
    REQUIRE(matches == 1);
    arena storage = { 0 };
    execution_wire wire = { .data = bytes,
                            .size = length,
                            .offset = marker_offset,
                            .reading = true,
                            .storage = &storage };
    uint64_t id = 0, layout = 0;
    size_t element_size = 0;
    const char *name = NULL, *key = NULL;
    execution_wire_u64(&wire, &id);
    size_t layout_offset = wire.offset;
    execution_wire_u64(&wire, &layout);
    execution_wire_size(&wire, &element_size);
    execution_wire_string(&wire, &name);
    execution_wire_string(&wire, &key);
    size_t index_offset = wire.offset;
    bool header_valid = !wire.error && id == marker.id &&
                        !strcmp(name, marker.name) && !strcmp(key, marker.key);
    arena_free(&storage);
    REQUIRE(header_valid && index_offset < length);
    changed = xmalloc(length);
    size_t offsets[] = { marker_offset, layout_offset, index_offset };
    for (int i = 0; i < 3; i++) {
        memcpy(changed, bytes, length);
        changed[offsets[i]] ^= 1;
        size_t budget = 64u * 1024u * 1024u;
        scene *rejected = scene_prepared_read_budget(
            changed,
            length,
            (ecs_component_catalog){ components, 1 },
            &budget,
            error,
            sizeof error
        );
        bool refused = rejected == NULL;
        scene_free(rejected);
        REQUIRE(refused && error[0]);
        REQUIRE(budget == 64u * 1024u * 1024u);
    }
    free(changed);
    changed = NULL;
    free(bytes);
    bytes = NULL;
    world = ecs_world_new();
    REQUIRE(scene_register_components(decoded, world, error, sizeof error) == ECS_OK);
    instance = scene_instantiate_components(decoded, world, error, sizeof error);
    REQUIRE(instance && ecs_world_live_count(world) == 1);
    scene_free(decoded);
    decoded = NULL;
    ecs_entity entity = scene_instance_entity(instance, 0);
    const unsigned char *presence = ecs_component_data(world, entity, marker.id);
    REQUIRE(presence && *presence == 0);
    restored = ecs_component_data(world, entity, component.id);
    REQUIRE(
        restored && !strcmp(restored->label, "tagged record") &&
        restored->frame.position.y == 9
    );
    rows[1] = (scene_component){ &unknown, &tag };
    document = scene_prepare(&spec, error, sizeof error);
    REQUIRE(document);
    REQUIRE(!scene_prepared_write(document, &bytes, &length, error, sizeof error));
    REQUIRE(bytes == NULL && length == 0 && error[0]);
    REQUIRE(ecs_world_live_count(world) == 1 && *presence == 0);
    return true;
}
TEST_CASE(prepared_integers_roundtrip_in_shortest_form)
{
    static const uint64_t words[] = { 0, 127, 128, 16383, 16384, UINT64_MAX };
    static const size_t word_sizes[] = { 1, 1, 2, 2, 3, 10 };
    static const int ints[] = { 0, -1, 1, -64, 64, INT_MIN, INT_MAX };
    static const size_t int_sizes[] = { 1, 1, 1, 1, 2, 5, 5 };
    for (size_t i = 0; i < sizeof words / sizeof *words; i++) {
        execution_wire writer = { 0 };
        uint64_t value = words[i];
        execution_wire_u64(&writer, &value);
        REQUIRE(!writer.error && writer.size == word_sizes[i]);
        execution_wire reader = { .data = writer.data, .size = writer.size, .reading = true };
        value = 0;
        execution_wire_u64(&reader, &value);
        REQUIRE(!reader.error && value == words[i] && reader.offset == writer.size);
        afree(writer.data);
    }
    for (size_t i = 0; i < sizeof ints / sizeof *ints; i++) {
        execution_wire writer = { 0 };
        int value = ints[i];
        execution_wire_int(&writer, &value);
        REQUIRE(!writer.error && writer.size == int_sizes[i]);
        execution_wire reader = { .data = writer.data, .size = writer.size, .reading = true };
        value = 0;
        execution_wire_int(&reader, &value);
        REQUIRE(!reader.error && value == ints[i]);
        afree(writer.data);
    }
    static unsigned char overlong[] = { 0x80, 0x00 };
    static unsigned char too_wide[] = { 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff, 0x02 };
    static unsigned char truncated[] = { 0x80 };
    static unsigned char out_of_int[] = { 0x80, 0x80, 0x80, 0x80, 0x10 };
    uint64_t word = 0;
    int number = 0;
    execution_wire reader = { .data = overlong, .size = sizeof overlong, .reading = true };
    execution_wire_u64(&reader, &word);
    REQUIRE(reader.error);
    reader = (execution_wire){ .data = too_wide, .size = sizeof too_wide, .reading = true };
    execution_wire_u64(&reader, &word);
    REQUIRE(reader.error);
    reader = (execution_wire){ .data = truncated, .size = sizeof truncated, .reading = true };
    execution_wire_u64(&reader, &word);
    REQUIRE(reader.error);
    reader = (execution_wire){ .data = out_of_int,
                               .size = sizeof out_of_int,
                               .reading = true };
    execution_wire_int(&reader, &number);
    REQUIRE(reader.error);
    return true;
}
TEST_CASE(prepared_wire_bounds_use_encoded_width)
{
    /* A one-byte value fits in the last free byte of an artifact. */
    uint64_t hash = 0, zero = 0;
    execution_wire last = { .hash = &hash, .size = EXECUTION_WIRE_MAX_BYTES - 1 };
    last.offset = last.size;
    execution_wire_u64(&last, &zero);
    REQUIRE(!last.error && last.size == EXECUTION_WIRE_MAX_BYTES);

    /* A trailing collection of one-byte scalars decodes after writing. */
    pin_value items[4] = { 0 };
    for (int i = 0; i < 4; i++) {
        items[i] = (pin_value){ .type = PIN_I32, .i = i };
    }
    pin_array array = { .element_type = PIN_I32, .count = 4, .capacity = 4, .items = items };
    pin_value list = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = &array };
    execution_wire writer = { 0 };
    execution_wire_value(&writer, &list);
    REQUIRE(!writer.error);
    arena storage = { 0 };
    execution_wire reader = { .data = writer.data,
                              .size = writer.size,
                              .reading = true,
                              .storage = &storage };
    pin_value read = { 0 };
    execution_wire_value(&reader, &read);
    const pin_array *decoded_array = read.ptr;
    bool same = !reader.error && reader.offset == writer.size && decoded_array &&
                decoded_array->count == 4 && pin_array_get(decoded_array, 3).i == 3;
    arena_free(&storage);
    afree(writer.data);
    REQUIRE(same);
    return true;
}
void test_scene_prepared()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prepared_integers_roundtrip_in_shortest_form);
    RUN_TEST_CASE(prepared_wire_bounds_use_encoded_width);
    RUN_TEST_CASE(prepared_scene_survives_input_retirement_and_instantiation);
    RUN_TEST_CASE(prepared_scene_roundtrips_repeated_component_rows);
    RUN_TEST_CASE(prepared_scene_rejects_component_identity_conflicts);
    RUN_TEST_CASE(prepared_scene_rechecks_shared_layouts_after_nested_catalog_replacement);
    RUN_TEST_CASE(prepared_scene_rejects_incompatible_and_malformed_records);
    RUN_TEST_CASE(prepared_scene_rejects_misplaced_parents_and_sibling_names);
    RUN_TEST_CASE(prepared_scene_rejects_links_to_no_handle);
    RUN_TEST_CASE(prepared_scene_roundtrips_markers_without_accepting_unknown_empty_structs);
    pu_clear_setup();
}
