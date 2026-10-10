#include "runtime/scenes/physics3d.h"

#include "execution/status.h"
#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/events.h"
#include "runtime/task_pool.h"
#include "runtime/world.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <cute_math3d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 3D physics through its real seams: components in, written-back components
 * and Contact Begin 3D / End 3D events out through game_entities. */
typedef struct
{
    ecs_entity owner;
    uint64_t other;
    bool begin;
    CF_V3 point;
} contact;

static const float STEP = 1.0f / 60;
static const float METER = GAME_UNITS_PER_METER;
static ecs_world *world;
static game_entities *entities;
static game_physics3d *physics;
static contact *contacts; /* The last step's events in dispatch order. */

static int dispatch(void *, ecs_entity owner, const char *event, execution_arguments arguments)
{
    bool begin = game_host_event_is(event, GAME_HOST_EVENT_CONTACT_BEGIN_3D);
    assert(begin || game_host_event_is(event, GAME_HOST_EVENT_CONTACT_END_3D));
    assert(arguments.count == (begin ? 2 : 1));
    contact seen = { owner, arguments.values[0].u64, begin, { 0 } };
    if (begin) {
        seen.point = *(const CF_V3 *)arguments.values[1].ptr;
    }
    apush(contacts, seen);
    return 0;
}

static int destroy(void *, const ecs_entity *doomed, int count)
{
    for (int i = 0; i < count; i++) {
        assert(ecs_world_record_destroy(world, doomed[i], (uint64_t)i) == ECS_OK);
    }
    return ecs_world_apply_commands(world);
}

static int attach(void *, ecs_entity, const char *)
{
    return -1; /* Nothing spawns prototypes here. */
}

static void setup()
{
    world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &GAME_COMPONENT_GameTransform3D, &GAME_COMPONENT_GameGlobalTransform3D,
        &GAME_COMPONENT_GameCollider3D,  &GAME_COMPONENT_GameBody3D,
        &GAME_COMPONENT_GameGravity3D,   &GAME_COMPONENT_GameCharacter3D
    };
    for (int i = 0; i < 6; i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    const game_entity_host host = { .destroy = destroy,
                                    .attach = attach,
                                    .dispatch = dispatch };
    entities = game_entities_new(world, &host);
    physics = game_physics3d_new(world, entities);
}

static void teardown()
{
    game_physics3d_free(physics);
    game_entities_free(entities);
    ecs_world_free(world);
    afree(contacts);
}

static GameCollider3D box(float x, float y, float z)
{
    return (GameCollider3D){ .shape = GAME_COLLIDER3D_BOX,
                             .size = { x, y, z },
                             .category = 1,
                             .mask = 1,
                             .friction = .6f };
}

static GameCollider3D ball(float radius)
{
    return (GameCollider3D){ .shape = GAME_COLLIDER3D_SPHERE,
                             .radius = radius,
                             .category = 1,
                             .mask = 1,
                             .friction = .6f };
}

/* A unit-scale, unrotated collider at position, with a Body3D unless static. */
static ecs_entity collider(ecs_entity parent, CF_V3 position, GameCollider3D shape, GameBodyType type)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameTransform3D_add(world, entity, 0) == ECS_OK);
    assert(GAME_GameCollider3D_add(world, entity, 0) == ECS_OK);
    if (type != GAME_BODY_STATIC) {
        assert(GAME_GameBody3D_add(world, entity, 0) == ECS_OK);
    }
    if (parent.slot) {
        assert(ecs_world_record_add_pair(world, entity, ECS_CHILD_OF, parent, 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameTransform3D_get(world, entity) = (GameTransform3D){
        .position = position,
        .rotation = cf_quat_identity(),
        .scale = { 1, 1, 1 }
    };
    *GAME_GameCollider3D_get(world, entity) = shape;
    if (type != GAME_BODY_STATIC) {
        GAME_GameBody3D_get(world, entity)->type = type;
    }
    return entity;
}

static ecs_entity ground()
{
    return collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, -METER / 2, 0 },
        box(40 * METER, METER, 40 * METER),
        GAME_BODY_STATIC
    );
}

static ecs_entity set_gravity(CF_V3 acceleration)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameGravity3D_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    GAME_GameGravity3D_get(world, entity)->acceleration = acceleration;
    return entity;
}

static int step(float delta)
{
    aclear(contacts);
    int result = game_physics3d_tick(physics, delta);
    if (result < 0) {
        fprintf(stderr, "physics3d %d: %s\n", result, game_physics3d_error(physics));
    }
    assert(result == FLOW_OK);
    do {
        assert(game_entities_dispatch_events(entities) >= 0);
    } while (game_entities_pending_events(entities));
    return acount(contacts);
}

static void steps(int count)
{
    for (int i = 0; i < count; i++) {
        step(STEP);
    }
}

static CF_V3 position(ecs_entity entity)
{
    return GAME_GameTransform3D_get_const(world, entity)->position;
}

static GameBody3D *body(ecs_entity entity)
{
    return GAME_GameBody3D_get(world, entity);
}

static bool near(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static bool near_v3(CF_V3 v, CF_V3 expected, float tolerance)
{
    return near(v.x, expected.x, tolerance) &&
           near(v.y, expected.y, tolerance) && near(v.z, expected.z, tolerance);
}

/* Whether the last step delivered this event, and where for a begin. */
static bool saw(ecs_entity owner, ecs_entity other, bool begin, CF_V3 *point)
{
    for (int i = 0; i < acount(contacts); i++) {
        const contact *c = &contacts[i];
        if (ecs_entity_to_u64(c->owner) == ecs_entity_to_u64(owner) &&
            c->other == ecs_entity_to_u64(other) && c->begin == begin) {
            if (point) {
                *point = c->point;
            }
            return true;
        }
    }
    return false;
}

static bool touched(ecs_entity a, ecs_entity b, bool begin)
{
    return saw(a, b, begin, NULL) && saw(b, a, begin, NULL);
}

static bool failed_with(const char *message)
{
    return game_physics3d_tick(physics, STEP) == FLOW_ERR_ARGUMENT &&
           strstr(game_physics3d_error(physics), message);
}

TEST_CASE(physics3d_box_falls_rests_and_reports_contact)
{
    ecs_entity floor = ground();
    ecs_entity cube = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 4 * METER, 0 },
        box(METER, METER, METER),
        GAME_BODY_DYNAMIC
    );
    REQUIRE(step(STEP) == 0);
    REQUIRE(position(cube).y < 4 * METER && body(cube)->linear_velocity.y < 0);
    REQUIRE(position(floor).y == -METER / 2);
    int landed = 0;
    CF_V3 point = { 0 };
    for (int i = 1; i < 120; i++) {
        step(STEP);
        if (touched(cube, floor, true)) {
            REQUIRE(saw(cube, floor, true, &point));
            landed = i;
        }
        REQUIRE(!touched(cube, floor, false));
    }
    /* About 0.85 s to fall 3.5 m at 9.81 m/s^2; the speculative margin
     * reports the contact a step or so early, at the floor's surface. */
    REQUIRE(landed >= 45 && landed <= 52);
    REQUIRE(near(point.y, 0, METER / 16) && near(point.x, 0, METER / 2));
    REQUIRE(near_v3(position(cube), (CF_V3){ 0, METER / 2, 0 }, METER / 64));
    REQUIRE(cf_len_v3(body(cube)->linear_velocity) < METER / 100);
    CF_Quat rest = GAME_GameTransform3D_get_const(world, cube)->rotation;
    REQUIRE(near(fabsf(rest.w), 1, 1e-4f));
    return true;
}

