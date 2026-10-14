#include "runtime/scenes/physics3d.h"

#include "execution/status.h"
#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/events.h"
#include "runtime/geometry/mesh_asset.h"
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
 * and Contact Begin 3D / End 3D events out through entities. */
typedef struct
{
    ecs_entity owner;
    uint64_t other;
    bool begin;
    CF_V3 point;
} contact;

static const float STEP = 1.0f / 60;
static const float METER = UNITS_PER_METER;
static ecs_world *test_world;
static entities *test_entities;
static physics3d *test_physics;
static contact *contacts; /* The last step's events in dispatch order. */

static int dispatch(void *, ecs_entity owner, const char *event, execution_arguments arguments)
{
    bool begin = host_event_is(event, HOST_EVENT_CONTACT_BEGIN_3D);
    assert(begin || host_event_is(event, HOST_EVENT_CONTACT_END_3D));
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
        assert(ecs_world_record_destroy(test_world, doomed[i], (uint64_t)i) == ECS_OK);
    }
    return ecs_world_apply_commands(test_world);
}

static int attach(void *, ecs_entity, const char *)
{
    return -1; /* Nothing spawns prototypes here. */
}

static void setup()
{
    test_world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &COMPONENT_Transform3D, &COMPONENT_GlobalTransform3D,
        &COMPONENT_Collider3D,  &COMPONENT_Body3D,
        &COMPONENT_Gravity3D,   &COMPONENT_Character3D,
        &COMPONENT_Joint3D
    };
    for (int i = 0; i < 7; i++) {
        assert(ecs_world_register_component(test_world, components[i], NULL) == ECS_OK);
    }
    const entity_host host = { .destroy = destroy, .attach = attach, .dispatch = dispatch };
    test_entities = entities_new(test_world, &host);
    test_physics = physics3d_new(test_world, test_entities);
}

static void teardown()
{
    physics3d_free(test_physics);
    entities_free(test_entities);
    ecs_world_free(test_world);
    afree(contacts);
    render_mesh_assets_clear();
}

static Collider3D box(float x, float y, float z)
{
    return (Collider3D){ .shape = COLLIDER3D_BOX,
                         .size = { x, y, z },
                         .category = 1,
                         .mask = 1,
                         .friction = .6f };
}

static Collider3D ball(float radius)
{
    return (Collider3D){ .shape = COLLIDER3D_SPHERE,
                         .radius = radius,
                         .category = 1,
                         .mask = 1,
                         .friction = .6f };
}

static Collider3D meshed(Collider3DShape shape, const char *mesh)
{
    Collider3D collider = { .shape = shape,
                            .mesh = mesh,
                            .category = 1,
                            .mask = 1,
                            .friction = .6f };
    return collider;
}

/* Publishes the points as source's mesh, every three indices a triangle. */
static void publish_mesh(const char *source, const CF_V3 *points, int count, const int *indices, int index_count)
{
    render_mesh_asset mesh = { .min = points[0], .max = points[0] };
    for (int i = 0; i < count; i++) {
        apush(mesh.vertices, ((render_mesh_surface_vertex){ .position = points[i] }));
        mesh.min = cf_min_v3(mesh.min, points[i]);
        mesh.max = cf_max_v3(mesh.max, points[i]);
    }
    for (int i = 0; i < index_count; i++) {
        apush(mesh.indices, indices[i]);
    }
    render_mesh_asset_publish(source, &mesh);
}

/* A block a meter on each side, from half a meter to one and a half along X,
 * with a point inside it that its hull leaves out. */
static void publish_block()
{
    CF_V3 points[9];
    for (int i = 0; i < 8; i++) {
        points[i] = (CF_V3){ (i & 1 ? 1.5f : .5f) * METER,
                             (i & 2 ? 1 : 0) * METER,
                             (i & 4 ? .5f : -.5f) * METER };
    }
    points[8] = (CF_V3){ METER, METER / 2, 0 };
    publish_mesh("content/meshes/block.mesh", points, 9, NULL, 0);
}

/* A square floor 20 meters across at height, as two triangles that repeat
 * their shared corners the way a mesh's seams do. */
static void publish_floor(float height)
{
    const float h = 10 * METER;
    CF_V3 points[] = { { -h, height, -h }, { -h, height, h }, { h, height, h },
                       { -h, height, -h }, { h, height, h },  { h, height, -h } };
    int indices[] = { 0, 1, 2, 3, 4, 5 };
    publish_mesh("content/meshes/floor.mesh", points, 6, indices, 6);
}

/* A unit-scale, unrotated collider at position, with a Body3D unless static. */
static ecs_entity collider(ecs_entity parent, CF_V3 position, Collider3D shape, BodyType type)
{
    ecs_entity entity = ecs_world_create_entity(test_world);
    assert(Transform3D_add(test_world, entity, 0) == ECS_OK);
    assert(Collider3D_add(test_world, entity, 0) == ECS_OK);
    if (type != BODY_STATIC) {
        assert(Body3D_add(test_world, entity, 0) == ECS_OK);
    }
    if (parent.slot) {
        assert(ecs_world_record_add_pair(test_world, entity, ECS_CHILD_OF, parent, 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    *Transform3D_get(test_world, entity) = (Transform3D){ .position = position,
                                                          .rotation = cf_quat_identity(),
                                                          .scale = { 1, 1, 1 } };
    *Collider3D_get(test_world, entity) = shape;
    if (type != BODY_STATIC) {
        Body3D_get(test_world, entity)->type = type;
    }
    return entity;
}

static ecs_entity ground()
{
    return collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, -METER / 2, 0 },
        box(40 * METER, METER, 40 * METER),
        BODY_STATIC
    );
}

static ecs_entity set_gravity(CF_V3 acceleration)
{
    ecs_entity entity = ecs_world_create_entity(test_world);
    assert(Gravity3D_add(test_world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    Gravity3D_get(test_world, entity)->acceleration = acceleration;
    return entity;
}

static int step(float delta)
{
    aclear(contacts);
    int result = physics3d_tick(test_physics, delta);
    if (result < 0) {
        fprintf(stderr, "physics3d %d: %s\n", result, physics3d_error(test_physics));
    }
    assert(result == FLOW_OK);
    do {
        assert(entities_dispatch_events(test_entities) >= 0);
    } while (entities_pending_events(test_entities));
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
    return Transform3D_get_const(test_world, entity)->position;
}

static Body3D *body(ecs_entity entity)
{
    return Body3D_get(test_world, entity);
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
    return physics3d_tick(test_physics, STEP) == FLOW_ERR_ARGUMENT &&
           strstr(physics3d_error(test_physics), message);
}

TEST_CASE(physics3d_box_falls_rests_and_reports_contact)
{
    ecs_entity floor = ground();
    ecs_entity cube = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 4 * METER, 0 },
        box(METER, METER, METER),
        BODY_DYNAMIC
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
    CF_Quat rest = Transform3D_get_const(test_world, cube)->rotation;
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
        BODY_DYNAMIC
    );
    /* A capsule lying along X, rotated a quarter turn about Z. */
    Collider3D capsule = { .shape = COLLIDER3D_CAPSULE,
                           .radius = METER / 4,
                           .height = 2 * METER,
                           .category = 1,
                           .mask = 1,
                           .friction = .6f };
    ecs_entity lying = collider((ecs_entity){ 0 }, (CF_V3){ 0, METER, 0 }, capsule, BODY_DYNAMIC);
    Transform3D_get(test_world, lying)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, 1.57079633f);
    ecs_entity scaled = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 4 * METER, 2 * METER, 0 },
        box(METER, METER, METER),
        BODY_DYNAMIC
    );
    Transform3D_get(test_world, scaled)->scale = (CF_V3){ 2, 2, 2 };
    steps(120);
    REQUIRE(near(position(sphere).y, METER / 4, METER / 64));
    REQUIRE(near(position(lying).y, METER / 4, METER / 64));
    REQUIRE(near(position(scaled).y, METER, METER / 64));
    /* The written-back rotation keeps the capsule lying along X. */
    CF_V3 axis = cf_mul_q_v3(Transform3D_get_const(test_world, lying)->rotation, (CF_V3){ 0, 1, 0 });
    REQUIRE(near(fabsf(axis.x), 1, 1e-3f));
    return true;
}

TEST_CASE(physics3d_gravity_comes_from_the_scene)
{
    ecs_entity scene = set_gravity((CF_V3){ 0 });
    ecs_entity floating = collider((ecs_entity){ 0 }, (CF_V3){ 0, METER, 0 }, ball(METER / 2), BODY_DYNAMIC);
    steps(60);
    REQUIRE(position(floating).y == METER && position(floating).x == 0);
    /* Changing it wakes the body, asleep after a second at rest. */
    Gravity3D_get(test_world, scene)->acceleration = (CF_V3){ 0, -10 * METER, 0 };
    steps(10);
    REQUIRE(position(floating).y < METER);
    set_gravity((CF_V3){ METER, 0, 0 });
    REQUIRE(failed_with("Only one entity may have Gravity3D; 2 do"));
    return true;
}

TEST_CASE(physics3d_bodies_take_and_return_velocities)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity mover = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), BODY_KINEMATIC);
    body(mover)->linear_velocity = (CF_V3){ METER, 0, 0 };
    body(mover)->angular_velocity = (CF_V3){ 0, 1, 0 };
    ecs_entity spinner = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 0, 10 * METER },
        box(METER, METER, METER),
        BODY_DYNAMIC
    );
    body(spinner)->angular_velocity = (CF_V3){ 0, 2, 0 };
    steps(60);
    /* One second later the kinematic body moved a meter and turned a radian. */
    REQUIRE(near_v3(position(mover), (CF_V3){ METER, 0, 0 }, 1e-3f * METER));
    CF_V3 heading = cf_mul_q_v3(
        Transform3D_get_const(test_world, mover)->rotation,
        (CF_V3){ 1, 0, 0 }
    );
    REQUIRE(near(atan2f(-heading.z, heading.x), 1, 1e-3f));
    REQUIRE(near(body(spinner)->angular_velocity.y, 2, 1e-3f));
    heading = cf_mul_q_v3(Transform3D_get_const(test_world, spinner)->rotation, (CF_V3){ 1, 0, 0 });
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
        BODY_DYNAMIC
    );
    steps(60);
    REQUIRE(near(position(cube).y, METER / 2, METER / 64));
    Transform3D_get(test_world, cube)->position = (CF_V3){ 3 * METER, 5 * METER, 0 };
    REQUIRE(step(STEP) == 2 && touched(cube, floor, false));
    REQUIRE(near(position(cube).x, 3 * METER, 1e-3f) && position(cube).y < 5 * METER);
    /* Moving the floor out from under a resting body wakes it to fall. */
    steps(120);
    REQUIRE(near(position(cube).y, METER / 2, METER / 64));
    Transform3D_get(test_world, floor)->position.y = -10 * METER;
    steps(20);
    REQUIRE(position(cube).y < METER / 4);
    return true;
}

TEST_CASE(physics3d_teleports_wake_what_they_land_in)
{
    ground();
    ecs_entity sphere = collider((ecs_entity){ 0 }, (CF_V3){ 0, METER / 2, 0 }, ball(METER / 2), BODY_DYNAMIC);
    ecs_entity pusher = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 5 * METER, METER / 2, 0 },
        box(METER, METER, METER),
        BODY_STATIC
    );
    steps(90);
    /* Asleep, the sphere's velocities read exactly zero. */
    REQUIRE(near_v3(body(sphere)->linear_velocity, (CF_V3){ 0 }, 0));
    /* A static box teleported into it pushes it out. */
    Transform3D_get(test_world, pusher)->position.x = .8f * METER;
    steps(30);
    REQUIRE(position(sphere).x < -.1f * METER);
    return true;
}

