#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/tilemap_definition.h"
#include "runtime/component_catalog.h"
#include "runtime/tilemap.h"
#include "third_party/pico_headers/pico_unit.h"

#ifdef CHIBI_TEST_PREPARATION
#include "authoring/image_prepare.h"
#include "authoring/prototype_prepare.h"
#include "runtime/images/catalog.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

static ecs_world *world;
static entities *test_entities;
#ifdef CHIBI_TEST_PREPARATION
static prototype *fixture_prototype;
static scene *fixture_scene;
#endif

static void setup()
{
    world = ecs_world_new();
    const ecs_component_desc *components[] = { &COMPONENT_Tilemap,
                                               &COMPONENT_Transform,
                                               &COMPONENT_Orientation };
    for (int i = 0; i < 3; i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    test_entities = entities_new(world, NULL);
}

static void teardown()
{
    entities_free(test_entities);
    ecs_world_free(world);
#ifdef CHIBI_TEST_PREPARATION
    prototype_free(fixture_prototype);
    fixture_prototype = NULL;
    scene_free(fixture_scene);
    fixture_scene = NULL;
    render_image_catalog_clear();
#endif
}

static ecs_entity layer(const char *cells)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(Tilemap_add(world, entity, 0) == ECS_OK);
    assert(Transform_add(world, entity, 0) == ECS_OK);
    assert(Orientation_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *Tilemap_get(world, entity) = (Tilemap){ .cells = cells,
                                             .columns = 2,
                                             .rows = 1,
                                             .frame_width = 3,
                                             .frame_height = 5 };
    *Transform_get(world, entity) = (Transform){ 10, -5 };
    return entity;
}

TEST_CASE(native_queries_and_cache_lifecycle)
{
    const native_definition_pack *pack = &TILEMAP_DEFINITION_PACK;
    REQUIRE(pack->definition_count == 2 && pack->binding_count == 2);
    REQUIRE(!strcmp(pack->definitions[0].source_name, "tilemap_frame_at"));
    REQUIRE(pack->definitions[0].inputs[0].storage == PIN_HANDLE);
    REQUIRE(pack->definitions[0].inputs[1].storage == PIN_STRUCT);
    REQUIRE(!strcmp(pack->definitions[0].inputs[1].type_name, "CF_V2"));
    REQUIRE(pack->definitions[0].effects == SCRIPT_EFFECT_READ);
    REQUIRE(!strcmp(pack->bindings[0].implementation.call->service, "game.entities"));
    ecs_entity entity = layer("7 -1");
    CF_V2 minimum = { 11, -4 }, maximum = { 14, -1 };
    pin_value arguments[] = { { .type = PIN_HANDLE,
                                .type_name = "ecs.entity",
                                .u64 = ecs_entity_to_u64(entity) },
                              { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &minimum },
                              { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &maximum } },
              result = { 0 };
    node_host_call_fn point = pack->bindings[0].implementation.call->invoke;
    node_host_call_fn box = pack->bindings[1].implementation.call->invoke;
    REQUIRE(!point(test_entities, arguments, &result));
    REQUIRE(result.type == PIN_I32 && result.i == 7);
    REQUIRE(!box(test_entities, arguments, &result) && result.i == 1);
    const char *error = NULL;
    const tilemap_grid *grid = entities_tilemap(test_entities, entity, &error);
    REQUIRE(grid && !error);
    int *frames = grid->frames;
    REQUIRE(!point(test_entities, arguments, &result) && grid->frames == frames);
    Tilemap_get(world, entity)->cells = "-1 9";
    REQUIRE(!point(test_entities, arguments, &result) && result.i == -1);
    REQUIRE(!box(test_entities, arguments, &result) && result.i == 1);
    Orientation_get(world, entity)->radians = 1.57079632679f;
    minimum = (CF_V2){ 9, -1 };
    REQUIRE(!point(test_entities, arguments, &result) && result.i == 9);
    Tilemap_get(world, entity)->cells = "broken";
    REQUIRE(point(test_entities, arguments, &result));
    Tilemap_get(world, entity)->cells = "2 3";
    entities_reset(test_entities);
    REQUIRE(!point(test_entities, arguments, &result) && result.i == 3);
    REQUIRE(ecs_world_record_destroy(world, entity, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(point(test_entities, arguments, &result));
    ecs_entity replacement = layer("4 5");
    REQUIRE(ecs_entity_to_u64(replacement) != ecs_entity_to_u64(entity));
    minimum = (CF_V2){ 11, -4 };
    arguments[0].u64 = ecs_entity_to_u64(replacement);
    REQUIRE(!point(test_entities, arguments, &result) && result.i == 4);
    ecs_entity parent = ecs_world_create_entity(world);
    REQUIRE(Transform_add(world, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(world, replacement, ECS_CHILD_OF, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    *Transform_get(world, parent) = (Transform){ -20, 30 };
    minimum = (CF_V2){ -9, 26 };
    REQUIRE(!point(test_entities, arguments, &result) && result.i == 4);
    entities_request_destroy(test_entities, replacement);
    REQUIRE(point(test_entities, arguments, &result));
    return true;
}

#ifdef CHIBI_TEST_PREPARATION
TEST_CASE(decorative_fixture_prepares_atlas_and_owns_cells)
{
    char text[4096];
    FILE *file = fopen(CHIBI_SOURCE_ROOT "/test/fixtures/sprites/content/prototypes/TileMosaic.chibi", "rb");
    REQUIRE(file);
    size_t length = fread(text, 1, sizeof text - 1, file);
    bool complete = !ferror(file) && feof(file);
    REQUIRE(fclose(file) == 0);
    REQUIRE(complete);
    text[length] = 0;
    const ecs_component_desc *components[] = { &COMPONENT_Tilemap,
                                               &COMPONENT_Transform,
                                               &COMPONENT_Orientation };
    prototype_registry registry = { .components = components, .component_count = 3 };
    char error[256] = { 0 };
    fixture_prototype = prototype_load(text, &registry, error, sizeof error);
    REQUIRE(fixture_prototype);
    render_image_catalog_clear();
    REQUIRE(
        render_image_mount_content(CHIBI_SOURCE_ROOT "/test/fixtures/sprites/content", error, sizeof error)
    );
    REQUIRE(render_image_source_count() == 0);
    fixture_scene = authoring_prototype_prepare(fixture_prototype, error, sizeof error);
    REQUIRE(fixture_scene);
    /* The atlas and its normal and height maps, each laid out like it. */
    const char *images[] = { "content/images/props.png",
                             "content/images/props_normal.png",
                             "content/images/props_height.png" };
    REQUIRE(render_image_source_count() == 3);
    for (int i = 0; i < 3; i++) {
        REQUIRE(!strcmp(render_image_source_key(i), images[i]));
        int width = 0, height = 0;
        REQUIRE(render_image_info(images[i], &width, &height));
        REQUIRE(width == 64 && height == 16);
    }
    /* Prepared data outlives the authored prototype and its input text. */
    prototype_free(fixture_prototype);
    fixture_prototype = NULL;
    memset(text, 0, sizeof text);
    REQUIRE(scene_object_count(fixture_scene) == 1);
    REQUIRE(scene_object_behavior_count(fixture_scene, 0) == 0);
    const Tilemap *map = NULL;
    const Transform *transform = NULL;
    const Orientation *orientation = NULL;
    for (int i = 0; i < scene_object_component_count(fixture_scene, 0); i++) {
        scene_component component = scene_object_component_at(fixture_scene, 0, i);
        if (component.desc->id == COMPONENT_Tilemap.id) {
            map = component.data;
        }
        if (component.desc->id == COMPONENT_Transform.id) {
            transform = component.data;
        }
        if (component.desc->id == COMPONENT_Orientation.id) {
            orientation = component.data;
        }
        REQUIRE(component.desc->id != COMPONENT_Collider.id);
    }
    REQUIRE(map && transform && orientation);
    REQUIRE(!strcmp(map->image, images[0]) && !strcmp(map->normal_map, images[1]));
    REQUIRE(!strcmp(map->height_map, images[2]) && map->relief == 4);
    REQUIRE(
        map->columns == 2 && map->rows == 2 && map->frame_width == 16 &&
        map->frame_height == 16
    );
    REQUIRE(!strcmp(map->cells, "1 2 -1 3"));
    REQUIRE(transform->x == -120 && transform->y == 40 && orientation->radians == .35f);
    tilemap_grid parsed = { 0 };
    bool valid = tilemap_grid_update(&parsed, map->cells, map->columns, map->rows, map->frame_width, map->frame_height);
    int frames[4] = { -2, -2, -2, -2 };
    if (valid) {
        memcpy(frames, parsed.frames, sizeof frames);
    }
    tilemap_grid_free(&parsed);
    REQUIRE(valid && frames[0] == 1 && frames[1] == 2 && frames[2] == -1 && frames[3] == 3);
    return true;
}

/* A tilemap whose normal map is another size than its atlas fails to prepare,
 * naming the map. */
TEST_CASE(mismatched_maps_fail_preparation)
{
    static const char
        text[] = "package asset\n"
                 "\n"
                 "import data \"game:assets\"\n"
                 "\n"
                 "pub Build :: () -> byte[] {\n"
                 "    return data.Encode(\n"
                 "        data.Object([\n"
                 "            data.Field.Named(\"name\", "
                 "data.Value.Text(\"Mismatched\")),\n"
                 "            data.Field.Named(\"components\", data.Object([\n"
                 "                data.Field.Named(\"Tilemap\", data.Object([\n"
                 "                    data.Field.Named(\"cells\", "
                 "data.Value.Text(\"1\")),\n"
                 "                    data.Field.Named(\"columns\", "
                 "data.Value.UInt(1)),\n"
                 "                    data.Field.Named(\"frame_height\", "
                 "data.Value.UInt(16)),\n"
                 "                    data.Field.Named(\"frame_width\", "
                 "data.Value.UInt(16)),\n"
                 "                    data.Field.Named(\"image\", "
                 "data.Value.Text(\"content/images/props.png\")),\n"
                 "                    data.Field.Named(\"normal_map\", "
                 "data.Value.Text(\"content/textures/EnergyCells.png\")),\n"
                 "                    data.Field.Named(\"rows\", "
                 "data.Value.UInt(1))\n"
                 "                ]))\n"
                 "            ]))\n"
                 "        ])\n"
                 "    )\n"
                 "}\n";
    const ecs_component_desc *components[] = { &COMPONENT_Tilemap };
    prototype_registry registry = { .components = components, .component_count = 1 };
    char error[256] = { 0 };
    fixture_prototype = prototype_load(text, &registry, error, sizeof error);
    REQUIRE(fixture_prototype);
    render_image_catalog_clear();
    REQUIRE(
        render_image_mount_content(CHIBI_SOURCE_ROOT "/test/fixtures/sprites/content", error, sizeof error)
    );
    REQUIRE(
        render_image_mount_content(CHIBI_SOURCE_ROOT "/test/fixtures/content", error, sizeof error)
    );
    REQUIRE(!authoring_prototype_prepare(fixture_prototype, error, sizeof error));
    REQUIRE(strstr(error, "content/textures/EnergyCells.png is 128x128, not the 64x16 of its image"));
    return true;
}
#endif

void test_tool_tilemap_native()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(native_queries_and_cache_lifecycle);
#ifdef CHIBI_TEST_PREPARATION
    RUN_TEST_CASE(decorative_fixture_prepares_atlas_and_owns_cells);
    RUN_TEST_CASE(mismatched_maps_fail_preparation);
#endif
    pu_setup(NULL, NULL);
}
