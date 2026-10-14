#include "ecs/ecs.h"
#include "execution/function.h"
#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/entities_native.h"
#include "runtime/player.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <string.h>

static ecs_world *world;
static player *name_player;
static scene *name_scene;
static arena name_storage;

static void prepare_world()
{
    world = ecs_world_new();
}

static void release_world()
{
    if (name_player) {
        player_free(name_player);
    }
    name_player = NULL;
    if (name_scene) {
        scene_free(name_scene);
    }
    name_scene = NULL;
    arena_free(&name_storage);
    ecs_world_free(world);
    world = NULL;
}

static ecs_entity spawn()
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(entity.slot);
    return entity;
}

static ecs_entity spawn_child(ecs_entity parent, const char *name)
{
    ecs_entity entity = spawn();
    if (parent.slot) {
        assert(ecs_world_record_add_pair(world, entity, ECS_CHILD_OF, parent, 0) == ECS_OK);
    }
    if (name) {
        assert(ecs_world_record_name(world, entity, name, 1) == ECS_OK);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

TEST_CASE(entity_name_snapshots_names_and_rejects_retiring_or_stale_handles)
{
    entities *owner = entities_new(world, NULL);
    arena storage = { 0 };
    behavior_function_context context = { .value_storage = &storage };
    ecs_entity entity = spawn_child((ecs_entity){ 0 }, "Panel");
    pin_value argument = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = ecs_entity_to_u64(entity) };
    pin_value result = { .type = PIN_STR, .type_name = "const char *" };
    REQUIRE(!ENTITY_NAME_CALL.context(&context, owner, &argument, &result));
    REQUIRE(!strcmp(result.str, "Panel"));
    const char *snapshot = result.str;
    REQUIRE(ecs_world_record_name(world, entity, "Renamed", 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!ENTITY_NAME_CALL.context(&context, owner, &argument, &result));
    REQUIRE(!strcmp(result.str, "Renamed") && !strcmp(snapshot, "Panel"));
    entities_request_destroy(owner, entity);
    REQUIRE(!ENTITY_NAME_CALL.context(&context, owner, &argument, &result));
    REQUIRE(!strcmp(result.str, ""));
    argument.u64 = 0;
    REQUIRE(!ENTITY_NAME_CALL.context(&context, owner, &argument, &result));
    REQUIRE(!strcmp(result.str, ""));
    entities_free(owner);
    arena_free(&storage);
    return true;
}

TEST_CASE(player_entity_names_use_instance_identity_without_global_name_mutation)
{
    scene_object objects[] = { { .name = "Application" }, { .name = "Widgets" } };
    scene_spec authored = { .name = "NameScope", .physics = physics_defaults(),
        .objects = objects, .object_count = 2 };
    char error[256];
    name_scene = scene_prepare(&authored, error, sizeof error);
    REQUIRE(name_scene);
    player_spec spec = { .scene = name_scene, .backend = BEHAVIOR_BYTECODE };
    name_player = player_new(&spec, error, sizeof error);
    REQUIRE(name_player);
    ecs_world *actual = world_ecs(player_world(name_player));
    entities *owner = world_entities(player_world(name_player));
    ecs_entity target = scene_instance_entity(player_scene(name_player), 1);
    REQUIRE(!ecs_entity_name(actual, target));
    behavior_function_context context = { .value_storage = &name_storage };
    pin_value argument = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = ecs_entity_to_u64(target) };
    pin_value result = { .type = PIN_STR, .type_name = "const char *" };
    REQUIRE(!ENTITY_NAME_CALL.context(&context, owner, &argument, &result));
    REQUIRE(!strcmp(result.str, "Widgets"));
    const char *snapshot = result.str;
    REQUIRE(!ecs_lookup(actual, (ecs_entity){ 0 }, "Widgets").slot);
    REQUIRE(player_load_scene(name_player, "NameScope", error, sizeof error));
    REQUIRE(!ENTITY_NAME_CALL.context(&context, owner, &argument, &result));
    REQUIRE(!strcmp(result.str, "") && !strcmp(snapshot, "Widgets"));
    argument.u64 = ecs_entity_to_u64(scene_instance_entity(player_scene(name_player), 1));
    REQUIRE(argument.u64 != ecs_entity_to_u64(target));
    REQUIRE(!ENTITY_NAME_CALL.context(&context, owner, &argument, &result));
    REQUIRE(!strcmp(result.str, "Widgets"));
    return true;
}

void test_tool_entity_names()
{
    pu_setup(prepare_world, release_world);
    RUN_TEST_CASE(entity_name_snapshots_names_and_rejects_retiring_or_stale_handles);
    RUN_TEST_CASE(player_entity_names_use_instance_identity_without_global_name_mutation);
    pu_clear_setup();
}