TEST_CASE(physics3d_children_follow_their_parent)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity carrier = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), BODY_KINEMATIC);
    body(carrier)->linear_velocity = (CF_V3){ METER, 0, 0 };
    Collider3D zone = box(METER / 2, METER / 2, METER / 2);
    zone.sensor = true;
    ecs_entity rider = collider(carrier, (CF_V3){ 0, 2 * METER, 0 }, zone, BODY_KINEMATIC);
    /* The rider ignores its own velocity and never writes its local pose. */
    body(rider)->linear_velocity = (CF_V3){ 0, -10 * METER, 0 };
    ecs_entity probe = collider(
        (ecs_entity){ 0 },
        (CF_V3){ METER, 2 * METER, 0 },
        ball(METER / 8),
        BODY_DYNAMIC
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
    REQUIRE(ecs_world_record_remove_pair(test_world, rider, ECS_CHILD_OF, carrier, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    body(rider)->linear_velocity = (CF_V3){ 0 };
    steps(10);
    REQUIRE(near_v3(position(rider), (CF_V3){ 0, 2 * METER, 0 }, 0));
    body(rider)->linear_velocity = (CF_V3){ 0, 0, METER };
    steps(60);
    REQUIRE(near_v3(position(rider), (CF_V3){ 0, 2 * METER, METER }, 1e-3f * METER));
    REQUIRE(ecs_world_record_add_pair(test_world, rider, ECS_CHILD_OF, carrier, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    steps(2);
    /* Moved to a distant parent, it teleports there within the step. */
    ecs_entity distant = collider((ecs_entity){ 0 }, (CF_V3){ 30 * METER, 0, 0 }, ball(METER / 4), BODY_STATIC);
    ecs_entity there = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 30 * METER, 2 * METER, METER },
        ball(METER / 8),
        BODY_DYNAMIC
    );
    REQUIRE(ecs_world_record_remove_pair(test_world, rider, ECS_CHILD_OF, carrier, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(test_world, rider, ECS_CHILD_OF, distant, 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(step(STEP) == 2 && touched(rider, there, true));
    /* A dynamic body under a placed parent is an error. */
    body(rider)->type = BODY_DYNAMIC;
    REQUIRE(failed_with("A dynamic Body3D must be placed in the world"));
    return true;
}

TEST_CASE(physics3d_followers_use_the_parent_rotation)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity turned = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), BODY_STATIC);
    /* A quarter turn about Y maps local +X to world -Z. */
    Transform3D_get(test_world, turned)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, 1.57079633f);
    Collider3D zone = ball(METER / 2);
    zone.sensor = true;
    ecs_entity arm = collider(turned, (CF_V3){ 4 * METER, 0, 0 }, zone, BODY_STATIC);
    ecs_entity there = collider((ecs_entity){ 0 }, (CF_V3){ 0, 0, -4 * METER }, ball(METER / 4), BODY_DYNAMIC);
    ecs_entity unrotated = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 4 * METER, 0, 0 },
        ball(METER / 4),
        BODY_DYNAMIC
    );
    REQUIRE(step(STEP) == 2 && touched(arm, there, true) && !touched(arm, unrotated, true));
    /* Turning the parent back carries the static child with it. */
    Transform3D_get(test_world, turned)->rotation = cf_quat_identity();
    REQUIRE(step(STEP) == 4 && touched(arm, there, false) && touched(arm, unrotated, true));
    return true;
}

/* A child of a moving body moves with it within the same step, so it touches
 * what lies where its parent carries it, not only where it started. */
TEST_CASE(physics3d_followers_move_with_their_body)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity parent = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), BODY_DYNAMIC);
    body(parent)->linear_velocity = (CF_V3){ 30 * METER, 0, 0 };
    Collider3D zone = ball(METER / 4);
    zone.sensor = true;
    ecs_entity child = collider(parent, (CF_V3){ METER, 0, 0 }, zone, BODY_STATIC);
    /* The child starts over 0.75..1.25 m and ends over 1.25..1.75 m; the
     * target covers 1.525..1.775 m. */
    ecs_entity target = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 1.65f * METER, 0, 0 },
        ball(METER / 8),
        BODY_DYNAMIC
    );
    REQUIRE(step(1.0f / 60) == 2 && touched(child, target, true));
    REQUIRE(near_v3(position(parent), (CF_V3){ METER / 2, 0, 0 }, 1e-3f * METER));
    REQUIRE(near_v3(position(child), (CF_V3){ METER, 0, 0 }, 0));
    /* A spinning parent carries its child along the arc's tangent: turning
     * 0.3 rad about Y in a step moves local +X toward -Z. */
    CF_V3 hub = { 0, 0, 10 * METER };
    ecs_entity spinner = collider((ecs_entity){ 0 }, hub, ball(METER / 4), BODY_KINEMATIC);
    body(spinner)->angular_velocity = (CF_V3){ 0, 18, 0 };
    ecs_entity arm = collider(spinner, (CF_V3){ METER, 0, 0 }, zone, BODY_STATIC);
    ecs_entity ahead = collider(
        (ecs_entity){ 0 },
        cf_add_v3(hub, (CF_V3){ .93f * METER, 0, -.55f * METER }),
        ball(METER / 8),
        BODY_DYNAMIC
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
    ecs_entity base = collider((ecs_entity){ 0 }, (CF_V3){ 0, 3 * METER, 0 }, ball(METER / 4), BODY_DYNAMIC);
    ecs_entity plate = collider(
        base,
        (CF_V3){ 0, -METER, 0 },
        box(2 * METER, METER / 2, 2 * METER),
        BODY_STATIC
    );
    ecs_entity crate = collider(
        (ecs_entity){ 0 },
        (CF_V3){ METER / 2, 3 * METER, 0 },
        box(METER / 2, METER / 2, METER / 2),
        BODY_DYNAMIC
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
    /* The ball's mass includes the plate's, at 1000 kg per cubic meter. */
    REQUIRE(near(body(base)->mass, 1000 * (2 + 4 * 3.14159265f / 3 / 64), .1f));
    return true;
}

/* A child goes where the solver takes its body within the step, even when the
 * body speeds up or is stopped. */
TEST_CASE(physics3d_followers_move_as_their_body_is_solved)
{
    /* Pushed from rest at 600 m/s^2, the body covers 1/6 m in its first step,
     * which carries the nose's edge from 1.25 m past the mark's at 1.325 m. */
    ecs_entity push = set_gravity((CF_V3){ 600 * METER, 0, 0 });
    ecs_entity rocket = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), BODY_DYNAMIC);
    Collider3D zone = ball(METER / 4);
    zone.sensor = true;
    ecs_entity nose = collider(rocket, (CF_V3){ METER, 0, 0 }, zone, BODY_STATIC);
    ecs_entity mark = collider((ecs_entity){ 0 }, (CF_V3){ 1.45f * METER, 0, 0 }, ball(METER / 8), BODY_STATIC);
    step(STEP);
    REQUIRE(touched(nose, mark, true));
    /* A body that would cover 1/2 m stops at a wall after 1/4 m, so its flag
     * stops short of the mark beyond it. */
    Gravity3D_get(test_world, push)->acceleration = (CF_V3){ 0 };
    CF_V3 at = { 0, 0, 20 * METER };
    ecs_entity ram = collider((ecs_entity){ 0 }, at, ball(METER / 4), BODY_DYNAMIC);
    body(ram)->linear_velocity = (CF_V3){ 30 * METER, 0, 0 };
    ecs_entity flag = collider(ram, (CF_V3){ 0, METER, 0 }, zone, BODY_STATIC);
    collider((ecs_entity){ 0 }, cf_add_v3(at, (CF_V3){ METER, 0, 0 }), box(METER, METER, METER), BODY_STATIC);
    ecs_entity beyond = collider(
        (ecs_entity){ 0 },
        cf_add_v3(at, (CF_V3){ .7f * METER, METER, 0 }),
        ball(METER / 8),
        BODY_STATIC
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
    ecs_entity sphere = collider((ecs_entity){ 0 }, (CF_V3){ 0, METER / 2, 0 }, ball(METER / 2), BODY_DYNAMIC);
    steps(90);
    REQUIRE(near_v3(body(sphere)->linear_velocity, (CF_V3){ 0 }, 0));
    ecs_entity carrier = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 5 * METER, 3 * METER, 0 },
        ball(METER / 4),
        BODY_KINEMATIC
    );
    body(carrier)->linear_velocity = (CF_V3){ -3 * METER, 0, 0 };
    collider(carrier, (CF_V3){ 0, -2.5f * METER, 0 }, box(METER, METER, METER), BODY_STATIC);
    steps(120);
    REQUIRE(position(sphere).x < -METER);
    return true;
}

/* A body and the colliders it carries move as one, so they never touch. */
TEST_CASE(physics3d_followers_never_touch_their_body)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity ship = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, box(METER, METER, METER), BODY_DYNAMIC);
    collider(ship, (CF_V3){ METER / 2, 0, 0 }, box(METER, METER, METER), BODY_STATIC);
    Collider3D zone = ball(METER);
    zone.sensor = true;
    collider(ship, (CF_V3){ 0 }, zone, BODY_KINEMATIC);
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
    ecs_entity player = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, box(METER, METER, METER), BODY_DYNAMIC);
    ecs_entity table = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 3 * METER, 0, 0 },
        box(METER, METER, METER),
        BODY_STATIC
    );
    ecs_entity item = collider(table, (CF_V3){ -2 * METER, 0, 0 }, box(METER, METER, METER), BODY_STATIC);
    REQUIRE(step(STEP) == 2 && touched(player, item, true));
    REQUIRE(ecs_world_record_add_pair(test_world, item, ECS_CHILD_OF, player, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    Transform3D_get(test_world, item)->position = (CF_V3){ METER / 2, 0, 0 };
    REQUIRE(step(STEP) == 2 && touched(player, item, false));
    steps(4);
    REQUIRE(near_v3(position(player), (CF_V3){ 0 }, 0));
    REQUIRE(near_v3(body(player)->linear_velocity, (CF_V3){ 0 }, 0));
    return true;
}

TEST_CASE(physics3d_contacts_end_when_bodies_part_or_go)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity left = collider((ecs_entity){ 0 }, (CF_V3){ -METER, 0, 0 }, ball(METER / 2), BODY_DYNAMIC);
    ecs_entity right = collider((ecs_entity){ 0 }, (CF_V3){ METER, 0, 0 }, ball(METER / 2), BODY_DYNAMIC);
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
    ecs_entity rest = collider((ecs_entity){ 0 }, (CF_V3){ 0, 3 * METER, 0 }, ball(METER / 2), BODY_DYNAMIC);
    ecs_entity gone = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 3 * METER + .99f * METER, 0 },
        ball(METER / 2),
        BODY_DYNAMIC
    );
    REQUIRE(step(STEP) == 2 && touched(rest, gone, true));
    entities_request_destroy(test_entities, gone);
    REQUIRE(entities_flush(test_entities) == 0);
    REQUIRE(step(STEP) == 1 && saw(rest, gone, false, NULL));
    return true;
}