TEST_CASE(physics3d_shapes_rest_on_their_scaled_extents)
{
    ground();
    ecs_entity sphere = collider(
        (ecs_entity){ 0 },
        (CF_V3){ -4 * METER, METER, 0 },
        ball(METER / 4),
        GAME_BODY_DYNAMIC
    );
    /* A capsule lying along X, rotated a quarter turn about Z. */
    GameCollider3D capsule = { .shape = GAME_COLLIDER3D_CAPSULE,
                               .radius = METER / 4,
                               .height = 2 * METER,
                               .category = 1,
                               .mask = 1,
                               .friction = .6f };
    ecs_entity lying = collider((ecs_entity){ 0 }, (CF_V3){ 0, METER, 0 }, capsule, GAME_BODY_DYNAMIC);
    GAME_GameTransform3D_get(world, lying)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, 1.57079633f);
    ecs_entity scaled = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 4 * METER, 2 * METER, 0 },
        box(METER, METER, METER),
        GAME_BODY_DYNAMIC
    );
    GAME_GameTransform3D_get(world, scaled)->scale = (CF_V3){ 2, 2, 2 };
    steps(120);
    REQUIRE(near(position(sphere).y, METER / 4, METER / 64));
    REQUIRE(near(position(lying).y, METER / 4, METER / 64));
    REQUIRE(near(position(scaled).y, METER, METER / 64));
    /* The written-back rotation keeps the capsule lying along X. */
    CF_V3 axis = cf_mul_q_v3(
        GAME_GameTransform3D_get_const(world, lying)->rotation,
        (CF_V3){ 0, 1, 0 }
    );
    REQUIRE(near(fabsf(axis.x), 1, 1e-3f));
    return true;
}

TEST_CASE(physics3d_gravity_comes_from_the_scene)
{
    ecs_entity scene = set_gravity((CF_V3){ 0 });
    ecs_entity floating = collider((ecs_entity){ 0 }, (CF_V3){ 0, METER, 0 }, ball(METER / 2), GAME_BODY_DYNAMIC);
    steps(60);
    REQUIRE(position(floating).y == METER && position(floating).x == 0);
    /* Changing it wakes the body, asleep after a second at rest. */
    GAME_GameGravity3D_get(world, scene)->acceleration = (CF_V3){ 0, -10 * METER, 0 };
    steps(10);
    REQUIRE(position(floating).y < METER);
    set_gravity((CF_V3){ METER, 0, 0 });
    REQUIRE(failed_with("Only one entity may have Gravity3D; 2 do"));
    return true;
}

TEST_CASE(physics3d_bodies_take_and_return_velocities)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity mover = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), GAME_BODY_KINEMATIC);
    body(mover)->linear_velocity = (CF_V3){ METER, 0, 0 };
    body(mover)->angular_velocity = (CF_V3){ 0, 1, 0 };
    ecs_entity spinner = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 0, 10 * METER },
        box(METER, METER, METER),
        GAME_BODY_DYNAMIC
    );
    body(spinner)->angular_velocity = (CF_V3){ 0, 2, 0 };
    steps(60);
    /* One second later the kinematic body moved a meter and turned a radian. */
    REQUIRE(near_v3(position(mover), (CF_V3){ METER, 0, 0 }, 1e-3f * METER));
    CF_V3 heading = cf_mul_q_v3(
        GAME_GameTransform3D_get_const(world, mover)->rotation,
        (CF_V3){ 1, 0, 0 }
    );
    REQUIRE(near(atan2f(-heading.z, heading.x), 1, 1e-3f));
    REQUIRE(near(body(spinner)->angular_velocity.y, 2, 1e-3f));
    heading = cf_mul_q_v3(
        GAME_GameTransform3D_get_const(world, spinner)->rotation,
        (CF_V3){ 1, 0, 0 }
    );
    REQUIRE(near(atan2f(-heading.z, heading.x), 2, 1e-2f));
    /* A changed velocity applies from the next step. */
    body(mover)->linear_velocity = (CF_V3){ 0, 0, -METER };
    steps(60);
    REQUIRE(near_v3(position(mover), (CF_V3){ METER, 0, -METER }, 1e-3f * METER));
    return true;
}

TEST_CASE(physics3d_moved_transforms_teleport)
{
    ecs_entity floor = ground();
    ecs_entity cube = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, METER / 2, 0 },
        box(METER, METER, METER),
        GAME_BODY_DYNAMIC
    );
    steps(60);
    REQUIRE(near(position(cube).y, METER / 2, METER / 64));
    GAME_GameTransform3D_get(world, cube)->position = (CF_V3){ 3 * METER, 5 * METER, 0 };
    REQUIRE(step(STEP) == 2 && touched(cube, floor, false));
    REQUIRE(near(position(cube).x, 3 * METER, 1e-3f) && position(cube).y < 5 * METER);
    /* Moving the floor out from under a resting body wakes it to fall. */
    steps(120);
    REQUIRE(near(position(cube).y, METER / 2, METER / 64));
    GAME_GameTransform3D_get(world, floor)->position.y = -10 * METER;
    steps(20);
    REQUIRE(position(cube).y < METER / 4);
    return true;
}

TEST_CASE(physics3d_teleports_wake_what_they_land_in)
{
    ground();
    ecs_entity sphere = collider((ecs_entity){ 0 }, (CF_V3){ 0, METER / 2, 0 }, ball(METER / 2), GAME_BODY_DYNAMIC);
    ecs_entity pusher = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 5 * METER, METER / 2, 0 },
        box(METER, METER, METER),
        GAME_BODY_STATIC
    );
    steps(90);
    /* Asleep, the sphere's velocities read exactly zero. */
    REQUIRE(near_v3(body(sphere)->linear_velocity, (CF_V3){ 0 }, 0));
    /* A static box teleported into it pushes it out. */
    GAME_GameTransform3D_get(world, pusher)->position.x = .8f * METER;
    steps(30);
    REQUIRE(position(sphere).x < -.1f * METER);
    return true;
}

