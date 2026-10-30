#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/tilemap_definition.h"
#include "runtime/component_catalog.h"
#include "runtime/tilemap.h"
#include "third_party/pico_headers/pico_unit.h"

#ifdef CHIBI_TEST_PREPARATION
#include "authoring/image_prepare.h"
#include "runtime/images/catalog.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

static ecs_world *world;
static entities *test_entities;

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
    REQUIRE(!strcmp(pack->definitions[0].inputs[1].type_name, "vec2"));
    REQUIRE(pack->definitions[0].effects == SCRIPT_EFFECT_READ);
    REQUIRE(!strcmp(pack->bindings[0].implementation.call->service, "game.entities"));
    ecs_entity entity = layer("7 -1");
    CF_V2 minimum = { 11, -4 }, maximum = { 14, -1 };
    pin_value arguments[] = { { .type = PIN_HANDLE,
                                .type_name = "ecs.entity",
                                .u64 = ecs_entity_to_u64(entity) },
                              { .type = PIN_STRUCT, .type_name = "vec2", .ptr = &minimum },
                              { .type = PIN_STRUCT, .type_name = "vec2", .ptr = &maximum } },
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

#endif

void test_tool_tilemap_native()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(native_queries_and_cache_lifecycle);
#ifdef CHIBI_TEST_PREPARATION
#endif
    pu_setup(NULL, NULL);
}