TEST_CASE(physics3d_masks_and_sensors_filter_contacts)
{
    set_gravity((CF_V3){ 0 });
    Collider3D zone = box(2 * METER, 2 * METER, 2 * METER);
    zone.sensor = true;
    ecs_entity trigger = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, zone, BODY_STATIC);
    /* Two lanes through the sensor; a wall blocks only the ghost's. */
    ecs_entity runner = collider(
        (ecs_entity){ 0 },
        (CF_V3){ -2 * METER, 0, -METER / 2 },
        ball(METER / 4),
        BODY_DYNAMIC
    );
    Collider3D unmasked = ball(METER / 4);
    unmasked.mask = 0;
    ecs_entity ghost = collider((ecs_entity){ 0 }, (CF_V3){ -2 * METER, 0, METER / 2 }, unmasked, BODY_DYNAMIC);
    ecs_entity wall = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 0, METER / 2 },
        box(METER / 2, 2 * METER, METER / 2),
        BODY_STATIC
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
    ecs_entity mover = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, ball(METER / 4), BODY_DYNAMIC);
    body(mover)->linear_velocity = (CF_V3){ 60, 0, 0 };
    body(mover)->angular_velocity = (CF_V3){ 0, 30, 0 };
    ecs_entity rider = collider(mover, (CF_V3){ 0, METER, 0 }, ball(METER / 4), BODY_KINEMATIC);
    ecs_entity fixed = collider((ecs_entity){ 0 }, (CF_V3){ 0, 0, 5 * METER }, ball(METER / 4), BODY_STATIC);
    step(STEP);
    CF_Quat rotation;
    CF_V3 translation;
    REQUIRE(physics3d_interpolation(test_physics, mover, .5f, &rotation, &translation));
    /* Halfway back along this step's motion. */
    CF_V3 solved = position(mover);
    CF_V3 shown = cf_add_v3(cf_mul_q_v3(rotation, solved), translation);
    REQUIRE(near(solved.x, 1, 1e-3f) && near_v3(shown, (CF_V3){ .5f, 0, 0 }, 1e-3f));
    /* Box3D limits rotation to an eighth of a turn per step; this turns half a radian. */
    CF_Quat end = Transform3D_get_const(test_world, mover)->rotation;
    CF_Quat shown_rotation = cf_mul_q(rotation, end);
    REQUIRE(near(2 * asinf(end.y), .5f, 1e-2f));
    REQUIRE(
        near(shown_rotation.y, sinf(asinf(end.y) / 2), 1e-4f) && shown_rotation.x == 0
    );
    REQUIRE(!physics3d_interpolation(test_physics, rider, .5f, &rotation, &translation));
    REQUIRE(!physics3d_interpolation(test_physics, fixed, .5f, &rotation, &translation));
    /* A pose moved after the step shows as it is. */
    Transform3D_get(test_world, mover)->position.x = 9;
    REQUIRE(physics3d_interpolation(test_physics, mover, .5f, &rotation, &translation));
    REQUIRE(rotation.w == 1 && translation.x == 0);
    /* Its body outlives a removed Transform3D until the next step. */
    REQUIRE(Transform3D_remove(test_world, mover, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(!physics3d_interpolation(test_physics, mover, .5f, &rotation, &translation));
    return true;
}

TEST_CASE(physics3d_rejects_invalid_colliders)
{
    ecs_entity entity = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, box(METER, METER, METER), BODY_DYNAMIC);
    char expected[64];
    snprintf(expected, sizeof expected, "on entity %u.", entity.slot);
    Transform3D_get(test_world, entity)->scale = (CF_V3){ 0 };
    REQUIRE(failed_with("nonzero Transform3D scale") && failed_with(expected));
    Transform3D_get(test_world, entity)->scale = (CF_V3){ 1, 1, 1 };
    Collider3D_get(test_world, entity)->size.y = -1;
    REQUIRE(failed_with("box needs a positive size"));
    *Collider3D_get(test_world, entity) = (Collider3D){ .shape = COLLIDER3D_CAPSULE,
                                                        .radius = METER,
                                                        .height = METER };
    REQUIRE(failed_with("height of at least twice"));
    Collider3D_get(test_world, entity)->height = 2 * METER;
    Collider3D_get(test_world, entity)->friction = -1;
    REQUIRE(failed_with("friction and restitution"));
    Collider3D_get(test_world, entity)->friction = 0;
    Transform3D_get(test_world, entity)->position.x = NAN;
    REQUIRE(failed_with("finite Transform3D"));
    Transform3D_get(test_world, entity)->position.x = 0;
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
    physics_settings settings = physics_defaults();
    settings.workers = workers;
    assert(physics3d_configure(test_physics, settings) == FLOW_OK);
    ground();
    ecs_entity boxes[PILE];
    for (int i = 0; i < PILE; i++) {
        CF_V3 at = { (float)(i % 16 - 8) * 1.5f * METER,
                     (float)(i / 16) * 1.1f * METER + METER,
                     (float)(i % 5) * .2f * METER };
        boxes[i] = collider((ecs_entity){ 0 }, at, box(METER, METER, METER), BODY_DYNAMIC);
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
    task_pool *pool = task_pool_acquire_workers(4);
    bool ran = task_pool_tasks(pool)->workers == 1 || task_pool_started(pool);
    task_pool_release(pool);
    REQUIRE(ran);
    return true;
}

TEST_CASE(physics3d_runs_as_a_world_module)
{
    char error[256];
    world_options options = { .modules = WORLD_PHYSICS3D };
    world *simulation = world_new(&options, error, sizeof error);
    REQUIRE(simulation);
    ecs_world *ecs = world_ecs(simulation);
    engine *runtime = world_engine(simulation);
    ecs_entity entity = ecs_world_create_entity(ecs);
    REQUIRE(Transform3D_add(ecs, entity, 0) == ECS_OK);
    REQUIRE(Collider3D_add(ecs, entity, 0) == ECS_OK);
    REQUIRE(Body3D_add(ecs, entity, 0) == ECS_OK);
    REQUIRE(Gravity3D_add(ecs, entity, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    *Transform3D_get(ecs, entity) = (Transform3D){ .scale = { 1, 1, 1 } };
    *Collider3D_get(ecs, entity) = ball(1);
    *Body3D_get(ecs, entity) = (Body3D){ .type = BODY_DYNAMIC,
                                         .linear_velocity = { 60, 0, 0 } };
    REQUIRE(engine_set_playing(runtime, true) == 0);
    REQUIRE(engine_frame(runtime, 0) == FLOW_OK);
    REQUIRE(engine_frame(runtime, .1) == FLOW_OK);
    float solved = Transform3D_get_const(ecs, entity)->position.x;
    REQUIRE(near(solved, 60 * engine_last_timing(runtime)->simulation_time, 1e-3f));
    REQUIRE(GlobalTransform3D_get_const(ecs, entity)->position.x == solved);
    REQUIRE(engine_frame(runtime, .1 + ENGINE_FIXED_SECONDS * .5) == FLOW_OK);
    GlobalTransform3D shown = engine_render_transform_3d(runtime, ecs, entity);
    REQUIRE(near(shown.position.x, solved - 60 * ENGINE_FIXED_SECONDS * .5f, 1e-3f));
    /* A scene reset drops the bodies; the entity's next step recreates its own. */
    engine_reset_scene(runtime);
    REQUIRE(
        near_v3(engine_render_transform_3d(runtime, ecs, entity).position, (CF_V3){ solved, 0, 0 }, 0)
    );
    REQUIRE(engine_frame(runtime, .3) == FLOW_OK);
    REQUIRE(Transform3D_get_const(ecs, entity)->position.x > solved);
    world_free(simulation);
    return true;
}

/* Pushes act through the center of mass at the next advancing step, so an
 * equal push moves a cube eight times as massive an eighth as fast. Bodies
 * weigh 1000 kg per cubic meter, Box3D's density of water. */
TEST_CASE(physics3d_pushes_divide_by_mass)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity small = collider((ecs_entity){ 0 }, (CF_V3){ 0, 0, 0 }, box(METER, METER, METER), BODY_DYNAMIC);
    ecs_entity large = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 0, 8 * METER },
        box(2 * METER, 2 * METER, 2 * METER),
        BODY_DYNAMIC
    );
    const float mass = 1000; /* kg, of the one meter cube */
    REQUIRE(physics3d_apply_impulse(test_physics, small, (CF_V3){ 64 * mass, 0, 0 }) == FLOW_OK);
    REQUIRE(physics3d_apply_impulse(test_physics, large, (CF_V3){ 64 * mass, 0, 0 }) == FLOW_OK);
    step(0); /* Only an advancing step pushes. */
    REQUIRE(body(small)->linear_velocity.x == 0);
    /* Each step writes a dynamic body's mass for scripts to size pushes. */
    REQUIRE(near(body(small)->mass, mass, .01f) && near(body(large)->mass, 8 * mass, .1f));
    body(small)->linear_velocity.z = 5; /* Pushes add to a velocity set now. */
    step(STEP);
    REQUIRE(near_v3(body(small)->linear_velocity, (CF_V3){ 64, 0, 5 }, 1e-2f));
    REQUIRE(near_v3(body(large)->linear_velocity, (CF_V3){ 8, 0, 0 }, 1e-2f));
    /* A force accelerates through one step; the next step drops it. */
    REQUIRE(
        physics3d_apply_force(test_physics, large, (CF_V3){ 0, 8 * mass * 120, 0 }) == FLOW_OK
    );
    REQUIRE(
        physics3d_apply_force(test_physics, large, (CF_V3){ 0, 8 * mass * 120, 0 }) == FLOW_OK
    );
    step(STEP);
    REQUIRE(near_v3(body(large)->linear_velocity, (CF_V3){ 8, 4, 0 }, 1e-2f));
    step(STEP);
    REQUIRE(near_v3(body(large)->linear_velocity, (CF_V3){ 8, 4, 0 }, 1e-2f));
    return true;
}

/* Only a live dynamic body takes a push, and one that is gone or reset before
 * the step is dropped. */
TEST_CASE(physics3d_pushes_need_a_dynamic_body)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity floor = ground();
    ecs_entity mover = collider((ecs_entity){ 0 }, (CF_V3){ 0, 2 * METER, 0 }, ball(8), BODY_KINEMATIC);
    ecs_entity rock = collider((ecs_entity){ 0 }, (CF_V3){ 4 * METER, 2 * METER, 0 }, ball(8), BODY_DYNAMIC);
    const CF_V3 kick = { 0, 1000, 0 };
    REQUIRE(physics3d_apply_impulse(test_physics, floor, kick) == FLOW_ERR_ARGUMENT);
    REQUIRE(physics3d_apply_force(test_physics, mover, kick) == FLOW_ERR_ARGUMENT);
    REQUIRE(physics3d_apply_impulse(test_physics, rock, (CF_V3){ NAN, 0, 0 }) == FLOW_ERR_ARGUMENT);
    REQUIRE(physics3d_apply_force(test_physics, rock, (CF_V3){ INFINITY, 0, 0 }) == FLOW_ERR_ARGUMENT);
    step(STEP);
    REQUIRE(physics3d_apply_impulse(test_physics, rock, kick) == FLOW_OK);
    physics3d_reset(test_physics);
    step(STEP);
    REQUIRE(body(rock)->linear_velocity.y == 0);
    REQUIRE(physics3d_apply_impulse(test_physics, rock, kick) == FLOW_OK);
    REQUIRE(ecs_world_destroy_entity(test_world, rock) == ECS_OK);
    REQUIRE(physics3d_apply_impulse(test_physics, rock, kick) == FLOW_ERR_ARGUMENT);
    ecs_entity reused = collider((ecs_entity){ 0 }, (CF_V3){ 4 * METER, 2 * METER, 0 }, ball(8), BODY_DYNAMIC);
    REQUIRE(reused.slot == rock.slot);
    step(STEP); /* The push named the destroyed rock, not the slot's new body. */
    REQUIRE(body(reused)->linear_velocity.y == 0);
    return true;
}