TEST_CASE(physics3d_children_follow_their_parent)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity carrier = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), GAME_BODY_KINEMATIC);
    body(carrier)->linear_velocity = (CF_V3){ METER, 0, 0 };
    GameCollider3D zone = box(METER / 2, METER / 2, METER / 2);
    zone.sensor = true;
    ecs_entity rider = collider(carrier, (CF_V3){ 0, 2 * METER, 0 }, zone, GAME_BODY_KINEMATIC);
    /* The rider ignores its own velocity and never writes its local pose. */
    body(rider)->linear_velocity = (CF_V3){ 0, -10 * METER, 0 };
    ecs_entity probe = collider(
        (ecs_entity){ 0 },
        (CF_V3){ METER, 2 * METER, 0 },
        ball(METER / 8),
        GAME_BODY_DYNAMIC
    );
    int met = 0;
    for (int i = 1; i <= 90 && !met; i++) {
        step(STEP);
        met = touched(rider, probe, true) ? i : 0;
    }
    /* The zone's edge reaches the probe's after (1 - 3/8) m at 1 m/s. */
    REQUIRE(met >= 36 && met <= 40);
    REQUIRE(near_v3(position(rider), (CF_V3){ 0, 2 * METER, 0 }, 0));
    REQUIRE(near_v3(body(probe)->linear_velocity, (CF_V3){ 0 }, 0));
    /* Let go, the rider stays where its local transform puts it and takes its
     * own velocity, not the one it had while carried. */
    REQUIRE(ecs_world_record_remove_pair(world, rider, ECS_CHILD_OF, carrier, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    body(rider)->linear_velocity = (CF_V3){ 0 };
    steps(10);
    REQUIRE(near_v3(position(rider), (CF_V3){ 0, 2 * METER, 0 }, 0));
    body(rider)->linear_velocity = (CF_V3){ 0, 0, METER };
    steps(60);
    REQUIRE(near_v3(position(rider), (CF_V3){ 0, 2 * METER, METER }, 1e-3f * METER));
    REQUIRE(ecs_world_record_add_pair(world, rider, ECS_CHILD_OF, carrier, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    steps(2);
    /* Moved to a distant parent, it teleports there within the step. */
    ecs_entity distant = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 30 * METER, 0, 0 },
        ball(METER / 4),
        GAME_BODY_STATIC
    );
    ecs_entity there = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 30 * METER, 2 * METER, METER },
        ball(METER / 8),
        GAME_BODY_DYNAMIC
    );
    REQUIRE(ecs_world_record_remove_pair(world, rider, ECS_CHILD_OF, carrier, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(world, rider, ECS_CHILD_OF, distant, 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(step(STEP) == 2 && touched(rider, there, true));
    /* A dynamic body under a placed parent is an error. */
    body(rider)->type = GAME_BODY_DYNAMIC;
    REQUIRE(failed_with("A dynamic Body3D must be placed in the world"));
    return true;
}

TEST_CASE(physics3d_followers_use_the_parent_rotation)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity turned = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), GAME_BODY_STATIC);
    /* A quarter turn about Y maps local +X to world -Z. */
    GAME_GameTransform3D_get(world, turned)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, 1.57079633f);
    GameCollider3D zone = ball(METER / 2);
    zone.sensor = true;
    ecs_entity arm = collider(turned, (CF_V3){ 4 * METER, 0, 0 }, zone, GAME_BODY_STATIC);
    ecs_entity there = collider((ecs_entity){ 0 }, (CF_V3){ 0, 0, -4 * METER }, ball(METER / 4), GAME_BODY_DYNAMIC);
    ecs_entity unrotated = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 4 * METER, 0, 0 },
        ball(METER / 4),
        GAME_BODY_DYNAMIC
    );
    REQUIRE(step(STEP) == 2 && touched(arm, there, true) && !touched(arm, unrotated, true));
    /* Turning the parent back carries the static child with it. */
    GAME_GameTransform3D_get(world, turned)->rotation = cf_quat_identity();
    REQUIRE(step(STEP) == 4 && touched(arm, there, false) && touched(arm, unrotated, true));
    return true;
}

/* A child of a moving body moves with it within the same step, so it touches
 * what lies where its parent carries it, not only where it started. */
TEST_CASE(physics3d_followers_move_with_their_body)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity parent = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), GAME_BODY_DYNAMIC);
    body(parent)->linear_velocity = (CF_V3){ 30 * METER, 0, 0 };
    GameCollider3D zone = ball(METER / 4);
    zone.sensor = true;
    ecs_entity child = collider(parent, (CF_V3){ METER, 0, 0 }, zone, GAME_BODY_STATIC);
    /* The child starts over 0.75..1.25 m and ends over 1.25..1.75 m; the
     * target covers 1.525..1.775 m. */
    ecs_entity target = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 1.65f * METER, 0, 0 },
        ball(METER / 8),
        GAME_BODY_DYNAMIC
    );
    REQUIRE(step(1.0f / 60) == 2 && touched(child, target, true));
    REQUIRE(near_v3(position(parent), (CF_V3){ METER / 2, 0, 0 }, 1e-3f * METER));
    REQUIRE(near_v3(position(child), (CF_V3){ METER, 0, 0 }, 0));
    /* A spinning parent carries its child along the arc's tangent: turning
     * 0.3 rad about Y in a step moves local +X toward -Z. */
    CF_V3 hub = { 0, 0, 10 * METER };
    ecs_entity spinner = collider((ecs_entity){ 0 }, hub, ball(METER / 4), GAME_BODY_KINEMATIC);
    body(spinner)->angular_velocity = (CF_V3){ 0, 18, 0 };
    ecs_entity arm = collider(spinner, (CF_V3){ METER, 0, 0 }, zone, GAME_BODY_STATIC);
    ecs_entity ahead = collider(
        (ecs_entity){ 0 },
        cf_add_v3(hub, (CF_V3){ .93f * METER, 0, -.55f * METER }),
        ball(METER / 8),
        GAME_BODY_DYNAMIC
    );
    step(1.0f / 60);
    REQUIRE(touched(arm, ahead, true));
    return true;
}

/* A child collider is part of its body: it holds the body up, reports its own
 * hits, and what rests on it falls asleep with the body. */
TEST_CASE(physics3d_followers_are_part_of_their_body)
{
    ecs_entity floor = ground();
    ecs_entity base = collider((ecs_entity){ 0 }, (CF_V3){ 0, 3 * METER, 0 }, ball(METER / 4), GAME_BODY_DYNAMIC);
    ecs_entity plate = collider(
        base,
        (CF_V3){ 0, -METER, 0 },
        box(2 * METER, METER / 2, 2 * METER),
        GAME_BODY_STATIC
    );
    ecs_entity crate = collider(
        (ecs_entity){ 0 },
        (CF_V3){ METER / 2, 3 * METER, 0 },
        box(METER / 2, METER / 2, METER / 2),
        GAME_BODY_DYNAMIC
    );
    bool landed = false;
    for (int i = 0; i < 240; i++) {
        step(STEP);
        landed = landed || touched(plate, floor, true);
        REQUIRE(!touched(base, floor, true));
    }
    REQUIRE(landed);
    /* The plate's bottom rests on the floor 1.25 m below the ball's center. */
    REQUIRE(near(position(base).y, 1.25f * METER, 1e-2f * METER));
    REQUIRE(near(position(crate).y, .75f * METER, 1e-2f * METER));
    REQUIRE(near_v3(body(base)->linear_velocity, (CF_V3){ 0 }, 0));
    REQUIRE(near_v3(body(crate)->linear_velocity, (CF_V3){ 0 }, 0));
    return true;
}

/* A child goes where the solver takes its body within the step, even when the
 * body speeds up or is stopped. */
