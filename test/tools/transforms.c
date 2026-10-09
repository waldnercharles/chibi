#include "runtime/component_catalog.h"
#include "runtime/transform_propagation.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <cute_math3d.h>
#include <math.h>

/* Transform propagation over a world with only the transform components. */
static ecs_world *world;

static void setup()
{
    world = ecs_world_new();
    /* A global registered first still comes with its local. */
    const ecs_component_desc *components[] = { &GAME_COMPONENT_GameGlobalTransform2D,
                                               &GAME_COMPONENT_GameTransform,
                                               &GAME_COMPONENT_GameOrientation,
                                               &GAME_COMPONENT_GameTransform3D,
                                               &GAME_COMPONENT_GameGlobalTransform3D };
    for (int i = 0; i < 5; i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
}

static void teardown()
{
    ecs_world_free(world);
}

static ecs_entity placed(ecs_entity parent, float x, float y, float radians)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameTransform_add(world, entity, 0) == ECS_OK);
    if (radians) {
        assert(GAME_GameOrientation_add(world, entity, 0) == ECS_OK);
    }
    if (parent.slot) {
        assert(ecs_world_record_add_pair(world, entity, ECS_CHILD_OF, parent, 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameTransform_get(world, entity) = (GameTransform){ x, y };
    if (radians) {
        GAME_GameOrientation_get(world, entity)->radians = radians;
    }
    return entity;
}

static bool near(float a, float b)
{
    return fabsf(a - b) < .0001f;
}

static bool matches_walk(ecs_entity entity)
{
    const GameGlobalTransform2D *global = GAME_GameGlobalTransform2D_get_const(world, entity);
    GameGlobalTransform2D walked = game_transform_world_2d(world, entity);
    return global && global->position.x == walked.position.x &&
           global->position.y == walked.position.y &&
           global->radians == walked.radians;
}

static bool at(ecs_entity entity, float x, float y)
{
    const GameGlobalTransform2D *global = GAME_GameGlobalTransform2D_get_const(world, entity);
    return near(global->position.x, x) && near(global->position.y, y) &&
           matches_walk(entity);
}

/* Children compose with their parent's placement whatever order they were
 * created in, and an ancestor without a Transform is skipped. */
TEST_CASE(transforms_compose_parents_first)
{
    ecs_entity grandchild = placed((ecs_entity){ 0 }, 1, 0, 0);
    ecs_entity root = placed((ecs_entity){ 0 }, 10.25f, -3.5f, 1.57079633f);
    ecs_entity group = ecs_world_create_entity(world);
    REQUIRE(ecs_world_record_add_pair(world, group, ECS_CHILD_OF, root, 0) == ECS_OK);
    ecs_entity child = placed(group, 5, 0, 1.57079633f);
    REQUIRE(ecs_world_record_add_pair(world, grandchild, ECS_CHILD_OF, child, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!GAME_GameGlobalTransform2D_get_const(world, group));
    REQUIRE(game_transform_propagate(world) == ECS_OK);
    const GameGlobalTransform2D *top = GAME_GameGlobalTransform2D_get_const(world, root);
    REQUIRE(top->position.x == 10.25f && top->position.y == -3.5f && top->radians == 1.57079633f);
    REQUIRE(at(child, 10.25f, 1.5f));
    REQUIRE(at(grandchild, 9.25f, 1.5f));
    REQUIRE(near(GAME_GameGlobalTransform2D_get_const(world, grandchild)->radians, 3.14159265f));
    return true;
}

TEST_CASE(transforms_follow_moves_and_reparenting)
{
    ecs_entity first = placed((ecs_entity){ 0 }, 100, 0, 0);
    ecs_entity second = placed((ecs_entity){ 0 }, 0, 50, 0);
    ecs_entity child = placed(first, 1, 2, 0);
    REQUIRE(game_transform_propagate(world) == ECS_OK);
    REQUIRE(at(child, 101, 2));
    GAME_GameTransform_get(world, first)->x = 200;
    REQUIRE(at(child, 101, 2) == false);
    REQUIRE(game_transform_propagate(world) == ECS_OK);
    REQUIRE(at(child, 201, 2));
    REQUIRE(ecs_world_record_add_pair(world, child, ECS_CHILD_OF, second, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(game_transform_propagate(world) == ECS_OK);
    REQUIRE(at(child, 1, 52));
    REQUIRE(ecs_world_record_remove_pair(world, child, ECS_CHILD_OF, second, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(game_transform_propagate(world) == ECS_OK);
    REQUIRE(at(child, 1, 2));
    return true;
}

static ecs_entity placed_3d(ecs_entity parent, GameTransform3D local)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameTransform3D_add(world, entity, 0) == ECS_OK);
    if (parent.slot) {
        assert(ecs_world_record_add_pair(world, entity, ECS_CHILD_OF, parent, 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameTransform3D_get(world, entity) = local;
    return entity;
}

static bool near_3d(CF_V3 v, float x, float y, float z)
{
    return near(v.x, x) && near(v.y, y) && near(v.z, z);
}

TEST_CASE(transforms_compose_3d_matrices)
{
    ecs_entity root = placed_3d(
        (ecs_entity){ 0 },
        (GameTransform3D){
            .position = { 1, 2, 3 },
            .rotation = cf_quat_identity(),
            .scale = { 2, 2, 2 },
        }
    );
    ecs_entity child = placed_3d(
        root,
        (GameTransform3D){
            .position = { 1, 0, 0 },
            .rotation = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, 1.57079633f),
            .scale = { 1, 1, 1 },
        }
    );
    /* A zero rotation reads as identity. */
    ecs_entity leaf = placed_3d(
        child,
        (GameTransform3D){ .position = { 1, 0, 0 }, .scale = { 1, 1, 1 } }
    );
    REQUIRE(game_transform_propagate(world) == ECS_OK);
    const GameGlobalTransform3D *top = GAME_GameGlobalTransform3D_get_const(world, root);
    REQUIRE(near_3d(top->position, 1, 2, 3) && near_3d(top->x_axis, 2, 0, 0));
    const GameGlobalTransform3D *middle = GAME_GameGlobalTransform3D_get_const(world, child);
    REQUIRE(near_3d(middle->position, 3, 2, 3));
    REQUIRE(near_3d(middle->x_axis, 0, 2, 0) && near_3d(middle->y_axis, -2, 0, 0));
    REQUIRE(near_3d(middle->z_axis, 0, 0, 2));
    REQUIRE(near_3d(GAME_GameGlobalTransform3D_get_const(world, leaf)->position, 3, 4, 3));
    return true;
}

void test_tool_transforms()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(transforms_compose_parents_first);
    RUN_TEST_CASE(transforms_follow_moves_and_reparenting);
    RUN_TEST_CASE(transforms_compose_3d_matrices);
    pu_clear_setup();
}