void test_native_catalog_enable();
static bool is(ecs_entity a, ecs_entity b)
{
    return ecs_entity_to_u64(a) == ecs_entity_to_u64(b);
}

static physics3d_hit ray(CF_V3 start, CF_V3 end, unsigned categories, ecs_entity exclude, bool sensors)
{
    physics3d_hit hit;
    int result = physics3d_raycast(test_physics, start, end, categories, exclude, sensors, &hit);
    assert(result == FLOW_OK);
    return hit;
}

static physics3d_hit sweep(ecs_entity caster, CF_V3 translation, bool sensors)
{
    physics3d_hit hit;
    int result = physics3d_shape_cast(test_physics, caster, translation, ~0u, sensors, &hit);
    assert(result == FLOW_OK);
    return hit;
}

static bool hit_at(physics3d_hit hit, ecs_entity entity, CF_V3 point, CF_V3 normal, float distance)
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
    ecs_entity rock = collider((ecs_entity){ 0 }, (CF_V3){ 0, 2 * METER, 0 }, ball(METER / 2), BODY_STATIC);
    Collider3D zone = ball(METER / 2);
    zone.sensor = true;
    ecs_entity mist = collider((ecs_entity){ 0 }, (CF_V3){ 0, 4 * METER, 0 }, zone, BODY_STATIC);
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
    Collider3D plank = box(METER / 2, METER / 2, 2 * METER);
    plank.category = 2;
    ecs_entity beam = collider((ecs_entity){ 0 }, (CF_V3){ 6 * METER, METER, 0 }, plank, BODY_STATIC);
    Transform3D *placed = Transform3D_get(test_world, beam);
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
        BODY_KINEMATIC
    );
    ecs_entity mast = collider(cart, (CF_V3){ 0, METER, 0 }, box(METER / 4, METER, METER / 4), BODY_STATIC);
    CF_V3 above = { 0, 10 * METER, 5 * METER }, below = { 0, -METER, 5 * METER };
    REQUIRE(is(ray(above, below, ~0u, (ecs_entity){ 0 }, false).entity, mast));
    REQUIRE(is(ray(above, below, ~0u, mast, false).entity, floor));
    REQUIRE(is(ray(above, below, ~0u, cart, false).entity, floor));
    physics3d_hit hit;
    REQUIRE(
        physics3d_raycast(test_physics, (CF_V3){ 0, 1e9f, 0 }, bottom, ~0u, (ecs_entity){ 0 }, false, &hit) ==
        FLOW_ERR_ARGUMENT
    );
    Collider3D_get(test_world, mist)->radius = 0;
    REQUIRE(
        physics3d_raycast(test_physics, top, bottom, ~0u, (ecs_entity){ 0 }, false, &hit) == FLOW_ERR_ARGUMENT
    );
    REQUIRE(!hit.found);
    return true;
}

TEST_CASE(physics3d_shape_casts_sweep_a_collider)
{
    ecs_entity floor = ground();
    ecs_entity drop = collider((ecs_entity){ 0 }, (CF_V3){ 0, 5 * METER, 0 }, ball(METER / 2), BODY_DYNAMIC);
    /* Its own child below it is part of its body, not in its way. */
    collider(drop, (CF_V3){ 0, -2 * METER, 0 }, box(METER, METER / 4, METER), BODY_STATIC);
    REQUIRE(hit_at(
        sweep(drop, (CF_V3){ 0, -10 * METER, 0 }, false),
        floor,
        (CF_V3){ 0 },
        (CF_V3){ 0, 1, 0 },
        4.5f * METER
    ));
    REQUIRE(!sweep(drop, (CF_V3){ 0, -4 * METER, 0 }, false).found);
    /* A capsule turned a quarter about Z lies along X, 2 m from end to end. */
    Collider3D log = { .shape = COLLIDER3D_CAPSULE,
                       .radius = METER / 4,
                       .height = 2 * METER,
                       .category = 1,
                       .mask = 1 };
    ecs_entity roller = collider((ecs_entity){ 0 }, (CF_V3){ 0, 2 * METER, 10 * METER }, log, BODY_DYNAMIC);
    Transform3D_get(test_world, roller)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, 1.57079633f);
    ecs_entity wall = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 5 * METER, 2 * METER, 10 * METER },
        box(METER, 4 * METER, 4 * METER),
        BODY_STATIC
    );
    Collider3D zone = box(METER, 4 * METER, 4 * METER);
    zone.sensor = true;
    ecs_entity gate = collider((ecs_entity){ 0 }, (CF_V3){ 3 * METER, 2 * METER, 10 * METER }, zone, BODY_STATIC);
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
    Transform3D_get(test_world, roller)->position.x = 5 * METER;
    physics3d_hit stuck = sweep(roller, across, false);
    REQUIRE(stuck.found && is(stuck.entity, wall) && stuck.distance == 0);
    REQUIRE(near_v3(stuck.normal, (CF_V3){ 0 }, 0));
    ecs_entity gone = ecs_world_create_entity(test_world);
    REQUIRE(!sweep(gone, across, false).found);
    return true;
}

/* Contact queries answer what the simulation would let touch: a resting
 * crate touches its floor, a near solid counts within Box3D's margin, a
 * sensor needs overlap, and masks and bodies apply. */