TEST_CASE(physics3d_followers_move_as_their_body_is_solved)
{
    /* Pushed from rest at 600 m/s^2, the body covers 1/6 m in its first step,
     * which carries the nose's edge from 1.25 m past the mark's at 1.325 m. */
    ecs_entity push = set_gravity((CF_V3){ 600 * METER, 0, 0 });
    ecs_entity rocket = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), GAME_BODY_DYNAMIC);
    GameCollider3D zone = ball(METER / 4);
    zone.sensor = true;
    ecs_entity nose = collider(rocket, (CF_V3){ METER, 0, 0 }, zone, GAME_BODY_STATIC);
    ecs_entity mark = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 1.45f * METER, 0, 0 },
        ball(METER / 8),
        GAME_BODY_STATIC
    );
    step(STEP);
    REQUIRE(touched(nose, mark, true));
    /* A body that would cover 1/2 m stops at a wall after 1/4 m, so its flag
     * stops short of the mark beyond it. */
    GAME_GameGravity3D_get(world, push)->acceleration = (CF_V3){ 0 };
    CF_V3 at = { 0, 0, 20 * METER };
    ecs_entity ram = collider((ecs_entity){ 0 }, at, ball(METER / 4), GAME_BODY_DYNAMIC);
    body(ram)->linear_velocity = (CF_V3){ 30 * METER, 0, 0 };
    ecs_entity flag = collider(ram, (CF_V3){ 0, METER, 0 }, zone, GAME_BODY_STATIC);
    collider((ecs_entity){ 0 }, cf_add_v3(at, (CF_V3){ METER, 0, 0 }), box(METER, METER, METER), GAME_BODY_STATIC);
    ecs_entity beyond = collider(
        (ecs_entity){ 0 },
        cf_add_v3(at, (CF_V3){ .7f * METER, METER, 0 }),
        ball(METER / 8),
        GAME_BODY_STATIC
    );
    for (int i = 0; i < 4; i++) {
        step(STEP);
        REQUIRE(!touched(flag, beyond, true));
    }
    REQUIRE(near(position(ram).x, METER / 4, METER / 64));
    return true;
}

/* A follower carried into a sleeping body wakes it and pushes it, as Box3D
 * wakes what a moving kinematic body starts touching. */
TEST_CASE(physics3d_moving_followers_wake_what_they_meet)
{
    ground();
    ecs_entity sphere = collider((ecs_entity){ 0 }, (CF_V3){ 0, METER / 2, 0 }, ball(METER / 2), GAME_BODY_DYNAMIC);
    steps(90);
    REQUIRE(near_v3(body(sphere)->linear_velocity, (CF_V3){ 0 }, 0));
    ecs_entity carrier = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 5 * METER, 3 * METER, 0 },
        ball(METER / 4),
        GAME_BODY_KINEMATIC
    );
    body(carrier)->linear_velocity = (CF_V3){ -3 * METER, 0, 0 };
    collider(carrier, (CF_V3){ 0, -2.5f * METER, 0 }, box(METER, METER, METER), GAME_BODY_STATIC);
    steps(120);
    REQUIRE(position(sphere).x < -METER);
    return true;
}

/* A body and the colliders it carries move as one, so they never touch. */
TEST_CASE(physics3d_followers_never_touch_their_body)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity ship = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, box(METER, METER, METER), GAME_BODY_DYNAMIC);
    collider(ship, (CF_V3){ METER / 2, 0, 0 }, box(METER, METER, METER), GAME_BODY_STATIC);
    GameCollider3D zone = ball(METER);
    zone.sensor = true;
    collider(ship, (CF_V3){ 0 }, zone, GAME_BODY_KINEMATIC);
    for (int i = 0; i < 4; i++) {
        REQUIRE(step(STEP) == 0);
    }
    REQUIRE(near_v3(position(ship), (CF_V3){ 0 }, 0));
    REQUIRE(near_v3(body(ship)->linear_velocity, (CF_V3){ 0 }, 0));
    return true;
}

/* A collider handed to the body it touches stops touching it, rather than
 * pushing it along. */
TEST_CASE(physics3d_followers_stop_touching_the_body_they_join)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity player = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, box(METER, METER, METER), GAME_BODY_DYNAMIC);
    ecs_entity table = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 3 * METER, 0, 0 },
        box(METER, METER, METER),
        GAME_BODY_STATIC
    );
    ecs_entity item = collider(table, (CF_V3){ -2 * METER, 0, 0 }, box(METER, METER, METER), GAME_BODY_STATIC);
    REQUIRE(step(STEP) == 2 && touched(player, item, true));
    REQUIRE(ecs_world_record_add_pair(world, item, ECS_CHILD_OF, player, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    GAME_GameTransform3D_get(world, item)->position = (CF_V3){ METER / 2, 0, 0 };
    REQUIRE(step(STEP) == 2 && touched(player, item, false));
    steps(4);
    REQUIRE(near_v3(position(player), (CF_V3){ 0 }, 0));
    REQUIRE(near_v3(body(player)->linear_velocity, (CF_V3){ 0 }, 0));
    return true;
}

TEST_CASE(physics3d_contacts_end_when_bodies_part_or_go)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity left = collider((ecs_entity){ 0 }, (CF_V3){ -METER, 0, 0 }, ball(METER / 2), GAME_BODY_DYNAMIC);
    ecs_entity right = collider((ecs_entity){ 0 }, (CF_V3){ METER, 0, 0 }, ball(METER / 2), GAME_BODY_DYNAMIC);
    body(left)->linear_velocity = (CF_V3){ METER, 0, 0 };
    body(right)->linear_velocity = (CF_V3){ -METER, 0, 0 };
    int met = 0;
    CF_V3 point = { 0 };
    for (int i = 1; i <= 60 && !met; i++) {
        step(STEP);
        met = touched(left, right, true) ? i : 0;
    }
    REQUIRE(met && saw(left, right, true, &point) && near_v3(point, (CF_V3){ 0 }, METER / 16));
    /* Events come in pair order: the lower handle first. */
    REQUIRE(
        acount(contacts) == 2 &&
        ecs_entity_to_u64(contacts[0].owner) == ecs_entity_to_u64(left)
    );
    body(left)->linear_velocity = (CF_V3){ -2 * METER, 0, 0 };
    body(right)->linear_velocity = (CF_V3){ 2 * METER, 0, 0 };
    int parted = 0;
    for (int i = 1; i <= 30 && !parted; i++) {
        step(STEP);
        parted = touched(left, right, false) ? i : 0;
    }
    REQUIRE(parted);
    /* Destroying one of a touching pair ends the contact for the other. */
    ecs_entity rest = collider((ecs_entity){ 0 }, (CF_V3){ 0, 3 * METER, 0 }, ball(METER / 2), GAME_BODY_DYNAMIC);
    ecs_entity gone = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 3 * METER + .99f * METER, 0 },
        ball(METER / 2),
        GAME_BODY_DYNAMIC
    );
    REQUIRE(step(STEP) == 2 && touched(rest, gone, true));
    game_entities_request_destroy(entities, gone);
    REQUIRE(game_entities_flush(entities) == 0);
    REQUIRE(step(STEP) == 1 && saw(rest, gone, false, NULL));
    return true;
}

