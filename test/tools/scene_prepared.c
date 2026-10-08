#include "ecs/component_catalog.h"
#include "runtime/scenes/prepared.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/value_wire.h"

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
static game_scene *scene, *decoded;
static game_scene_instance *instance;
static ecs_world *world;
static unsigned char *bytes, *changed;
static size_t length;
static ecs_component_desc component;
static void cleanup()
{
    game_scene_instance_free(instance);
    instance = NULL;
    ecs_world_free(world);
    world = NULL;
    game_scene_free(scene);
    game_scene_free(decoded);
    scene = decoded = NULL;
    free(bytes);
    free(changed);
    bytes = changed = NULL;
    length = 0;
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
    game_scene_component row = { &component, &value };
    const char *behaviors[] = { "Move", "Observe" };
    game_scene_object objects[] = {
        { .name = "First",
          .components = &row,
          .component_count = 1,
          .behaviors = behaviors,
          .behavior_count = 2 },
        { .name = "Second", .components = &row, .component_count = 1 },
    };
    game_scene_spec spec = { .name = "PreparedScene",
                             .ui = "interface",
                             .perspective = true,
                             .physics = game_physics_defaults(),
                             .objects = objects,
                             .object_count = 2 };
    char error[256];
    scene = game_scene_prepare(&spec, error, sizeof error);
    memset(label, 0, sizeof label);
    memset(&value, 0, sizeof value);
    return scene &&
           game_scene_prepared_write(scene, &bytes, &length, error, sizeof error);
}
TEST_CASE(prepared_scene_survives_input_retirement_and_instantiation)
{
    REQUIRE(prepare());
    game_scene_free(scene);
    scene = NULL;
    const ecs_component_desc *components[] = { &component };
    ecs_component_catalog catalog = { components, 1 };
    char error[256];
    size_t budget = 64u * 1024u * 1024u;
    decoded = game_scene_prepared_read_budget(bytes, length, catalog, &budget, error, sizeof error);
    REQUIRE(decoded != NULL);
    REQUIRE(budget == 64u * 1024u * 1024u - game_scene_storage_bytes(decoded));
    free(bytes);
    bytes = NULL;
    REQUIRE(!strcmp(game_scene_name(decoded), "PreparedScene"));
    REQUIRE(!strcmp(game_scene_ui(decoded), "interface") && game_scene_perspective(decoded));
    REQUIRE(game_physics_settings_equal(game_scene_physics(decoded), game_physics_defaults()));
    REQUIRE(game_scene_object_count(decoded) == 2);
    REQUIRE(game_scene_object_behavior_count(decoded, 0) == 2);
    REQUIRE(!strcmp(game_scene_object_behavior(decoded, 0, 1), "Observe"));
    game_scene_component row = game_scene_object_component_at(decoded, 1, 0);
    const record *value = row.data;
    REQUIRE(
        value->frame.enabled && value->frame.position.x == 2 &&
        value->frame.position.y == 3
    );
    REQUIRE(!strcmp(value->label, "owned scene label"));
    world = ecs_world_new();
    REQUIRE(game_scene_register_components(decoded, world, error, sizeof error) == ECS_OK);
    instance = game_scene_instantiate_components(decoded, world, error, sizeof error);
    REQUIRE(instance != NULL && ecs_world_live_count(world) == 2);
    game_scene_free(decoded);
    decoded = NULL;
    ecs_entity entity = game_scene_instance_entity(instance, 0);
    value = ecs_component_data(world, entity, component.id);
    REQUIRE(value && !strcmp(value->label, "owned scene label"));
    REQUIRE(value->frame.position.y == 3);
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
        game_scene_prepared_read_budget(bytes, length, catalog, &budget, error, sizeof error) == NULL
    );
    REQUIRE(budget == 1);
    size_t cuts[] = { 0, 8, length - 1 };
    for (int i = 0; i < 3; i++) {
        REQUIRE(game_scene_prepared_read(bytes, cuts[i], catalog, error, sizeof error) == NULL);
        REQUIRE(error[0] != 0);
    }
    changed = xmalloc(length + 1);
    memcpy(changed, bytes, length);
    changed[length] = 0;
    REQUIRE(game_scene_prepared_read(changed, length + 1, catalog, error, sizeof error) == NULL);
    changed[8] = 127;
    REQUIRE(game_scene_prepared_read(changed, length, catalog, error, sizeof error) == NULL);
    REQUIRE(
        game_scene_prepared_read(bytes, 64u * 1024u * 1024u + 1, catalog, error, sizeof error) == NULL
    );
    ecs_component_desc wrong = component;
    const ecs_component_desc *wrong_components[] = { &wrong };
    ecs_component_catalog incompatible = { wrong_components, 1 };
    wrong.id++;
    REQUIRE(game_scene_prepared_read(bytes, length, incompatible, error, sizeof error) == NULL);
    wrong = component;
    wrong.key = "different.key";
    REQUIRE(game_scene_prepared_read(bytes, length, incompatible, error, sizeof error) == NULL);
    wrong = component;
    wrong.element_size++;
    REQUIRE(game_scene_prepared_read(bytes, length, incompatible, error, sizeof error) == NULL);
    wrong = component;
    wrong.layout_hash = ecs_layout_hash(wrong.type) ^ 1;
    REQUIRE(game_scene_prepared_read(bytes, length, incompatible, error, sizeof error) == NULL);
    const ecs_component_desc *duplicates[] = { &component, &component };
    REQUIRE(
        game_scene_prepared_read(bytes, length, (ecs_component_catalog){ duplicates, 2 }, error, sizeof error) ==
        NULL
    );
    /* Rejection never changed the retained source generation or valid record. */
    decoded = game_scene_prepared_read(bytes, length, catalog, error, sizeof error);
    REQUIRE(decoded != NULL && game_scene_object_count(decoded) == 2);
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
    game_scene_component rows[] = { { &component, &value }, { &marker, &tag } };
    game_scene_object object = { .name = "Tagged",
                                 .components = rows,
                                 .component_count = 2 };
    game_scene_spec spec = { .name = "MarkerScene",
                             .physics = game_physics_defaults(),
                             .objects = &object,
                             .object_count = 1 };
    char error[256];
    scene = game_scene_prepare(&spec, error, sizeof error);
    REQUIRE(scene);
    memset(label, 0, sizeof label);
    memset(&value, 0, sizeof value);
    tag = 0xff;
    REQUIRE(game_scene_prepared_write(scene, &bytes, &length, error, sizeof error));
    game_scene_free(scene);
    scene = NULL;
    const ecs_component_desc *components[] = { &component, &marker };
    decoded = game_scene_prepared_read(
        bytes,
        length,
        (ecs_component_catalog){ components, 2 },
        error,
        sizeof error
    );
    REQUIRE(decoded && game_scene_object_count(decoded) == 1);
    REQUIRE(game_scene_object_component_count(decoded, 0) == 2);
    game_scene_component marked = game_scene_object_component_at(decoded, 0, 1);
    REQUIRE(
        marked.desc && marked.desc->id == marker.id && marked.desc->type == &TYPE_MARKER &&
        marked.data && *(const unsigned char *)marked.data == 0
    );
    const record *restored = game_scene_object_component_at(decoded, 0, 0).data;
    REQUIRE(
        restored && restored->frame.enabled &&
        restored->frame.position.x == 7 && restored->frame.position.y == 9
    );
    REQUIRE(!strcmp(restored->label, "tagged record"));
    game_scene_free(decoded);
    decoded = game_scene_prepared_read(
        bytes,
        length,
        (ecs_component_catalog){ components, 1 },
        error,
        sizeof error
    );
    REQUIRE(decoded && game_scene_object_component_count(decoded, 0) == 2);
    marked = game_scene_object_component_at(decoded, 0, 1);
    REQUIRE(marked.desc->id == marker.id && marked.desc->type == &TYPE_MARKER);
    REQUIRE(
        !strcmp(marked.desc->name, marker.name) &&
        !strcmp(marked.desc->key, marker.key)
    );
    REQUIRE(*(const unsigned char *)marked.data == 0);

    /* Locate the unique field-wise marker header, then corrupt one schema field. */
    size_t marker_offset = 0;
    int matches = 0;
    for (size_t i = 0; i + 24 <= length; i++) {
        uint64_t id = 0;
        for (int byte = 0; byte < 8; byte++) {
            id |= (uint64_t)bytes[i + byte] << (8 * byte);
        }
        if (id == marker.id) {
            marker_offset = i;
            matches++;
        }
    }
    REQUIRE(matches == 1);
    arena storage = { 0 };
    execution_wire wire = { .data = bytes,
                            .size = length,
                            .offset = marker_offset + 24,
                            .reading = true,
                            .storage = &storage };
    const char *name = NULL, *key = NULL;
    execution_wire_string(&wire, &name);
    execution_wire_string(&wire, &key);
    size_t index_offset = wire.offset;
    bool header_valid = !wire.error && !strcmp(name, marker.name) &&
                        !strcmp(key, marker.key);
    arena_free(&storage);
    REQUIRE(header_valid && index_offset < length);
    changed = xmalloc(length);
    size_t offsets[] = { marker_offset, marker_offset + 8, index_offset };
    for (int i = 0; i < 3; i++) {
        memcpy(changed, bytes, length);
        changed[offsets[i]] ^= 1;
        size_t budget = 64u * 1024u * 1024u;
        game_scene *rejected = game_scene_prepared_read_budget(
            changed,
            length,
            (ecs_component_catalog){ components, 1 },
            &budget,
            error,
            sizeof error
        );
        bool refused = rejected == NULL;
        game_scene_free(rejected);
        REQUIRE(refused && error[0]);
        REQUIRE(budget == 64u * 1024u * 1024u);
    }
    free(changed);
    changed = NULL;
    free(bytes);
    bytes = NULL;
    world = ecs_world_new();
    REQUIRE(game_scene_register_components(decoded, world, error, sizeof error) == ECS_OK);
    instance = game_scene_instantiate_components(decoded, world, error, sizeof error);
    REQUIRE(instance && ecs_world_live_count(world) == 1);
    game_scene_free(decoded);
    decoded = NULL;
    ecs_entity entity = game_scene_instance_entity(instance, 0);
    const unsigned char *presence = ecs_component_data(world, entity, marker.id);
    REQUIRE(presence && *presence == 0);
    restored = ecs_component_data(world, entity, component.id);
    REQUIRE(
        restored && !strcmp(restored->label, "tagged record") &&
        restored->frame.position.y == 9
    );
    rows[1] = (game_scene_component){ &unknown, &tag };
    scene = game_scene_prepare(&spec, error, sizeof error);
    REQUIRE(scene);
    REQUIRE(!game_scene_prepared_write(scene, &bytes, &length, error, sizeof error));
    REQUIRE(bytes == NULL && length == 0 && error[0]);
    REQUIRE(ecs_world_live_count(world) == 1 && *presence == 0);
    return true;
}
void test_scene_prepared()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prepared_scene_survives_input_retirement_and_instantiation);
    RUN_TEST_CASE(prepared_scene_rejects_incompatible_and_malformed_records);
    RUN_TEST_CASE(prepared_scene_roundtrips_markers_without_accepting_unknown_empty_structs);
    pu_clear_setup();
}