TEST_CASE(physics3d_contact_queries_match_what_touches)
{
    ecs_entity floor = ground();
    ecs_entity crate = collider((ecs_entity){ 0 }, (CF_V3){ 0, METER, 0 }, box(METER, METER, METER), BODY_DYNAMIC);
    ecs_entity handle = collider(
        crate,
        (CF_V3){ 0, -METER / 4, 0 },
        box(METER / 4, METER / 2, METER / 4),
        BODY_STATIC
    );
    bool landed = false;
    for (int i = 0; i < 240; i++) {
        step(STEP);
        landed = landed || touched(crate, floor, true) || touched(handle, floor, true);
    }
    REQUIRE(landed && near_v3(body(crate)->linear_velocity, (CF_V3){ 0 }, 0));
    ecs_entity *found = NULL;
    REQUIRE(physics3d_contact_entities(test_physics, crate, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 1 && is(found[0], floor));
    REQUIRE(physics3d_contact_entities(test_physics, floor, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 2 && is(found[0], crate) && is(found[1], handle));
    REQUIRE(physics3d_contact_entities(test_physics, floor, 2, &found) == FLOW_OK);
    REQUIRE(acount(found) == 0);
    /* A hundredth of a meter above a solid is within the margin; a sensor
     * there touches nothing, and neither does a solid it can't collide with. */
    float hover = METER / 2 + METER / 100;
    ecs_entity near_ball = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 5 * METER, hover, 0 },
        ball(METER / 2),
        BODY_STATIC
    );
    Collider3D zone = ball(METER / 2);
    zone.sensor = true;
    ecs_entity near_zone = collider((ecs_entity){ 0 }, (CF_V3){ 10 * METER, hover, 0 }, zone, BODY_STATIC);
    Collider3D ghost = ball(METER / 2);
    ghost.mask = 2;
    ecs_entity sunk = collider((ecs_entity){ 0 }, (CF_V3){ -5 * METER, 0, 0 }, ghost, BODY_STATIC);
    REQUIRE(physics3d_contact_entities(test_physics, near_ball, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 1 && is(found[0], floor));
    REQUIRE(physics3d_contact_entities(test_physics, near_zone, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 0);
    Transform3D_get(test_world, near_zone)->position.y = METER / 4;
    REQUIRE(physics3d_contact_entities(test_physics, near_zone, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 1 && is(found[0], floor));
    REQUIRE(physics3d_contact_entities(test_physics, sunk, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 0);
    REQUIRE(physics3d_contact_entities(test_physics, crate, 0, &found) == FLOW_OK);
    REQUIRE(acount(found) == 0);
    afree(found);
    return true;
}

/* A hull collider wraps its mesh's vertices, however many it casts with,
 * scaled, mirrored and carried like any other shape. */
TEST_CASE(physics3d_hulls_wrap_mesh_vertices)
{
    ecs_entity floor = ground();
    /* An octagonal drum a meter across and tall, sixteen vertices. */
    CF_V3 drum[16];
    for (int i = 0; i < 8; i++) {
        float angle = (float)i * 3.14159265f / 4;
        drum[2 * i] = (CF_V3){ cosf(angle) * METER / 2,
                               METER / 2,
                               sinf(angle) * METER / 2 };
        drum[2 * i + 1] = (CF_V3){ drum[2 * i].x, -METER / 2, drum[2 * i].z };
    }
    publish_mesh("content/meshes/drum.mesh", drum, 16, NULL, 0);
    Collider3D can = meshed(COLLIDER3D_HULL, "content/meshes/drum.mesh");
    ecs_entity barrel = collider((ecs_entity){ 0 }, (CF_V3){ 0, 4 * METER, 0 }, can, BODY_DYNAMIC);
    ecs_entity lid = collider(barrel, (CF_V3){ 0, METER, 0 }, can, BODY_STATIC);
    physics3d_hit hit = sweep(barrel, (CF_V3){ 0, -10 * METER, 0 }, false);
    REQUIRE(hit.found && is(hit.entity, floor) && near(hit.distance, 3.5f * METER, METER / 100));
    steps(120);
    REQUIRE(near_v3(position(barrel), (CF_V3){ 0, METER / 2, 0 }, METER / 64));
    CF_V3 top = { 0, 10 * METER, 0 }, up = { 0, 1, 0 };
    REQUIRE(hit_at(
        ray(top, (CF_V3){ 0 }, ~0u, (ecs_entity){ 0 }, false),
        lid,
        (CF_V3){ 0, 2 * METER, 0 },
        up,
        8 * METER
    ));
    ecs_entity *found = NULL;
    REQUIRE(physics3d_contact_entities(test_physics, barrel, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 1 && is(found[0], floor));
    afree(found);
    /* Mirrored in X, a block beside its origin lies on the other side. */
    publish_block();
    Collider3D block = meshed(COLLIDER3D_HULL, "content/meshes/block.mesh");
    ecs_entity mirrored = collider((ecs_entity){ 0 }, (CF_V3){ 0, 0, 5 * METER }, block, BODY_STATIC);
    Transform3D_get(test_world, mirrored)->scale = (CF_V3){ -1, 1, 1 };
    REQUIRE(hit_at(
        ray((CF_V3){ 5 * METER, METER / 2, 5 * METER },
            (CF_V3){ -5 * METER, METER / 2, 5 * METER },
            ~0u,
            (ecs_entity){ 0 },
            false),
        mirrored,
        (CF_V3){ -METER / 2, METER / 2, 5 * METER },
        (CF_V3){ 1, 0, 0 },
        5.5f * METER
    ));
    ecs_entity left = collider(
        (ecs_entity){ 0 },
        (CF_V3){ -METER, 3 * METER, 5 * METER },
        ball(METER / 4),
        BODY_DYNAMIC
    );
    ecs_entity right = collider(
        (ecs_entity){ 0 },
        (CF_V3){ METER, 3 * METER, 5 * METER },
        ball(METER / 4),
        BODY_DYNAMIC
    );
    steps(120);
    REQUIRE(near(position(left).y, 1.25f * METER, METER / 64));
    REQUIRE(near(position(right).y, METER / 4, METER / 64));
    return true;
}

/* A static mesh collider holds up convex bodies on its triangles, answers
 * queries, and rebuilds when its mesh is published again. */
TEST_CASE(physics3d_meshes_hold_up_convex_bodies)
{
    publish_floor(0);
    Collider3D terrain = meshed(COLLIDER3D_MESH, "content/meshes/floor.mesh");
    ecs_entity floor = collider((ecs_entity){ 0 }, (CF_V3){ 0, METER, 0 }, terrain, BODY_STATIC);
    ecs_entity rock = collider((ecs_entity){ 0 }, (CF_V3){ 0, 4 * METER, 0 }, ball(METER / 2), BODY_DYNAMIC);
    CF_V3 top = { METER, 10 * METER, 0 }, up = { 0, 1, 0 };
    REQUIRE(hit_at(
        ray(top, (CF_V3){ METER, 0, 0 }, ~0u, (ecs_entity){ 0 }, false),
        floor,
        (CF_V3){ METER, METER, 0 },
        up,
        9 * METER
    ));
    bool landed = false;
    for (int i = 0; i < 120; i++) {
        step(STEP);
        landed = landed || touched(rock, floor, true);
    }
    REQUIRE(landed && near(position(rock).y, 1.5f * METER, METER / 64));
    ecs_entity *found = NULL;
    REQUIRE(physics3d_contact_entities(test_physics, rock, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 1 && is(found[0], floor));
    REQUIRE(physics3d_contact_entities(test_physics, floor, ~0u, &found) == FLOW_OK);
    REQUIRE(acount(found) == 1 && is(found[0], rock));
    afree(found);
    physics3d_hit hit;
    REQUIRE(physics3d_shape_cast(test_physics, floor, up, ~0u, false, &hit) == FLOW_ERR_ARGUMENT);
    /* The rock sleeps on it; a floor raised into it lifts it. */
    publish_floor(METER / 4);
    steps(60);
    REQUIRE(near(position(rock).y, 1.75f * METER, METER / 64));
    return true;
}

/* Box3D sensors visit only convex shapes, so a sensor passing through a mesh,
 * or a body through a mesh sensor, still reports both edges once. */
TEST_CASE(physics3d_sensors_overlap_meshes)
{
    set_gravity((CF_V3){ 0 });
    publish_floor(0);
    Collider3D terrain = meshed(COLLIDER3D_MESH, "content/meshes/floor.mesh");
    ecs_entity floor = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, terrain, BODY_STATIC);
    terrain.sensor = true;
    ecs_entity veil = collider((ecs_entity){ 0 }, (CF_V3){ 0, 10 * METER, 0 }, terrain, BODY_STATIC);
    Collider3D zone = box(METER, METER, METER);
    zone.sensor = true;
    ecs_entity probe = collider((ecs_entity){ 0 }, (CF_V3){ 0, 2 * METER, 0 }, zone, BODY_KINEMATIC);
    zone.mask = 0;
    ecs_entity blind = collider((ecs_entity){ 0 }, (CF_V3){ 2 * METER, 2 * METER, 0 }, zone, BODY_KINEMATIC);
    ecs_entity rock = collider((ecs_entity){ 0 }, (CF_V3){ 0, 12 * METER, 0 }, ball(METER / 4), BODY_DYNAMIC);
    const ecs_entity movers[] = { probe, blind, rock };
    for (int i = 0; i < 3; i++) {
        body(movers[i])->linear_velocity = (CF_V3){ 0, -2 * METER, 0 };
    }
    int entered = 0, left = 0, crossed = 0;
    CF_V3 point = { 0 };
    for (int i = 0; i < 120; i++) {
        step(STEP);
        CF_V3 seen;
        if (saw(probe, floor, true, &seen)) {
            point = seen;
        }
        entered += touched(probe, floor, true);
        left += touched(probe, floor, false);
        crossed += touched(rock, veil, true) + touched(rock, veil, false);
        REQUIRE(!saw(blind, floor, true, NULL) && !saw(probe, veil, true, NULL));
    }
    REQUIRE(entered == 1 && left == 1 && crossed == 2);
    REQUIRE(near_v3(point, (CF_V3){ 0 }, METER / 8));
    REQUIRE(near(body(rock)->linear_velocity.y, -2 * METER, 1e-3f));
    return true;
}

/* A Transform3D without a collider, under parent when it has one. */
static ecs_entity point(ecs_entity parent, CF_V3 position)
{
    ecs_entity entity = ecs_world_create_entity(test_world);
    assert(Transform3D_add(test_world, entity, 0) == ECS_OK);
    if (parent.slot) {
        assert(ecs_world_record_add_pair(test_world, entity, ECS_CHILD_OF, parent, 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    *Transform3D_get(test_world, entity) = (Transform3D){ .position = position,
                                                          .rotation = cf_quat_identity(),
                                                          .scale = { 1, 1, 1 } };
    return entity;
}

/* Joins entity's body to other's, or to the world when other is zero. */
static Joint3D *join(ecs_entity entity, Joint3DKind kind, ecs_entity other)
{
    assert(Joint3D_add(test_world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    Joint3D *joint = Joint3D_get(test_world, entity);
    joint->kind = kind;
    joint->other = other.slot ? ecs_entity_to_u64(other) : 0;
    return joint;
}

static float distance(ecs_entity a, ecs_entity b)
{
    return cf_len_v3(cf_sub_v3(position(a), position(b)));
}

TEST_CASE(physics3d_hinges_and_sliders_keep_to_their_axis)
{
    Collider3D block = box(METER, METER, METER);
    ecs_entity door = collider((ecs_entity){ 0 }, (CF_V3){ 0, 2 * METER, 0 }, block, BODY_DYNAMIC);
    Joint3D *hinge = join(door, JOINT3D_HINGE, (ecs_entity){ 0 });
    hinge->motor = true;
    hinge->speed = 1;
    hinge->strength = 1e9f;
    ecs_entity gate = collider((ecs_entity){ 0 }, (CF_V3){ -4 * METER, 2 * METER, 0 }, block, BODY_DYNAMIC);
    hinge = join(gate, JOINT3D_HINGE, (ecs_entity){ 0 });
    hinge->motor = hinge->limit = true;
    hinge->speed = 1;
    hinge->strength = 1e6f;
    hinge->upper = .5f;
    ecs_entity lift = collider((ecs_entity){ 0 }, (CF_V3){ 4 * METER, 2 * METER, 0 }, block, BODY_DYNAMIC);
    Joint3D *slider = join(lift, JOINT3D_SLIDER, (ecs_entity){ 0 });
    slider->limit = true;
    slider->lower = -METER;
    steps(60);
    /* The hinge holds the door up and its motor turns it about Y. */
    REQUIRE(near_v3(position(door), (CF_V3){ 0, 2 * METER, 0 }, METER / 64));
    REQUIRE(near_v3(body(door)->angular_velocity, (CF_V3){ 0, 1, 0 }, 1e-2f));
    /* The gate's motor pushes it only to its limit. */
    CF_Quat swung = Transform3D_get_const(test_world, gate)->rotation;
    REQUIRE(
        near(2 * asinf(swung.y), .5f, 1e-2f) &&
        near(body(gate)->angular_velocity.y, 0, 1e-2f)
    );
    /* The lift fell along Y to its lower limit and stays there, upright. */
    REQUIRE(near_v3(position(lift), (CF_V3){ 4 * METER, METER, 0 }, METER / 64));
    REQUIRE(physics3d_apply_impulse(test_physics, lift, (CF_V3){ 1e6f, 0, 1e6f }) == FLOW_OK);
    steps(10);
    REQUIRE(near_v3(position(lift), (CF_V3){ 4 * METER, METER, 0 }, METER / 64));
    REQUIRE(near(Transform3D_get_const(test_world, lift)->rotation.w, 1, 1e-4f));
    return true;
}

TEST_CASE(physics3d_balls_welds_and_distances_hold_bodies)
{
    /* A bob hangs from a ball joint a meter from its center. */
    ecs_entity bob = collider((ecs_entity){ 0 }, (CF_V3){ METER, 0, 0 }, ball(METER / 4), BODY_DYNAMIC);
    join(point(bob, (CF_V3){ -METER, 0, 0 }), JOINT3D_BALL, (ecs_entity){ 0 });
    /* Two boxes welded side by side fall and take a push as one. */
    Collider3D block = box(METER, METER, METER);
    ecs_entity left = collider((ecs_entity){ 0 }, (CF_V3){ 10 * METER, 0, 0 }, block, BODY_DYNAMIC);
    ecs_entity right = collider((ecs_entity){ 0 }, (CF_V3){ 11 * METER, 0, 0 }, block, BODY_DYNAMIC);
    join(right, JOINT3D_WELD, left);
    /* A rod keeps a weight two meters from a fixed post, and a rope lets
     * another fall until it is taut. */
    Collider3D small = ball(METER / 4);
    ecs_entity post = collider((ecs_entity){ 0 }, (CF_V3){ 20 * METER, 0, 0 }, small, BODY_STATIC);
    ecs_entity weight = collider((ecs_entity){ 0 }, (CF_V3){ 20 * METER, -2 * METER, 0 }, small, BODY_DYNAMIC);
    join(weight, JOINT3D_DISTANCE, post);
    body(weight)->linear_velocity = (CF_V3){ 0, 0, 2 * METER };
    ecs_entity hook = collider((ecs_entity){ 0 }, (CF_V3){ 30 * METER, 0, 0 }, small, BODY_STATIC);
    ecs_entity load = collider((ecs_entity){ 0 }, (CF_V3){ 30 * METER, -METER / 2, 0 }, small, BODY_DYNAMIC);
    Joint3D *rope = join(load, JOINT3D_DISTANCE, hook);
    rope->spring = true;
    rope->limit = true;
    rope->upper = 2 * METER;
    steps(2);
    REQUIRE(physics3d_apply_impulse(test_physics, left, (CF_V3){ 0, 0, 1e5f }) == FLOW_OK);
    steps(58);
    /* A second in, the bob has swung through to the far side. */
    REQUIRE(near(cf_len_v3(position(bob)), METER, METER / 64) && position(bob).x < -METER / 2);
    /* The push turned the pair; the right box stays a meter along the left's X. */
    CF_Quat turned = Transform3D_get_const(test_world, left)->rotation;
    CF_V3 offset = cf_mul_q_v3(cf_quat_conjugate(turned), cf_sub_v3(position(right), position(left)));
    REQUIRE(near_v3(offset, (CF_V3){ METER, 0, 0 }, METER / 64));
    REQUIRE(position(left).y < -METER && position(left).z > METER / 64);
    REQUIRE(
        near(distance(weight, post), 2 * METER, METER / 64) &&
        position(weight).z > METER / 4
    );
    REQUIRE(near(distance(load, hook), 2 * METER, METER / 32));
    return true;
}

TEST_CASE(physics3d_joints_follow_their_components)
{
    Collider3D block = box(METER, METER, METER);
    ecs_entity lamp = collider((ecs_entity){ 0 }, (CF_V3){ 0, 5 * METER, 0 }, block, BODY_DYNAMIC);
    join(lamp, JOINT3D_HINGE, (ecs_entity){ 0 });
    steps(10);
    REQUIRE(
        near(position(lamp).y, 5 * METER, METER / 64) &&
        body(lamp)->angular_velocity.y == 0
    );
    /* A field change makes the joint again: now its motor turns it. */
    Joint3D *hinge = Joint3D_get(test_world, lamp);
    hinge->motor = true;
    hinge->speed = -2;
    hinge->strength = 1e9f;
    steps(10);
    REQUIRE(near(body(lamp)->angular_velocity.y, -2, 1e-2f));
    /* Moving the joint to the wheel's rim makes it again there. */
    ecs_entity wheel = collider((ecs_entity){ 0 }, (CF_V3){ 8 * METER, 5 * METER, 0 }, block, BODY_DYNAMIC);
    ecs_entity axle = point(wheel, (CF_V3){ 0 });
    hinge = join(axle, JOINT3D_HINGE, (ecs_entity){ 0 });
    hinge->motor = true;
    hinge->speed = 2;
    hinge->strength = 1e9f;
    steps(10);
    REQUIRE(near_v3(position(wheel), (CF_V3){ 8 * METER, 5 * METER, 0 }, METER / 64));
    Transform3D_get(test_world, axle)->position = cf_mul_q_v3(
        cf_quat_conjugate(Transform3D_get_const(test_world, wheel)->rotation),
        (CF_V3){ METER, 0, 0 }
    );
    steps(20);
    CF_V3 rim = { 9 * METER, 5 * METER, 0 };
    REQUIRE(near(cf_len_v3(cf_sub_v3(position(wheel), rim)), METER, METER / 64));
    REQUIRE(!near_v3(position(wheel), (CF_V3){ 8 * METER, 5 * METER, 0 }, METER / 4));
    /* Without its Joint3D it falls. */
    REQUIRE(Joint3D_remove(test_world, lamp, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    steps(30);
    REQUIRE(position(lamp).y < 5 * METER - METER / 4);
    /* A weld to a body that is gone holds nothing. */
    ecs_entity shelf = collider((ecs_entity){ 0 }, (CF_V3){ 4 * METER, 5 * METER, 0 }, block, BODY_KINEMATIC);
    ecs_entity vase = collider((ecs_entity){ 0 }, (CF_V3){ 5 * METER, 5 * METER, 0 }, block, BODY_DYNAMIC);
    join(vase, JOINT3D_WELD, shelf);
    steps(10);
    REQUIRE(near(position(vase).y, 5 * METER, METER / 64));
    REQUIRE(ecs_world_destroy_entity(test_world, shelf) == ECS_OK);
    steps(30);
    REQUIRE(position(vase).y < 5 * METER - METER / 4);
    /* A body being destroyed releases a joint on an entity under it, and a
     * joint to one, in the step before its children are destroyed too. */
    ecs_entity bob = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 20 * METER, 5 * METER, 0 },
        ball(METER / 4),
        BODY_DYNAMIC
    );
    join(point(bob, (CF_V3){ -METER, 0, 0 }), JOINT3D_BALL, (ecs_entity){ 0 });
    ecs_entity weight = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 24 * METER, 5 * METER, 0 },
        ball(METER / 4),
        BODY_DYNAMIC
    );
    join(weight, JOINT3D_DISTANCE, point(bob, (CF_V3){ 0 }));
    steps(2);
    entities_request_destroy(test_entities, bob);
    steps(1);
    REQUIRE(entities_flush(test_entities) == 0);
    steps(1);
    return true;
}

TEST_CASE(physics3d_joints_ignore_unused_limits_and_start_anywhere)
{
    /* A slider whose limit is off ignores lower and upper, so it falls. */
    Collider3D block = box(METER, METER, METER);
    ecs_entity drawer = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, block, BODY_DYNAMIC);
    Joint3D *slider = join(drawer, JOINT3D_SLIDER, (ecs_entity){ 0 });
    slider->lower = 1;
    /* A rope tied on at its hook's origin starts slack and falls taut. */
    Collider3D small = ball(METER / 4);
    ecs_entity hook = collider((ecs_entity){ 0 }, (CF_V3){ 10 * METER, 0, 0 }, small, BODY_STATIC);
    ecs_entity load = collider((ecs_entity){ 0 }, (CF_V3){ 10 * METER, -METER / 2, 0 }, small, BODY_DYNAMIC);
    Joint3D *rope = join(point(load, (CF_V3){ 0, METER / 2, 0 }), JOINT3D_DISTANCE, hook);
    rope->spring = rope->limit = true;
    rope->upper = 2 * METER;
    steps(60);
    REQUIRE(position(drawer).y < -METER && near(position(drawer).x, 0, METER / 64));
    REQUIRE(near_v3(position(load), (CF_V3){ 10 * METER, -2.5f * METER, 0 }, METER / 32));
    return true;
}

static bool joint_fails(ecs_entity entity, const char *message)
{
    bool failed = failed_with(message);
    *Joint3D_get(test_world, entity) = (Joint3D){ 0 };
    return failed && physics3d_tick(test_physics, STEP) == FLOW_OK;
}

TEST_CASE(physics3d_joints_need_two_bodies_and_sound_values)
{
    ecs_entity bare = ecs_world_create_entity(test_world);
    REQUIRE(Joint3D_add(test_world, bare, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(failed_with("A Joint3D needs a Transform3D on entity"));
    REQUIRE(ecs_world_destroy_entity(test_world, bare) == ECS_OK);
    ecs_entity loose = point((ecs_entity){ 0 }, (CF_V3){ 0 });
    join(loose, JOINT3D_WELD, (ecs_entity){ 0 });
    REQUIRE(failed_with("A Joint3D must be on or under a 3D body on entity"));
    REQUIRE(Joint3D_remove(test_world, loose, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    ecs_entity crate = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, box(METER, METER, METER), BODY_DYNAMIC);
    ecs_entity grip = point(crate, (CF_V3){ METER / 2, 0, 0 });
    Joint3D *joint = join(grip, JOINT3D_WELD, loose);
    REQUIRE(joint_fails(grip, "A Joint3D's other must be on or under a 3D body"));
    Joint3D_get(test_world, grip)->other = ecs_entity_to_u64(crate);
    REQUIRE(joint_fails(grip, "A Joint3D must join two different bodies"));
    Joint3D_get(test_world, grip)->kind = JOINT3D_DISTANCE;
    REQUIRE(joint_fails(grip, "A distance Joint3D needs another body"));
    ecs_entity post = collider((ecs_entity){ 0 }, (CF_V3){ 4 * METER, 0, 0 }, ball(METER / 4), BODY_STATIC);
    joint = Joint3D_get(test_world, grip);
    joint->kind = JOINT3D_DISTANCE;
    joint->other = ecs_entity_to_u64(post);
    joint->limit = true;
    joint->lower = -2;
    joint->upper = -1;
    REQUIRE(joint_fails(grip, "A distance Joint3D limit needs lower at least 0"));
    /* A transform-only joint entity or other must be finite too. */
    Joint3D_get(test_world, grip)->other = ecs_entity_to_u64(post);
    Transform3D_get(test_world, grip)->position.x = NAN;
    REQUIRE(failed_with("A Joint3D and its other need finite Transform3Ds"));
    Transform3D_get(test_world, grip)->position.x = METER / 2;
    ecs_entity tip = point(post, (CF_V3){ INFINITY, 0, 0 });
    Joint3D_get(test_world, grip)->other = ecs_entity_to_u64(tip);
    REQUIRE(joint_fails(grip, "A Joint3D and its other need finite Transform3Ds"));
    joint = Joint3D_get(test_world, grip);
    joint->kind = JOINT3D_BALL;
    joint->limit = true;
    joint->upper = 2;
    REQUIRE(joint_fails(grip, "A ball Joint3D limit needs upper between 0 and pi/2"));
    joint->limit = true;
    joint->lower = 1;
    REQUIRE(joint_fails(grip, "A Joint3D limit needs lower at most upper"));
    Joint3D_get(test_world, grip)->hertz = -1;
    REQUIRE(joint_fails(grip, "Joint3D strength, hertz and damping must not be negative"));
    Joint3D_get(test_world, grip)->speed = NAN;
    REQUIRE(joint_fails(grip, "Joint3D values must be finite"));
    Joint3D_get(test_world, grip)->kind = (Joint3DKind)99;
    REQUIRE(joint_fails(grip, "Unknown Joint3D kind"));
    return true;
}

TEST_CASE(physics3d_hulls_and_meshes_need_prepared_meshes)
{
    Collider3D missing = meshed(COLLIDER3D_HULL, "content/meshes/missing.mesh");
    ecs_entity entity = collider((ecs_entity){ 0 }, (CF_V3){ 0 }, missing, BODY_DYNAMIC);
    REQUIRE(
        failed_with("Collider3D mesh content/meshes/missing.mesh is not a prepared content .mesh on entity")
    );
    physics3d_hit hit;
    REQUIRE(
        physics3d_raycast(test_physics, (CF_V3){ 0, METER, 0 }, (CF_V3){ 0 }, ~0u, (ecs_entity){ 0 }, false, &hit) ==
        FLOW_ERR_ARGUMENT
    );
    Collider3D *shape = Collider3D_get(test_world, entity);
    shape->mesh = "";
    REQUIRE(failed_with("A Collider3D hull or mesh needs a mesh and a scale of at least .01"));
    CF_V3 flat[] = { { 0 }, { METER, 0, 0 }, { 0, 0, METER }, { METER, 0, METER } };
    int square[] = { 0, 2, 1, 1, 2, 3 };
    publish_mesh("content/meshes/flat.mesh", flat, 4, square, 6);
    shape->mesh = "content/meshes/flat.mesh";
    REQUIRE(failed_with("A Collider3D hull needs a mesh whose vertices enclose a volume"));
    publish_block();
    shape->mesh = "content/meshes/block.mesh";
    Transform3D *placed = Transform3D_get(test_world, entity);
    placed->scale = (CF_V3){ .001f, 1, 1 };
    REQUIRE(failed_with("a scale of at least .01"));
    placed->scale = (CF_V3){ 1e5f, 1, 1 };
    REQUIRE(failed_with("A Collider3D hull or mesh must fit within 100 km after scale"));
    placed->scale = (CF_V3){ 1, 1, 1 };
    REQUIRE(physics3d_tick(test_physics, STEP) == FLOW_OK);
    shape->shape = COLLIDER3D_MESH;
    shape->mesh = "content/meshes/flat.mesh";
    REQUIRE(failed_with("A Collider3D mesh must be static and placed in the world"));
    body(entity)->type = BODY_STATIC;
    REQUIRE(physics3d_tick(test_physics, STEP) == FLOW_OK);
    CF_V3 sliver[] = { { 0 }, { METER, 0, 0 }, { 2 * METER, 0, 0 } };
    int line[] = { 0, 1, 2 };
    publish_mesh("content/meshes/sliver.mesh", sliver, 3, line, 3);
    shape->mesh = "content/meshes/sliver.mesh";
    REQUIRE(failed_with("A Collider3D mesh needs a mesh with a triangle of nonzero area"));
    shape->mesh = "content/meshes/flat.mesh";
    collider(entity, (CF_V3){ 0, METER, 0 }, meshed(COLLIDER3D_MESH, "content/meshes/flat.mesh"), BODY_STATIC);
    REQUIRE(failed_with("A Collider3D mesh must be static and placed in the world"));
    return true;
}

static const float SLOP = .005f * METER; /* Box3D's linear slop. */

/* A kinematic capsule character, 1.8 m tall and .4 m in radius. */
static ecs_entity character(CF_V3 position)
{
    Collider3D capsule = { .shape = COLLIDER3D_CAPSULE,
                           .radius = .4f * METER,
                           .height = 1.8f * METER,
                           .category = 1,
                           .mask = 1 };
    ecs_entity entity = collider((ecs_entity){ 0 }, position, capsule, BODY_KINEMATIC);
    assert(Character3D_add(test_world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    return entity;
}

/* Steps a character at velocity, adding gravity as a script would when it
 * falls. */
static void walk(ecs_entity walker, CF_V3 velocity, bool falls, int count)
{
    for (int i = 0; i < count; i++) {
        Body3D *moving = body(walker);
        float fall = falls ? moving->linear_velocity.y - 9.81f * METER * STEP
                           : velocity.y;
        moving->linear_velocity = cf_v3(velocity.x, fall, velocity.z);
        step(STEP);
    }
}

static const Character3D *character_state(ecs_entity walker)
{
    return Character3D_get_const(test_world, walker);
}

static ecs_entity platform(BodyType type)
{
    return collider((ecs_entity){ 0 }, (CF_V3){ 0, -METER / 2, 0 }, box(8 * METER, METER, 8 * METER), type);
}

TEST_CASE(physics3d_characters_ride_translating_platforms)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(character_state(walker)->on_floor);
    CF_V3 offset = cf_sub_v3(position(walker), position(floor));
    CF_V3 velocities[] = { { 2 * METER, 0, METER },
                           { 0, 2 * METER, 0 },
                           { 0, -2 * METER, 0 } };
    for (int direction = 0; direction < 3; direction++) {
        body(floor)->linear_velocity = velocities[direction];
        for (int i = 0; i < 30; i++) {
            walk(walker, (CF_V3){ 0 }, true, 1);
            REQUIRE(character_state(walker)->on_floor);
            REQUIRE(near_v3(cf_sub_v3(position(walker), position(floor)), offset, 2 * SLOP));
            REQUIRE(near_v3(body(walker)->linear_velocity, (CF_V3){ 0 }, 1e-3f));
        }
    }
    return true;
}

TEST_CASE(physics3d_characters_ride_rotating_and_dynamic_platforms)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity floor = platform(BODY_DYNAMIC);
    ecs_entity walker = character((CF_V3){ 2 * METER, .9f * METER, 0 });
    step(STEP);
    REQUIRE(character_state(walker)->on_floor);
    CF_V3 offset = cf_sub_v3(position(walker), position(floor));
    body(floor)->linear_velocity = (CF_V3){ METER, METER, 0 };
    body(floor)->angular_velocity = (CF_V3){ 0, 2, 0 };
    for (int i = 0; i < 120; i++) {
        step(STEP);
        CF_Quat rotation = Transform3D_get_const(test_world, floor)->rotation;
        CF_V3 local = cf_mul_q_v3(
            cf_quat_conjugate(rotation),
            cf_sub_v3(position(walker), position(floor))
        );
        REQUIRE(near_v3(local, offset, 2 * SLOP));
        REQUIRE(character_state(walker)->on_floor);
        REQUIRE(near_v3(body(walker)->linear_velocity, (CF_V3){ 0 }, 1e-3f));
        REQUIRE(near(Transform3D_get_const(test_world, walker)->rotation.w, 1, 1e-6f));
    }
    REQUIRE(near(position(floor).y, 1.5f * METER, 2 * SLOP));
    REQUIRE(near(body(floor)->linear_velocity.y, METER, 1e-3f));
    return true;
}

TEST_CASE(physics3d_dynamic_platform_forces_do_not_stop_at_the_rider)
{
    set_gravity((CF_V3){ 0 });
    ecs_entity floor = platform(BODY_DYNAMIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    step(STEP);
    REQUIRE(touched(walker, floor, true));
    float mass = body(floor)->mass;
    REQUIRE(mass > 0);
    REQUIRE(
        physics3d_apply_impulse(test_physics, floor, (CF_V3){ 0, mass * 2 * METER, 0 }) == FLOW_OK
    );
    step(STEP);
    REQUIRE(near(body(floor)->linear_velocity.y, 2 * METER, 1e-3f));
    REQUIRE(!touched(walker, floor, false));
    REQUIRE(character_state(walker)->on_floor);
    REQUIRE(
        physics3d_apply_force(test_physics, floor, (CF_V3){ 0, mass * 200 * METER, 0 }) == FLOW_OK
    );
    step(STEP);
    REQUIRE(near(body(floor)->linear_velocity.y, 2 * METER + 200 * METER * STEP, 1e-3f));
    REQUIRE(!touched(walker, floor, false));
    REQUIRE(character_state(walker)->on_floor);
    REQUIRE(near(position(walker).y - position(floor).y, 1.4f * METER, 2 * SLOP));
    return true;
}

TEST_CASE(physics3d_platform_carry_stops_at_thin_obstacles)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    walk(walker, (CF_V3){ 0 }, true, 1);
    collider(
        (ecs_entity){ 0 },
        (CF_V3){ METER, 2 * METER, 0 },
        box(.1f * METER, 4 * METER, 8 * METER),
        BODY_STATIC
    );
    /* Authored movement beyond the wall still sweeps the full carry. */
    Transform3D_get(test_world, floor)->position.x += 3 * METER;
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(position(walker).x <= .55f * METER + 2 * SLOP);
    REQUIRE(position(walker).x > .5f * METER);
    REQUIRE(near(body(walker)->linear_velocity.x, 0, 1e-3f));
    REQUIRE(character_state(walker)->on_floor);
    return true;
}

TEST_CASE(physics3d_characters_leave_platforms)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    walk(walker, (CF_V3){ 0 }, true, 1);
    body(floor)->linear_velocity.x = 2 * METER;
    CF_V3 at = position(walker);
    walk(walker, (CF_V3){ 0, 5 * METER, 0 }, false, 1);
    REQUIRE(!character_state(walker)->on_floor);
    REQUIRE(near(position(walker).x, at.x, 1e-3f));
    walk(walker, (CF_V3){ 0 }, true, 90);
    REQUIRE(character_state(walker)->on_floor);
    walk(walker, (CF_V3){ 0, 0, 8 * METER }, true, 60);
    REQUIRE(!character_state(walker)->on_floor);
    at = position(walker);
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(near(position(walker).x, at.x, 1e-3f));
    return true;
}

TEST_CASE(physics3d_platform_support_drops_replaced_shapes_and_characters)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    walk(walker, (CF_V3){ 0 }, true, 1);
    /* Replacing a collider at a distant pose must not move its old rider. */
    Collider3D_get(test_world, floor)->size.x *= .5f;
    Transform3D_get(test_world, floor)->position.x = 20 * METER;
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(near(position(walker).x, 0, 1e-3f));
    REQUIRE(!character_state(walker)->on_floor);
    Transform3D_get(test_world, floor)->position.x = 0;
    Transform3D_get(test_world, walker)->position = (CF_V3){ 0, .9f * METER, 0 };
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(character_state(walker)->on_floor);
    Transform3D_get(test_world, walker)->position.x = -20 * METER;
    body(floor)->linear_velocity.x = 2 * METER;
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(near(position(walker).x, -20 * METER, 1e-3f));
    REQUIRE(!character_state(walker)->on_floor);
    return true;
}

TEST_CASE(physics3d_platform_support_drops_destroyed_bodies)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(ecs_world_record_destroy(test_world, floor, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    ecs_entity replacement = platform(BODY_KINEMATIC);
    Transform3D_get(test_world, replacement)->position.x = 20 * METER;
    body(replacement)->linear_velocity.x = 2 * METER;
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(near(position(walker).x, 0, 1e-3f));
    REQUIRE(!character_state(walker)->on_floor);
    physics3d_reset(test_physics);
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(near(position(walker).x, 0, 1e-3f));
    return true;
}

TEST_CASE(physics3d_platform_support_obeys_filters_and_sensors)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    for (int change = 0; change < 3; change++) {
        Collider3D_get(test_world, floor)->sensor = false;
        Collider3D_get(test_world, floor)->mask = 1;
        Collider3D_get(test_world, walker)->mask = 1;
        Transform3D_get(test_world, floor)->position.x = 0;
        Transform3D_get(test_world, walker)->position = (CF_V3){ 0, .9f * METER, 0 };
        walk(walker, (CF_V3){ 0 }, true, 1);
        REQUIRE(character_state(walker)->on_floor);
        switch (change) {
            case 0: Collider3D_get(test_world, floor)->sensor = true; break;
            case 1: Collider3D_get(test_world, floor)->mask = 2; break;
            case 2: Collider3D_get(test_world, walker)->mask = 2; break;
        }
        Transform3D_get(test_world, floor)->position.x = METER;
        walk(walker, (CF_V3){ 0 }, true, 1);
        REQUIRE(near(position(walker).x, 0, 1e-3f));
        REQUIRE(!character_state(walker)->on_floor);
    }
    return true;
}

TEST_CASE(physics3d_platform_support_follows_gravity)
{
    ecs_entity sky = set_gravity((CF_V3){ -10 * METER, 0, 0 });
    ecs_entity floor = collider(
        (ecs_entity){ 0 },
        (CF_V3){ -METER / 2, 0, 0 },
        box(METER, 8 * METER, 8 * METER),
        BODY_KINEMATIC
    );
    ecs_entity walker = character((CF_V3){ .9f * METER, 0, 0 });
    Transform3D_get(test_world, walker)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, 1.57079633f);
    body(walker)->linear_velocity = (CF_V3){ -METER, 0, 0 };
    step(STEP);
    REQUIRE(character_state(walker)->on_floor);
    CF_V3 offset = cf_sub_v3(position(walker), position(floor));
    body(floor)->linear_velocity.y = METER;
    for (int i = 0; i < 30; i++) {
        body(walker)->linear_velocity = (CF_V3){ -METER, 0, 0 };
        step(STEP);
        REQUIRE(near_v3(cf_sub_v3(position(walker), position(floor)), offset, 2 * SLOP));
        REQUIRE(character_state(walker)->on_floor);
    }
    Gravity3D_get(test_world, sky)->acceleration.x *= -1;
    CF_V3 at = position(walker);
    body(walker)->linear_velocity = (CF_V3){ 0 };
    step(STEP);
    REQUIRE(!character_state(walker)->on_floor);
    REQUIRE(near(position(walker).y, at.y, 1e-3f));
    return true;
}

TEST_CASE(physics3d_platform_carry_refreshes_sensor_contacts)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    Collider3D zone = box(METER, METER, METER);
    zone.sensor = true;
    ecs_entity sensor = collider((ecs_entity){ 0 }, (CF_V3){ 2 * METER, METER, 0 }, zone, BODY_STATIC);
    walk(walker, (CF_V3){ 0 }, true, 1);
    Transform3D_get(test_world, floor)->position.x += 2 * METER;
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(touched(walker, sensor, true));
    Transform3D_get(test_world, floor)->position.x += 2 * METER;
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(touched(walker, sensor, false));
    return true;
}

TEST_CASE(physics3d_platform_carry_stops_at_ceilings)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 2.5f * METER, 0 },
        box(8 * METER, .1f * METER, 8 * METER),
        BODY_STATIC
    );
    walk(walker, (CF_V3){ 0 }, true, 1);
    Transform3D_get(test_world, floor)->position.y += 4 * METER;
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(position(walker).y <= 1.55f * METER + 2 * SLOP);
    REQUIRE(position(walker).y > 1.5f * METER);
    REQUIRE(!character_state(walker)->on_floor);
    return true;
}

TEST_CASE(physics3d_platform_cannot_push_a_rider_through_a_ceiling)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, 2.5f * METER, 0 },
        box(8 * METER, .1f * METER, 8 * METER),
        BODY_STATIC
    );
    walk(walker, (CF_V3){ 0 }, true, 1);
    Transform3D_get(test_world, floor)->position.y += METER;
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(position(walker).y <= 1.55f * METER + 2 * SLOP);
    REQUIRE(position(walker).y > 1.5f * METER);
    return true;
}