TEST_CASE(physics3d_masks_and_sensors_filter_contacts)
{
    set_gravity((CF_V3){ 0 });
    GameCollider3D zone = box(2 * METER, 2 * METER, 2 * METER);
    zone.sensor = true;
    ecs_entity trigger = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, zone, GAME_BODY_STATIC);
    /* Two lanes through the sensor; a wall blocks only the ghost's. */
    ecs_entity runner = collider(
        (ecs_entity){ 0 },
        (CF_V3){ -2 * METER, 0, -METER / 2 },
        ball(METER / 4),
        GAME_BODY_DYNAMIC
    );
    GameCollider3D unmasked = ball(METER / 4);
    unmasked.mask = 0;
    ecs_entity ghost = collider((ecs_entity){ 0 }, (CF_V3){ -2 * METER, 0, METER / 2 }, unmasked, GAME_BODY_DYNAMIC);
    ecs_entity wall = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 0, METER / 2 },
        box(METER / 2, 2 * METER, METER / 2),
        GAME_BODY_STATIC
    );
    body(runner)->linear_velocity = (CF_V3){ 2 * METER, 0, 0 };
    body(ghost)->linear_velocity = (CF_V3){ 2 * METER, 0, 0 };
    bool entered = false, left = false;
    for (int i = 0; i < 120; i++) {
        step(STEP);
        entered |= touched(trigger, runner, true);
        left |= touched(trigger, runner, false);
        REQUIRE(!touched(ghost, wall, true) && !touched(ghost, trigger, true));
    }
    /* The sensor reports both edges without slowing the runner, and the
     * maskless ghost passed through the wall. */
    REQUIRE(entered && left);
    REQUIRE(near(body(runner)->linear_velocity.x, 2 * METER, 1e-3f));
    REQUIRE(position(ghost).x > METER);
    return true;
}

TEST_CASE(physics3d_interpolates_moving_roots_only)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity mover = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), GAME_BODY_DYNAMIC);
    body(mover)->linear_velocity = (CF_V3){ 60, 0, 0 };
    body(mover)->angular_velocity = (CF_V3){ 0, 30, 0 };
    ecs_entity rider = collider(mover, (CF_V3){ 0, METER, 0 }, ball(METER / 4), GAME_BODY_KINEMATIC);
    ecs_entity fixed = collider((ecs_entity){ 0 }, (CF_V3){ 0, 0, 5 * METER }, ball(METER / 4), GAME_BODY_STATIC);
    step(STEP);
    CF_Quat rotation;
    CF_V3 translation;
    REQUIRE(game_physics3d_interpolation(physics, mover, .5f, &rotation, &translation));
    /* Halfway back along this step's motion. */
    CF_V3 solved = position(mover);
    CF_V3 shown = cf_add_v3(cf_mul_q_v3(rotation, solved), translation);
    REQUIRE(near(solved.x, 1, 1e-3f) && near_v3(shown, (CF_V3){ .5f, 0, 0 }, 1e-3f));
    /* Box3D limits rotation to an eighth of a turn per step; this turns half a radian. */
    CF_Quat end = GAME_GameTransform3D_get_const(world, mover)->rotation;
    CF_Quat shown_rotation = cf_mul_q(rotation, end);
    REQUIRE(near(2 * asinf(end.y), .5f, 1e-2f));
    REQUIRE(
        near(shown_rotation.y, sinf(asinf(end.y) / 2), 1e-4f) && shown_rotation.x == 0
    );
    REQUIRE(!game_physics3d_interpolation(physics, rider, .5f, &rotation, &translation));
    REQUIRE(!game_physics3d_interpolation(physics, fixed, .5f, &rotation, &translation));
    /* A pose moved after the step shows as it is. */
    GAME_GameTransform3D_get(world, mover)->position.x = 9;
    REQUIRE(game_physics3d_interpolation(physics, mover, .5f, &rotation, &translation));
    REQUIRE(rotation.w == 1 && translation.x == 0);
    /* Its body outlives a removed Transform3D until the next step. */
    REQUIRE(GAME_GameTransform3D_remove(world, mover, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!game_physics3d_interpolation(physics, mover, .5f, &rotation, &translation));
    return true;
}

TEST_CASE(physics3d_rejects_invalid_colliders)
{
    ecs_entity entity = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, box(METER, METER, METER), GAME_BODY_DYNAMIC);
    char expected[64];
    snprintf(expected, sizeof expected, "on entity %u.", entity.slot);
    GAME_GameTransform3D_get(world, entity)->scale = (CF_V3){ 0 };
    REQUIRE(failed_with("nonzero Transform3D scale") && failed_with(expected));
    GAME_GameTransform3D_get(world, entity)->scale = (CF_V3){ 1, 1, 1 };
    GAME_GameCollider3D_get(world, entity)->size.y = -1;
    REQUIRE(failed_with("box needs a positive size"));
    *GAME_GameCollider3D_get(world, entity) = (GameCollider3D){ .shape = GAME_COLLIDER3D_CAPSULE,
                                                                .radius = METER,
                                                                .height = METER };
    REQUIRE(failed_with("height of at least twice"));
    GAME_GameCollider3D_get(world, entity)->height = 2 * METER;
    GAME_GameCollider3D_get(world, entity)->friction = -1;
    REQUIRE(failed_with("friction and restitution"));
    GAME_GameCollider3D_get(world, entity)->friction = 0;
    GAME_GameTransform3D_get(world, entity)->position.x = NAN;
    REQUIRE(failed_with("finite Transform3D"));
    GAME_GameTransform3D_get(world, entity)->position.x = 0;
    body(entity)->linear_velocity.z = INFINITY;
    REQUIRE(failed_with("velocities must be finite"));
    body(entity)->linear_velocity.z = 0;
    REQUIRE(step(STEP) == 0);
    return true;
}

enum
{
    PILE = 288 /* Above the parallel step's body minimum. */
};

/* Drops a pile of boxes onto the ground with the given workers and records
 * where they end up and how many contact events they raised. */
static int pile(int workers, CF_V3 *out)
{
    game_physics_settings settings = game_physics_defaults();
    settings.workers = workers;
    assert(game_physics3d_configure(physics, settings) == FLOW_OK);
    ground();
    ecs_entity boxes[PILE];
    for (int i = 0; i < PILE; i++) {
        CF_V3 at = { (float)(i % 16 - 8) * 1.5f * METER,
                     (float)(i / 16) * 1.1f * METER + METER,
                     (float)(i % 5) * .2f * METER };
        boxes[i] = collider((ecs_entity){ 0 }, at, box(METER, METER, METER), GAME_BODY_DYNAMIC);
    }
    int events = 0;
    for (int i = 0; i < 60; i++) {
        events += step(STEP);
    }
    for (int i = 0; i < PILE; i++) {
        out[i] = position(boxes[i]);
    }
    return events;
}

TEST_CASE(physics3d_parallel_steps_match_serial)
{
    static CF_V3 serial[PILE], parallel[PILE];
    int serial_events = pile(1, serial);
    teardown();
    setup();
    REQUIRE(pile(4, parallel) == serial_events && serial_events > PILE);
    REQUIRE(!memcmp(serial, parallel, sizeof serial));
    /* Hosts with workers ran the pile on them. */
    game_task_pool *pool = game_task_pool_acquire_workers(4);
    bool ran = game_task_pool_tasks(pool)->workers == 1 ||
               game_task_pool_started(pool);
    game_task_pool_release(pool);
    REQUIRE(ran);
    return true;
}

