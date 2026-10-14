#include "third_party/pico_headers/pico_unit.h"

#include <box3d/box3d.h>
#include <box3d/collision.h>
#include <box3d/math_functions.h>
#include <math.h>

static const float step = 1.0f / 60.0f;
static const int substeps = 4;
static const int frames = 120;
static b3WorldId world;

static void cleanup()
{
    if (b3World_IsValid(world)) {
        b3DestroyWorld(world);
    }
}

static b3BodyId add_box(b3BodyType type, b3Pos position, float hx, float hy, float hz)
{
    b3BodyDef body = b3DefaultBodyDef();
    body.type = type;
    body.position = position;
    b3BodyId id = b3CreateBody(world, &body);
    b3BoxHull box = b3MakeBoxHull(hx, hy, hz);
    b3ShapeDef shape = b3DefaultShapeDef();
    shape.density = 1.0f;
    b3CreateHullShape(id, &shape, &box.base);
    return id;
}

// A closest ray straight down from 10 m above the ground plane.
static b3RayResult cast_down(float x)
{
    return b3World_CastRayClosest(
        world,
        (b3Pos){ x, 10.0f, 0.0f },
        (b3Vec3){ 0.0f, -20.0f, 0.0f },
        b3DefaultQueryFilter()
    );
}

static bool within(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

TEST_CASE(box3d_box_falls_rests_and_answers_raycasts)
{
    // The default world has 10 m/s^2 gravity along -y.
    b3WorldDef def = b3DefaultWorldDef();
    world = b3CreateWorld(&def);
    REQUIRE(b3World_IsValid(world));
    b3BodyId ground = add_box(b3_staticBody, (b3Pos){ 0.0f, -1.0f, 0.0f }, 20.0f, 1.0f, 20.0f);
    b3BodyId cube = add_box(b3_dynamicBody, (b3Pos){ 0.0f, 4.0f, 0.0f }, 0.5f, 0.5f, 0.5f);

    b3World_Step(world, step, substeps);
    float falling = b3Body_GetPosition(cube).y;
    REQUIRE(falling < 4.0f);
    REQUIRE(b3Body_GetLinearVelocity(cube).y < 0.0f);
    for (int i = 1; i < frames; i++) {
        b3World_Step(world, step, substeps);
    }
    b3Pos rest = b3Body_GetPosition(cube);
    REQUIRE(within(rest.y, 0.5f, 0.02f));
    REQUIRE(within(rest.x, 0.0f, 0.01f) && within(rest.z, 0.0f, 0.01f));
    REQUIRE(b3Length(b3Body_GetLinearVelocity(cube)) < 0.05f);

    b3RayResult cube_hit = cast_down(0.0f);
    REQUIRE(cube_hit.hit && B3_ID_EQUALS(b3Shape_GetBody(cube_hit.shapeId), cube));
    REQUIRE(within(cube_hit.point.y, 1.0f, 0.02f));
    REQUIRE(within(cube_hit.normal.y, 1.0f, 1e-3f));
    b3RayResult ground_hit = cast_down(5.0f);
    REQUIRE(ground_hit.hit && B3_ID_EQUALS(b3Shape_GetBody(ground_hit.shapeId), ground));
    REQUIRE(within(ground_hit.point.y, 0.0f, 1e-3f));
    REQUIRE(within(ground_hit.fraction, 0.5f, 1e-3f));
    REQUIRE(!cast_down(30.0f).hit);

    b3DestroyWorld(world);
    REQUIRE(!b3World_IsValid(world));
    return true;
}

TEST_CASE(box3d_contact_manifold_storage_can_be_reused)
{
    b3WorldDef def = b3DefaultWorldDef();
    world = b3CreateWorld(&def);
    add_box(b3_staticBody, (b3Pos){ 0.0f, -1.0f, 0.0f }, 20.0f, 1.0f, 20.0f);

    // Single manifolds have float alignment. Their allocator freelist stores pointers.
    // Several contacts exercise consecutive slots, then both freelist writes and reads.
    for (int pass = 0; pass < 2; pass++) {
        b3BodyId cubes[4];
        for (int i = 0; i < 4; i++) {
            cubes[i] = add_box(b3_dynamicBody, (b3Pos){ 3.0f * i, 0.5f, 0.0f }, 0.5f, 0.5f, 0.5f);
        }
        for (int i = 0; i < frames; i++) {
            b3World_Step(world, step, substeps);
        }
        for (int i = 0; i < 4; i++) {
            REQUIRE(within(b3Body_GetPosition(cubes[i]).y, 0.5f, 0.02f));
            b3DestroyBody(cubes[i]);
            REQUIRE(!b3Body_IsValid(cubes[i]));
        }
    }
    return true;
}

void test_box3d()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(box3d_box_falls_rests_and_answers_raycasts);
    RUN_TEST_CASE(box3d_contact_manifold_storage_can_be_reused);
    pu_clear_setup();
}