TEST_CASE(physics3d_platform_support_uses_a_followers_body)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity top = collider(
        floor,
        (CF_V3){ 0, METER / 2, 0 },
        box(8 * METER, METER / 5, 8 * METER),
        BODY_STATIC
    );
    ecs_entity walker = character((CF_V3){ 2 * METER, METER, 0 });
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(character_state(walker)->on_floor);
    Transform3D_get(test_world, floor)->position.x = METER;
    Transform3D_get(test_world, floor)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, 1.57079633f);
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(near_v3(position(walker), (CF_V3){ METER, METER, -2 * METER }, 2 * SLOP));
    REQUIRE(character_state(walker)->on_floor);
    ecs_entity other = platform(BODY_KINEMATIC);
    Transform3D_get(test_world, other)->position.x = 20 * METER;
    REQUIRE(ecs_world_record_remove_pair(test_world, top, ECS_CHILD_OF, floor, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(test_world, top, ECS_CHILD_OF, other, 1) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    CF_V3 at = position(walker);
    body(floor)->linear_velocity.x = METER;
    body(other)->linear_velocity.x = METER;
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(near(position(walker).x, at.x, 1e-3f));
    return true;
}

TEST_CASE(physics3d_platform_tilt_keeps_the_capsule_upright)
{
    ecs_entity floor = platform(BODY_KINEMATIC);
    ecs_entity walker = character((CF_V3){ 2 * METER, .9f * METER, 0 });
    walk(walker, (CF_V3){ 0 }, true, 1);
    CF_Quat tilt = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, .2f);
    Transform3D_get(test_world, floor)->rotation = tilt;
    walk(walker, (CF_V3){ 0 }, true, 1);
    CF_V3 surface = cf_add_v3(
        position(floor),
        cf_mul_q_v3(tilt, (CF_V3){ 2 * METER, .5f * METER, 0 })
    );
    REQUIRE(character_state(walker)->on_floor);
    REQUIRE(near(position(walker).x, surface.x, .04f * METER));
    REQUIRE(
        near_v3(character_state(walker)->floor_normal, cf_mul_q_v3(tilt, (CF_V3){ 0, 1, 0 }), 1e-3f)
    );
    REQUIRE(near(Transform3D_get_const(test_world, walker)->rotation.w, 1, 1e-6f));
    Transform3D_get(test_world, floor)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, .9f);
    walk(walker, (CF_V3){ 0 }, true, 1);
    REQUIRE(!character_state(walker)->on_floor);
    return true;
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
        BODY_STATIC
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
        BODY_STATIC
    );
    ecs_entity floater = character((CF_V3){ 0, 5 * METER, 0 });
    walk(floater, (CF_V3){ 2 * METER, 0, 0 }, false, 120);
    REQUIRE(near(position(floater).x, 2.1f * METER, 2 * SLOP));
    REQUIRE(!character_state(floater)->on_floor);
    Collider3D zone = box(METER, 4 * METER, 4 * METER);
    zone.sensor = true;
    collider((ecs_entity){ 0 }, (CF_V3){ 0, 2 * METER, 3 * METER }, zone, BODY_STATIC);
    ecs_entity walker = character((CF_V3){ 0, .9f * METER, 0 });
    Collider3D pack = box(METER / 2, METER / 2, METER / 2);
    pack.sensor = true;
    collider(walker, (CF_V3){ 0, 0, -.6f * METER }, pack, BODY_STATIC);
    ecs_entity crate = collider(
        (ecs_entity){ 0 },
        (CF_V3){ 0, METER / 2, 8 * METER },
        box(METER, METER, METER),
        BODY_DYNAMIC
    );
    walk(walker, (CF_V3){ 0, 0, 2 * METER }, false, 240);
    REQUIRE(near(position(walker).z, 7.1f * METER, 2 * SLOP));
    REQUIRE(near(position(crate).z, 8 * METER, SLOP));
    /* Sideways gravity turns the wall into a floor. */
    Gravity3D_get(test_world, sky)->acceleration = (CF_V3){ 9.81f * METER, 0, 0 };
    walk(floater, (CF_V3){ METER, 0, 0 }, false, 1);
    REQUIRE(character_state(floater)->on_floor);
    REQUIRE(near_v3(character_state(floater)->floor_normal, (CF_V3){ -1, 0, 0 }, 1e-3f));
    return true;
}