TEST_CASE(physics3d_runs_as_a_world_module)
{
    char error[256];
    game_world_options options = { .modules = GAME_WORLD_PHYSICS3D };
    game_world *simulation = game_world_new(&options, error, sizeof error);
    REQUIRE(simulation);
    ecs_world *ecs = game_world_ecs(simulation);
    engine *runtime = game_world_engine(simulation);
    ecs_entity entity = ecs_world_create_entity(ecs);
    REQUIRE(GAME_GameTransform3D_add(ecs, entity, 0) == ECS_OK);
    REQUIRE(GAME_GameCollider3D_add(ecs, entity, 0) == ECS_OK);
    REQUIRE(GAME_GameBody3D_add(ecs, entity, 0) == ECS_OK);
    REQUIRE(GAME_GameGravity3D_add(ecs, entity, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    *GAME_GameTransform3D_get(ecs, entity) = (GameTransform3D){ .scale = { 1, 1, 1 } };
    *GAME_GameCollider3D_get(ecs, entity) = ball(1);
    *GAME_GameBody3D_get(ecs, entity) = (GameBody3D){ .type = GAME_BODY_DYNAMIC,
                                                      .linear_velocity = { 60, 0, 0 } };
    REQUIRE(engine_set_playing(runtime, true) == 0);
    REQUIRE(engine_frame(runtime, 0) == FLOW_OK);
    REQUIRE(engine_frame(runtime, .1) == FLOW_OK);
    float solved = GAME_GameTransform3D_get_const(ecs, entity)->position.x;
    REQUIRE(near(solved, 60 * engine_last_timing(runtime)->simulation_time, 1e-3f));
    REQUIRE(GAME_GameGlobalTransform3D_get_const(ecs, entity)->position.x == solved);
    REQUIRE(engine_frame(runtime, .1 + ENGINE_FIXED_SECONDS * .5) == FLOW_OK);
    GameGlobalTransform3D shown = engine_render_transform_3d(runtime, ecs, entity);
    REQUIRE(near(shown.position.x, solved - 60 * ENGINE_FIXED_SECONDS * .5f, 1e-3f));
    /* A scene reset drops the bodies; the entity's next step recreates its own. */
    engine_reset_scene(runtime);
    REQUIRE(
        near_v3(engine_render_transform_3d(runtime, ecs, entity).position, (CF_V3){ solved, 0, 0 }, 0)
    );
    REQUIRE(engine_frame(runtime, .3) == FLOW_OK);
    REQUIRE(GAME_GameTransform3D_get_const(ecs, entity)->position.x > solved);
    game_world_free(simulation);
    return true;
}

void test_native_catalog_enable();
static bool is(ecs_entity a, ecs_entity b)
{
    return ecs_entity_to_u64(a) == ecs_entity_to_u64(b);
}

static game_physics3d_hit ray(CF_V3 start, CF_V3 end, unsigned categories, ecs_entity exclude, bool sensors)
{
    game_physics3d_hit hit;
    int result = game_physics3d_raycast(world, entities, start, end, categories, exclude, sensors, &hit);
    assert(result == FLOW_OK);
    return hit;
}

static game_physics3d_hit sweep(ecs_entity caster, CF_V3 translation, bool sensors)
{
    game_physics3d_hit hit;
    int result = game_physics3d_shape_cast(world, entities, caster, translation, ~0u, sensors, &hit);
    assert(result == FLOW_OK);
    return hit;
}

static bool hit_at(game_physics3d_hit hit, ecs_entity entity, CF_V3 point, CF_V3 normal, float distance)
{
    const float tolerance = METER / 100;
    return hit.found && is(hit.entity, entity) &&
           near_v3(hit.point, point, tolerance) &&
           near_v3(hit.normal, normal, 1e-3f) &&
           near(hit.distance, distance, tolerance);
}

/* Rays see colliders as their components place them now, before any step. */
TEST_CASE(physics3d_raycasts_find_the_nearest_collider)
{
    ecs_entity floor = ground();
    ecs_entity rock = collider((ecs_entity){ 0 }, (CF_V3){ 0, 2 * METER, 0 }, ball(METER / 2), GAME_BODY_STATIC);
    GameCollider3D zone = ball(METER / 2);
    zone.sensor = true;
    ecs_entity mist = collider((ecs_entity){ 0 }, (CF_V3){ 0, 4 * METER, 0 }, zone, GAME_BODY_STATIC);
    CF_V3 top = { 0, 10 * METER, 0 }, bottom = { 0, -METER, 0 }, up = { 0, 1, 0 };
    REQUIRE(hit_at(
        ray(top, bottom, ~0u, (ecs_entity){ 0 }, false),
        rock,
        (CF_V3){ 0, 2.5f * METER, 0 },
        up,
        7.5f * METER
    ));
    REQUIRE(hit_at(
        ray(top, bottom, ~0u, (ecs_entity){ 0 }, true),
        mist,
        (CF_V3){ 0, 4.5f * METER, 0 },
        up,
        5.5f * METER
    ));
    REQUIRE(hit_at(ray(top, bottom, ~0u, rock, false), floor, (CF_V3){ 0 }, up, 10 * METER));
    REQUIRE(!ray(top, bottom, 2, (ecs_entity){ 0 }, true).found);
    /* A ray from inside hits at its start without a normal. */
    REQUIRE(hit_at(
        ray((CF_V3){ 0, 2 * METER, 0 }, top, ~0u, (ecs_entity){ 0 }, false),
        rock,
        (CF_V3){ 0, 2 * METER, 0 },
        (CF_V3){ 0 },
        0
    ));
    /* A long thin box turned a quarter about Y and doubled lies across X. */
    GameCollider3D plank = box(METER / 2, METER / 2, 2 * METER);
    plank.category = 2;
    ecs_entity beam = collider((ecs_entity){ 0 }, (CF_V3){ 6 * METER, METER, 0 }, plank, GAME_BODY_STATIC);
    GameTransform3D *placed = GAME_GameTransform3D_get(world, beam);
    placed->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, 1.57079633f);
    placed->scale = (CF_V3){ 2, 2, 2 };
    REQUIRE(hit_at(
        ray((CF_V3){ 0, METER, 0 }, (CF_V3){ 10 * METER, METER, 0 }, 2, (ecs_entity){ 0 }, false),
        beam,
        (CF_V3){ 4 * METER, METER, 0 },
        (CF_V3){ -1, 0, 0 },
        4 * METER
    ));
    /* Excluding a child excludes the body that carries it. */
    ecs_entity cart = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, METER, 5 * METER },
        box(METER, METER, METER),
        GAME_BODY_KINEMATIC
    );
    ecs_entity mast = collider(cart, (CF_V3){ 0, METER, 0 }, box(METER / 4, METER, METER / 4), GAME_BODY_STATIC);
    CF_V3 above = { 0, 10 * METER, 5 * METER }, below = { 0, -METER, 5 * METER };
    REQUIRE(is(ray(above, below, ~0u, (ecs_entity){ 0 }, false).entity, mast));
    REQUIRE(is(ray(above, below, ~0u, mast, false).entity, floor));
    REQUIRE(is(ray(above, below, ~0u, cart, false).entity, floor));
    game_physics3d_hit hit;
    REQUIRE(
        game_physics3d_raycast(world, entities, (CF_V3){ 0, 1e9f, 0 }, bottom, ~0u, (ecs_entity){ 0 }, false, &hit) ==
        FLOW_ERR_ARGUMENT
    );
    GAME_GameCollider3D_get(world, mist)->radius = 0;
    REQUIRE(
        game_physics3d_raycast(world, entities, top, bottom, ~0u, (ecs_entity){ 0 }, false, &hit) ==
        FLOW_ERR_ARGUMENT
    );
    REQUIRE(!hit.found);
    return true;
}