/* A character meeting the floor in a pass shorter than the mover's tolerance
 * stands on it in that step. */
TEST_CASE(physics3d_characters_land_within_a_short_pass)
{
    ground();
    ecs_entity walker = character((CF_V3){ 0, .9f * METER + METER / 200, 0 });
    body(walker)->linear_velocity = (CF_V3){ 0, -2 * METER, 0 };
    step(STEP);
    REQUIRE(character_state(walker)->on_floor);
    REQUIRE(near(body(walker)->linear_velocity.y, 0, 1e-3f));
    REQUIRE(near(position(walker).y, .9f * METER, 2 * SLOP));
    return true;
}

/* Characters stop at each other as at walls, even when both move toward each
 * other in the same step. */
TEST_CASE(physics3d_characters_meet_each_other)
{
    set_gravity((CF_V3){ 0, 0, 0 });
    ecs_entity left = character((CF_V3){ 0, METER, 0 });
    ecs_entity right = character((CF_V3){ 0, METER, 2 * METER });
    for (int i = 0; i < 30; i++) {
        body(left)->linear_velocity = (CF_V3){ 0, 0, 4 * METER };
        body(right)->linear_velocity = (CF_V3){ 0, 0, -4 * METER };
        step(STEP);
        REQUIRE(position(right).z - position(left).z >= .8f * METER - 4 * SLOP);
    }
    return true;
}