TEST_CASE(physics3d_shape_casts_sweep_a_collider)
{
    ecs_entity floor = ground();
    ecs_entity drop = collider((ecs_entity){ 0 }, (CF_V3){ 0, 5 * METER, 0 }, ball(METER / 2), GAME_BODY_DYNAMIC);
    /* Its own child below it is part of its body, not in its way. */
    collider(drop, (CF_V3){ 0, -2 * METER, 0 }, box(METER, METER / 4, METER), GAME_BODY_STATIC);
    REQUIRE(hit_at(
        sweep(drop, (CF_V3){ 0, -10 * METER, 0 }, false),
        floor,
        (CF_V3){ 0 },
        (CF_V3){ 0, 1, 0 },
        4.5f * METER
    ));
    REQUIRE(!sweep(drop, (CF_V3){ 0, -4 * METER, 0 }, false).found);
    /* A capsule turned a quarter about Z lies along X, 2 m from end to end. */
    GameCollider3D log = { .shape = GAME_COLLIDER3D_CAPSULE,
                           .radius = METER / 4,
                           .height = 2 * METER,
                           .category = 1,
                           .mask = 1 };
    ecs_entity roller = collider((ecs_entity){ 0 }, (CF_V3){ 0, 2 * METER, 10 * METER }, log, GAME_BODY_DYNAMIC);
    GAME_GameTransform3D_get(world, roller)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, 1.57079633f);
    ecs_entity wall = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 5 * METER, 2 * METER, 10 * METER },
        box(METER, 4 * METER, 4 * METER),
        GAME_BODY_STATIC
    );
    GameCollider3D zone = box(METER, 4 * METER, 4 * METER);
    zone.sensor = true;
    ecs_entity gate = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 3 * METER, 2 * METER, 10 * METER },
        zone,
        GAME_BODY_STATIC
    );
    CF_V3 across = { 10 * METER, 0, 0 };
    REQUIRE(hit_at(
        sweep(roller, across, false),
        wall,
        (CF_V3){ 4.5f * METER, 2 * METER, 10 * METER },
        (CF_V3){ -1, 0, 0 },
        3.5f * METER
    ));
    REQUIRE(is(sweep(roller, across, true).entity, gate));
    /* What it already overlaps, it hits at once, without a normal. */
    GAME_GameTransform3D_get(world, roller)->position.x = 5 * METER;
    game_physics3d_hit stuck = sweep(roller, across, false);
    REQUIRE(stuck.found && is(stuck.entity, wall) && stuck.distance == 0);
    REQUIRE(near_v3(stuck.normal, (CF_V3){ 0 }, 0));
    ecs_entity gone = ecs_world_create_entity(world);
    REQUIRE(!sweep(gone, across, false).found);
    return true;
}

/* Contact queries answer what the simulation would let touch: a resting
 * crate touches its floor, a near solid counts within Box3D's margin, a
 * sensor needs overlap, and masks and bodies apply. */