TEST_CASE(physics3d_characters_need_a_kinematic_capsule)
{
    ecs_entity walker = character((CF_V3){ 0, METER, 0 });
    step(STEP);
    body(walker)->type = BODY_DYNAMIC;
    REQUIRE(failed_with("A Character3D needs a kinematic Body3D"));
    body(walker)->type = BODY_KINEMATIC;
    Collider3D_get(test_world, walker)->sensor = true;
    REQUIRE(failed_with("A Character3D needs a solid capsule Collider3D"));
    *Collider3D_get(test_world, walker) = box(METER, METER, METER);
    REQUIRE(failed_with("A Character3D needs a solid capsule Collider3D"));
    Collider3D thin = { .shape = COLLIDER3D_CAPSULE, .radius = METER / 100, .height = METER };
    *Collider3D_get(test_world, walker) = thin;
    REQUIRE(failed_with("at least two centimeters in radius"));
    thin.radius = METER / 50;
    *Collider3D_get(test_world, walker) = thin;
    REQUIRE(physics3d_tick(test_physics, STEP) == FLOW_OK);
    /* The mover sweeps the capsule as it stands, so physics keeps it from
     * spinning, even through a tick that does not advance. */
    body(walker)->angular_velocity = (CF_V3){ 10, 0, 0 };
    REQUIRE(physics3d_tick(test_physics, 0) == FLOW_OK);
    REQUIRE(near_v3(body(walker)->angular_velocity, (CF_V3){ 0 }, 0));
    body(walker)->angular_velocity = (CF_V3){ 10, 0, 0 };
    REQUIRE(physics3d_tick(test_physics, STEP) == FLOW_OK);
    REQUIRE(near_v3(body(walker)->angular_velocity, (CF_V3){ 0 }, 0));
    REQUIRE(physics3d_tick(test_physics, STEP) == FLOW_OK);
    CF_Quat turned = Transform3D_get_const(test_world, walker)->rotation;
    REQUIRE(near(turned.w, 1, 1e-6f));
    /* Only its capsule moves, so what it carries must be a sensor. */
    ecs_entity pack = collider(walker, (CF_V3){ 0 }, box(METER, METER, METER), BODY_STATIC);
    REQUIRE(failed_with("A Collider3D under a Character3D must be a sensor"));
    Collider3D_get(test_world, pack)->sensor = true;
    REQUIRE(physics3d_tick(test_physics, STEP) == FLOW_OK);
    ecs_entity lost = ecs_world_create_entity(test_world);
    assert(Transform3D_add(test_world, lost, 0) == ECS_OK);
    assert(Character3D_add(test_world, lost, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(failed_with("A Character3D needs a Transform3D and a Collider3D"));
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
    RUN_TEST_CASE(physics3d_hulls_wrap_mesh_vertices);
    RUN_TEST_CASE(physics3d_meshes_hold_up_convex_bodies);
    RUN_TEST_CASE(physics3d_sensors_overlap_meshes);
    RUN_TEST_CASE(physics3d_hulls_and_meshes_need_prepared_meshes);
    RUN_TEST_CASE(physics3d_hinges_and_sliders_keep_to_their_axis);
    RUN_TEST_CASE(physics3d_balls_welds_and_distances_hold_bodies);
    RUN_TEST_CASE(physics3d_joints_follow_their_components);
    RUN_TEST_CASE(physics3d_joints_ignore_unused_limits_and_start_anywhere);
    RUN_TEST_CASE(physics3d_joints_need_two_bodies_and_sound_values);
    RUN_TEST_CASE(physics3d_characters_slide_along_what_they_meet);
    RUN_TEST_CASE(physics3d_characters_ride_translating_platforms);
    RUN_TEST_CASE(physics3d_characters_ride_rotating_and_dynamic_platforms);
    RUN_TEST_CASE(physics3d_dynamic_platform_forces_do_not_stop_at_the_rider);
    RUN_TEST_CASE(physics3d_platform_carry_stops_at_thin_obstacles);
    RUN_TEST_CASE(physics3d_characters_leave_platforms);
    RUN_TEST_CASE(physics3d_platform_support_drops_replaced_shapes_and_characters);
    RUN_TEST_CASE(physics3d_platform_support_drops_destroyed_bodies);
    RUN_TEST_CASE(physics3d_platform_support_obeys_filters_and_sensors);
    RUN_TEST_CASE(physics3d_platform_support_follows_gravity);
    RUN_TEST_CASE(physics3d_platform_carry_refreshes_sensor_contacts);
    RUN_TEST_CASE(physics3d_platform_carry_stops_at_ceilings);
    RUN_TEST_CASE(physics3d_platform_cannot_push_a_rider_through_a_ceiling);
    RUN_TEST_CASE(physics3d_platform_support_uses_a_followers_body);
    RUN_TEST_CASE(physics3d_platform_tilt_keeps_the_capsule_upright);
    RUN_TEST_CASE(physics3d_characters_meet_only_solid_others);
    RUN_TEST_CASE(physics3d_characters_land_within_a_short_pass);
    RUN_TEST_CASE(physics3d_characters_meet_each_other);
    RUN_TEST_CASE(physics3d_characters_need_a_kinematic_capsule);
    RUN_TEST_CASE(physics3d_pushes_divide_by_mass);
    RUN_TEST_CASE(physics3d_pushes_need_a_dynamic_body);
    RUN_TEST_CASE(physics3d_contacts_end_when_bodies_part_or_go);
    RUN_TEST_CASE(physics3d_masks_and_sensors_filter_contacts);
    RUN_TEST_CASE(physics3d_interpolates_moving_roots_only);
    RUN_TEST_CASE(physics3d_rejects_invalid_colliders);
    RUN_TEST_CASE(physics3d_parallel_steps_match_serial);
    pu_clear_setup();
    RUN_TEST_CASE(physics3d_runs_as_a_world_module);
}