TEST_CASE(physics3d_contact_queries_match_what_touches)
{
    ecs_entity floor = ground();
    ecs_entity crate = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, METER, 0 },
        box(METER, METER, METER),
        GAME_BODY_DYNAMIC
    );
    ecs_entity handle = collider(
        crate,
        (CF_V3){ 0, -METER / 4, 0 },
        box(METER / 4, METER / 2, METER / 4),
        GAME_BODY_STATIC
    );
    bool landed = false;
    for (int i = 0; i < 240; i++) {
        step(STEP);
        landed = landed || touched(crate, floor, true) || touched(handle, floor, true);
    }
    REQUIRE(landed && near_v3(body(crate)->linear_velocity, (CF_V3){ 0 }, 0));
    ecs_entity *found = NULL;
    REQUIRE(game_physics3d_contact_entities(world, entities, crate, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 1 && is(found[0], floor));
    REQUIRE(game_physics3d_contact_entities(world, entities, floor, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 2 && is(found[0], crate) && is(found[1], handle));
    REQUIRE(game_physics3d_contact_entities(world, entities, floor, 2, &found) == FLOW_OK);
    REQUIRE(acount(found) == 0);
    /* A hundredth of a meter above a solid is within the margin; a sensor
     * there touches nothing, and neither does a solid it can't collide with. */
    float hover = METER / 2 + METER / 100;
    ecs_entity near_ball = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 5 * METER, hover, 0 },
        ball(METER / 2),
        GAME_BODY_STATIC
    );
    GameCollider3D zone = ball(METER / 2);
    zone.sensor = true;
    ecs_entity near_zone = collider((ecs_entity){ 0 }, (CF_V3){ 10 * METER, hover, 0 }, zone, GAME_BODY_STATIC);
    GameCollider3D ghost = ball(METER / 2);
    ghost.mask = 2;
    ecs_entity sunk = collider((ecs_entity){ 0 }, (CF_V3){ -5 * METER, 0, 0 }, ghost, GAME_BODY_STATIC);
    REQUIRE(game_physics3d_contact_entities(world, entities, near_ball, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 1 && is(found[0], floor));
    REQUIRE(game_physics3d_contact_entities(world, entities, near_zone, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 0);
    GAME_GameTransform3D_get(world, near_zone)->position.y = METER / 4;
    REQUIRE(game_physics3d_contact_entities(world, entities, near_zone, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 1 && is(found[0], floor));
    REQUIRE(game_physics3d_contact_entities(world, entities, sunk, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 0);
    REQUIRE(game_physics3d_contact_entities(world, entities, crate, 0, &found) == FLOW_OK);
    REQUIRE(acount(found) == 0);
    afree(found);
    return true;
}

static const float SLOP = .005f * METER; /* Box3D's linear slop. */

/* A kinematic capsule character, 1.8 m tall and .4 m in radius. */
static ecs_entity character(CF_V3 position)
{
    GameCollider3D capsule = { .shape = GAME_COLLIDER3D_CAPSULE,
                               .radius = .4f * METER,
                               .height = 1.8f * METER,
                               .category = 1,
                               .mask = 1 };
    ecs_entity entity = collider((ecs_entity){ 0 }, position, capsule, GAME_BODY_KINEMATIC);
    assert(GAME_GameCharacter3D_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

/* Steps a character at velocity, adding gravity as a script would when it
 * falls. */
static void walk(ecs_entity walker, CF_V3 velocity, bool falls, int count)
{
    for (int i = 0; i < count; i++) {
        GameBody3D *moving = body(walker);
        float fall = falls ? moving->linear_velocity.y - 9.81f * METER * STEP
                           : velocity.y;
        moving->linear_velocity = cf_v3(velocity.x, fall, velocity.z);
        step(STEP);
    }
}

static const GameCharacter3D *character_state(ecs_entity walker)
{
    return GAME_GameCharacter3D_get_const(world, walker);
}

/* A character lands, stands without gathering speed, stops at a wall and
 * slides along it, all with the velocity it can actually keep. */
TEST_CASE(physics3d_characters_slide_along_what_they_meet)
{
    ground();
    collider(
        (ecs_entity){ 0 },
        (CF_V3){ 3 * METER, 2 * METER, 0 },
        box(METER, 4 * METER, 40 * METER),
        GAME_BODY_STATIC
    );
    ecs_entity walker = character((CF_V3){ 0, METER, 0 });
    walk(walker, (CF_V3){ 2 * METER, 0, 2 * METER }, true, 120);
    REQUIRE(character_state(walker)->on_floor);
    REQUIRE(near_v3(character_state(walker)->floor_normal, (CF_V3){ 0, 1, 0 }, 1e-3f));
    CF_V3 at = position(walker);
    REQUIRE(near(at.x, 2.1f * METER, 2 * SLOP) && near(at.y, .9f * METER, 2 * SLOP));
    REQUIRE(near(at.z, 4 * METER, METER / 10));
    REQUIRE(near_v3(body(walker)->linear_velocity, (CF_V3){ 0, 0, 2 * METER }, 1e-3f));
    /* It leaves the floor when it jumps and lands again. */
    walk(walker, (CF_V3){ 0, 5 * METER, 0 }, false, 1);
    REQUIRE(!character_state(walker)->on_floor && position(walker).y > at.y);
    walk(walker, (CF_V3){ 0 }, true, 90);
    REQUIRE(character_state(walker)->on_floor && near(position(walker).y, at.y, 2 * SLOP));
    return true;
}

/* A wall is not a floor, sensors and its own colliders never block it, and
 * it stops at a dynamic body as at a wall. Up opposes the scene's gravity. */
TEST_CASE(physics3d_characters_meet_only_solid_others)
{
    ground();
    ecs_entity sky = set_gravity((CF_V3){ 0, 0, 0 });
    collider(
        (ecs_entity){ 0 },
        (CF_V3){ 3 * METER, 5 * METER, 0 },
        box(METER, 4 * METER, 40 * METER),
        GAME_BODY_STATIC
    );
    ecs_entity floater = character((CF_V3){ 0, 5 * METER, 0 });
    walk(floater, (CF_V3){ 2 * METER, 0, 0 }, false, 120);
    REQUIRE(near(position(floater).x, 2.1f * METER, 2 * SLOP));
    REQUIRE(!character_state(floater)->on_floor);
    GameCollider3D zone = box(METER, 4 * METER, 4 * METER);
    zone.sensor = true;
    collider((ecs_entity){ 0 }, (CF_V3){ 0, 2 * METER, 3 * METER }, zone, GAME_BODY_STATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    collider(walker, (CF_V3){ 0, 0, -.6f * METER }, box(METER / 2, METER / 2, METER / 2), GAME_BODY_STATIC);
    ecs_entity crate = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, METER / 2, 8 * METER },
        box(METER, METER, METER),
        GAME_BODY_DYNAMIC
    );
    walk(walker, (CF_V3){ 0, 0, 2 * METER }, false, 240);
    REQUIRE(near(position(walker).z, 7.1f * METER, 2 * SLOP));
    REQUIRE(near(position(crate).z, 8 * METER, SLOP));
    /* Sideways gravity turns the wall into a floor. */
    GAME_GameGravity3D_get(world, sky)->acceleration = (CF_V3){ 9.81f * METER, 0, 0 };
    walk(floater, (CF_V3){ METER, 0, 0 }, false, 1);
    REQUIRE(character_state(floater)->on_floor);
    REQUIRE(near_v3(character_state(floater)->floor_normal, (CF_V3){ -1, 0, 0 }, 1e-3f));
    return true;
}

TEST_CASE(physics3d_characters_need_a_kinematic_capsule)
{
    ecs_entity walker = character((CF_V3){ 0, METER, 0 });
    step(STEP);
    body(walker)->type = GAME_BODY_DYNAMIC;
    REQUIRE(failed_with("A Character3D needs a kinematic Body3D"));
    body(walker)->type = GAME_BODY_KINEMATIC;
    GAME_GameCollider3D_get(world, walker)->sensor = true;
    REQUIRE(failed_with("A Character3D needs a solid capsule Collider3D"));
    *GAME_GameCollider3D_get(world, walker) = box(METER, METER, METER);
    REQUIRE(failed_with("A Character3D needs a solid capsule Collider3D"));
    GameCollider3D thin = { .shape = GAME_COLLIDER3D_CAPSULE,
                            .radius = METER / 100,
                            .height = METER };
    *GAME_GameCollider3D_get(world, walker) = thin;
    REQUIRE(failed_with("at least two centimeters in radius"));
    thin.radius = METER / 50;
    *GAME_GameCollider3D_get(world, walker) = thin;
    REQUIRE(game_physics3d_tick(physics, STEP) == FLOW_OK);
    return true;
}

void test_tool_physics3d()
{
    test_native_catalog_enable(); /* Contact payloads copy a CF_V3. */
    pu_setup(setup, teardown);
    RUN_TEST_CASE(physics3d_box_falls_rests_and_reports_contact);
    RUN_TEST_CASE(physics3d_shapes_rest_on_their_scaled_extents);
    RUN_TEST_CASE(physics3d_gravity_comes_from_the_scene);
    RUN_TEST_CASE(physics3d_bodies_take_and_return_velocities);
    RUN_TEST_CASE(physics3d_moved_transforms_teleport);
    RUN_TEST_CASE(physics3d_teleports_wake_what_they_land_in);
    RUN_TEST_CASE(physics3d_children_follow_their_parent);
    RUN_TEST_CASE(physics3d_followers_use_the_parent_rotation);
    RUN_TEST_CASE(physics3d_followers_move_with_their_body);
    RUN_TEST_CASE(physics3d_followers_are_part_of_their_body);
    RUN_TEST_CASE(physics3d_followers_move_as_their_body_is_solved);
    RUN_TEST_CASE(physics3d_moving_followers_wake_what_they_meet);
    RUN_TEST_CASE(physics3d_followers_never_touch_their_body);
    RUN_TEST_CASE(physics3d_followers_stop_touching_the_body_they_join);
    RUN_TEST_CASE(physics3d_raycasts_find_the_nearest_collider);
    RUN_TEST_CASE(physics3d_shape_casts_sweep_a_collider);
    RUN_TEST_CASE(physics3d_contact_queries_match_what_touches);
    RUN_TEST_CASE(physics3d_characters_slide_along_what_they_meet);
    RUN_TEST_CASE(physics3d_characters_meet_only_solid_others);
    RUN_TEST_CASE(physics3d_characters_need_a_kinematic_capsule);
    RUN_TEST_CASE(physics3d_contacts_end_when_bodies_part_or_go);
    RUN_TEST_CASE(physics3d_masks_and_sensors_filter_contacts);
    RUN_TEST_CASE(physics3d_interpolates_moving_roots_only);
    RUN_TEST_CASE(physics3d_rejects_invalid_colliders);
    RUN_TEST_CASE(physics3d_parallel_steps_match_serial);
    pu_clear_setup();
    RUN_TEST_CASE(physics3d_runs_as_a_world_module);
}
