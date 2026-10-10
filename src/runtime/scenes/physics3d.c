#include "runtime/scenes/physics3d.h"

#include "execution/status.h"
#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/events.h"
#include "runtime/geometry/mesh_asset.h"
#include "runtime/task_pool.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <box3d/box3d.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    PARALLEL_BODY_MINIMUM = 256,
    /* A hull keeps this many vertices at most, so it can also be cast. */
    HULL_VERTICES = B3_MAX_SHAPE_CAST_POINTS
};

/* In project units. Box3D loses float precision beyond about 16 km. */
static const float EARTH_GRAVITY = 9.81f * GAME_UNITS_PER_METER;
static const float FARTHEST = 100000 * GAME_UNITS_PER_METER;
/* A follower's scale comes from decomposing its world matrix, whose last bits
 * wobble as parents turn; smaller changes keep the shape. */
static const float SCALE_TOLERANCE = 1e-4f;
/* The narrowest character; its body gives up a centimeter of radius. */
static const float CHARACTER_RADIUS = .02f * GAME_UNITS_PER_METER;
/* A mesh repeats a position at each seam in its normals or UVs; rejoining
 * positions this close lets Box3D find the edges its triangles share. */
static const float WELD_TOLERANCE = .001f * GAME_UNITS_PER_METER;

typedef struct
{
    CF_V3 position;
    CF_Quat rotation;
} pose;

/* Where an entity's body goes. A mirrored scale is negative in x; boxes,
 * spheres and capsules use its magnitude. */
typedef struct
{
    pose pose;
    CF_V3 scale;
    bool follower; /* Placed by a ChildOf parent with a Transform3D. */
} placement;

/* One per entity slot while the entity has a collider. */
typedef struct
{
    ecs_entity entity;
    b3BodyId body; /* Its own, or null while another body carries its shape. */
    b3ShapeId shape;         /* Null while a follower waits to be placed. */
    unsigned carrier;        /* The slot whose body holds the shape. */
    GameCollider3D collider; /* Its mesh is not read; see mesh. */
    int mesh;                /* 1 + its collision mesh, or 0. */
    GameBodyType type;
    CF_V3 scale; /* The scale the shape was built at. */
    bool follower, seen, character;
    /* A root's local transform as physics last read or wrote it; a change
     * means something else moved it. */
    GameTransform3D written;
    /* A follower's pose on its carrier's body, or in the world when it has a
     * body of its own. */
    pose placed;
    CF_V3 linear_velocity, angular_velocity; /* As last set or written back. */
    pose start, end; /* A moving root's solved poses around the last step. */
} body_record;

/* A mesh's collision data, built from the mesh catalog when a hull or mesh
 * collider first names it and kept until a reset. */
typedef struct
{
    char *source;
    uint64_t identity; /* The catalog asset's; republishing it builds anew. */
    CF_V3 extent;      /* The largest coordinate on each axis. */
    b3HullData *hull;  /* Null until a hull collider uses it. */
    b3MeshData *mesh;  /* Null until a mesh collider uses it. */
} collision_mesh;

typedef struct
{
    uint64_t first, second;
    CF_V3 point;
} contact_pair;

typedef struct
{
    b3TaskCallback *run;
    void *context;
} task_slot;

/* An impulse or force for a dynamic body's next advancing step. */
typedef struct
{
    ecs_entity entity;
    CF_V3 amount;
    bool impulse;
} push;

struct game_physics3d
{
    ecs_world *world;
    game_entities *entities;
    game_physics_settings settings;
    game_task_pool *pool;
    game_task_system tasks;
    b3WorldId simulation; /* Null until the first collider. */
    CF_V3 gravity;        /* As the world last applied it. */
    body_record *bodies;
    int body_count;
    contact_pair *previous, *current;
    b3ContactData *contacts;
    b3ShapeId *shapes; /* Scratch for sensor visitors and a body's shapes. */
    push *pushes;
    collision_mesh *meshes;
    task_slot slots[B3_MAX_TASKS];
    int slot_count;
    char error[256];
};

static bool same_entity(ecs_entity a, ecs_entity b)
{
    return ecs_entity_to_u64(a) == ecs_entity_to_u64(b);
}

static b3Vec3 to_b3(CF_V3 v)
{
    return (b3Vec3){ v.x, v.y, v.z };
}

static CF_V3 from_b3(b3Vec3 v)
{
    return cf_v3(v.x, v.y, v.z);
}

static b3Quat quat_to_b3(CF_Quat q)
{
    return (b3Quat){ { q.x, q.y, q.z }, q.w };
}

static CF_Quat quat_from_b3(b3Quat q)
{
    return cf_quat(q.v.x, q.v.y, q.v.z, q.s);
}

static bool same_v3(CF_V3 a, CF_V3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static bool same_pose(pose a, pose b)
{
    return same_v3(a.position, b.position) && a.rotation.x == b.rotation.x &&
           a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
           a.rotation.w == b.rotation.w;
}

static pose local_pose(const GameTransform3D *local)
{
    return (pose){ local->position, local->rotation };
}

static bool finite_v3(CF_V3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static bool within(CF_V3 v, float limit)
{
    return finite_v3(v) && fabsf(v.x) <= limit && fabsf(v.y) <= limit &&
           fabsf(v.z) <= limit;
}

static bool positive(CF_V3 v)
{
    return v.x > 0 && v.y > 0 && v.z > 0;
}

/* An unset (zero) rotation reads as identity, as in transform propagation. */
static CF_Quat unit(CF_Quat q)
{
    return q.x || q.y || q.z || q.w ? cf_quat_norm(q) : cf_quat_identity();
}

static placement follower_placement(CF_M4x4 m)
{
    placement at = { .follower = true };
    cf_m4_decompose(m, &at.pose.position, &at.pose.rotation, &at.scale);
    return at;
}

static placement place(const ecs_world *world, ecs_entity entity)
{
    const GameTransform3D *local = GAME_GameTransform3D_get_const(world, entity);
    if (!game_transform_parent_3d(world, entity).slot) {
        return (placement){ { local->position, unit(local->rotation) }, local->scale, false };
    }
    GameGlobalTransform3D global = game_transform_world_3d(world, entity);
    const CF_V3 columns[] = { global.x_axis,
                              global.y_axis,
                              global.z_axis,
                              global.position };
    CF_M4x4 m = cf_m4_identity();
    for (int i = 0; i < 4; i++) {
        m.elements[i * 4] = columns[i].x;
        m.elements[i * 4 + 1] = columns[i].y;
        m.elements[i * 4 + 2] = columns[i].z;
    }
    return follower_placement(m);
}

/* Where a follower sits on its anchor's body: its own and its parents'
 * Transform3D below the anchor, after the anchor's scale. It stays put while
 * the anchor moves and turns. */
static placement carried_placement(const ecs_world *world, ecs_entity entity, ecs_entity anchor)
{
    CF_M4x4 m = cf_m4_identity();
    for (ecs_entity at = entity; !same_entity(at, anchor);
         at = game_transform_parent_3d(world, at)) {
        const GameTransform3D *local = GAME_GameTransform3D_get_const(world, at);
        m = cf_mul_m4(
            cf_m4_from_trs(local->position, unit(local->rotation), local->scale),
            m
        );
    }
    const GameTransform3D *top = GAME_GameTransform3D_get_const(world, anchor);
    m = cf_mul_m4(cf_m4_from_trs(cf_v3(0, 0, 0), cf_quat_identity(), top->scale), m);
    return follower_placement(m);
}

/* The capsule's radius and the half length of its segment, after scale. */
static void capsule_extent(const GameCollider3D *collider, CF_V3 scale, float *radius, float *half)
{
    *radius = collider->radius * fmaxf(fabsf(scale.x), fabsf(scale.z));
    *half = collider->height * fabsf(scale.y) / 2 - *radius;
}

static const char *invalid_shape(const GameCollider3D *collider, CF_V3 scale)
{
    float radius, half;
    scale = cf_abs_v3(scale);
    switch (collider->shape) {
        case GAME_COLLIDER3D_BOX:
            return positive(collider->size) &&
                           within(cf_mul_v3(collider->size, scale), FARTHEST)
                     ? NULL
                     : "A Collider3D box needs a positive size";
        case GAME_COLLIDER3D_SPHERE:
            radius = collider->radius * fmaxf(scale.x, fmaxf(scale.y, scale.z));
            return collider->radius > 0 && radius <= FARTHEST
                     ? NULL
                     : "A Collider3D sphere needs a positive radius";
        case GAME_COLLIDER3D_CAPSULE:
            capsule_extent(collider, scale, &radius, &half);
            return collider->radius > 0 && half >= 0 && radius + half <= FARTHEST ? NULL
                                                                                  : "A Collider3D capsule needs a positive radius and a height of at "
                                                                                    "least twice it after scale";
        case GAME_COLLIDER3D_HULL:
        case GAME_COLLIDER3D_MESH:
            /* Box3D raises a smaller scale to its least. */
            return collider->mesh && collider->mesh[0] &&
                           fminf(scale.x, fminf(scale.y, scale.z)) >= B3_MIN_SCALE
                     ? NULL
                     : "A Collider3D hull or mesh needs a mesh and a scale "
                       "of at least .01";
    }
    return "Unknown Collider3D shape";
}

static const char *invalid(placement at, const GameCollider3D *collider, const GameBody3D *body)
{
    CF_Quat q = at.pose.rotation;
    if (!within(at.pose.position, FARTHEST) || !isfinite(q.x + q.y + q.z + q.w)) {
        return "A 3D body needs a finite Transform3D within 100 km of the "
               "origin";
    }
    if (!positive(cf_abs_v3(at.scale)) || !finite_v3(at.scale)) {
        return "A 3D collider needs a finite, nonzero Transform3D scale";
    }
    if (!isfinite(collider->friction) || collider->friction < 0 ||
        !isfinite(collider->restitution) || collider->restitution < 0) {
        return "Collider3D friction and restitution must be finite and "
               "nonnegative";
    }
    if (body->type != GAME_BODY_STATIC && body->type != GAME_BODY_DYNAMIC &&
        body->type != GAME_BODY_KINEMATIC) {
        return "Unknown Body3D type";
    }
    if (!within(body->linear_velocity, FARTHEST) || !finite_v3(body->angular_velocity)) {
        return "Body3D velocities must be finite";
    }
    if (at.follower && body->type == GAME_BODY_DYNAMIC) {
        return "A dynamic Body3D must be placed in the world, not under a "
               "parent "
               "with a Transform3D";
    }
    /* Box3D's meshes collide only on static bodies, at the body's origin. */
    if (collider->shape == GAME_COLLIDER3D_MESH &&
        (at.follower || body->type != GAME_BODY_STATIC)) {
        return "A Collider3D mesh must be static and placed in the world, not "
               "under a parent with a Transform3D";
    }
    return invalid_shape(collider, at.scale);
}

static const char *invalid_character(placement at, const GameCollider3D *collider, const GameBody3D *body)
{
    if (body->type != GAME_BODY_KINEMATIC || at.follower) {
        return "A Character3D needs a kinematic Body3D placed in the world";
    }
    float radius, half;
    capsule_extent(collider, at.scale, &radius, &half);
    if (collider->shape != GAME_COLLIDER3D_CAPSULE || collider->sensor ||
        radius < CHARACTER_RADIUS) {
        return "A Character3D needs a solid capsule Collider3D at least two "
               "centimeters in radius";
    }
    return NULL;
}

static int fail(game_physics3d *physics, ecs_entity entity, const char *problem)
{
    snprintf(
        physics->error,
        sizeof physics->error,
        "%s on entity %u.",
        problem,
        entity.slot
    );
    return FLOW_ERR_ARGUMENT;
}

static void run_slot(int, int, uint32_t, void *user)
{
    task_slot *slot = user;
    slot->run(slot->context);
}

/* Box3D enqueues single tasks from the stepping thread, at most B3_MAX_TASKS
 * per step. Each slot keeps one alive until the step that queued it ends. */
static void *enqueue_task(b3TaskCallback *run, void *context, void *user, const char *)
{
    game_physics3d *physics = user;
    int index = physics->slot_count++;
    assert(index < B3_MAX_TASKS);
    physics->slots[index] = (task_slot){ run, context };
    return physics->tasks
        .enqueue(run_slot, 1, 1, &physics->slots[index], physics->tasks.user);
}

static void finish_task(void *task, void *user)
{
    game_physics3d *physics = user;
    physics->tasks.finish(task, physics->tasks.user);
}

static void create_world(game_physics3d *physics)
{
    b3WorldDef def = b3DefaultWorldDef();
    def.gravity = to_b3(physics->gravity);
    def.enableSleep = physics->settings.sleep;
    def.enableContinuous = physics->settings.continuous;
    def.contactHertz = physics->settings.contact_hertz;
    def.contactDampingRatio = physics->settings.contact_damping;
    if (physics->tasks.workers > 1) {
        def.workerCount = (uint32_t)physics->tasks.workers;
        def.enqueueTask = enqueue_task;
        def.finishTask = finish_task;
        def.userTaskContext = physics;
    }
    physics->simulation = b3CreateWorld(&def);
}

static void destroy_world(game_physics3d *physics)
{
    if (B3_IS_NON_NULL(physics->simulation)) {
        b3DestroyWorld(physics->simulation);
    }
    physics->simulation = (b3WorldId){ 0 };
    aclear(physics->bodies);
    physics->body_count = 0;
    aclear(physics->previous);
    aclear(physics->current);
    aclear(physics->pushes);
}

/* Box3D's unit configuration is process-wide, and precedes all defaults and
 * standalone queries. */
static void use_project_units()
{
    static bool initialized;
    if (!initialized) {
        b3SetLengthUnitsPerMeter(GAME_UNITS_PER_METER);
        initialized = true;
    }
}

static void free_meshes(game_physics3d *physics)
{
    for (int i = 0; i < acount(physics->meshes); i++) {
        collision_mesh *entry = &physics->meshes[i];
        free(entry->source);
        if (entry->hull) {
            b3DestroyHull(entry->hull);
        }
        if (entry->mesh) {
            b3DestroyMesh(entry->mesh);
        }
    }
    aclear(physics->meshes);
}

game_physics3d *game_physics3d_new(ecs_world *world, game_entities *entities)
{
    assert(world && entities);
    use_project_units();
    game_physics3d *physics = xcalloc(1, sizeof *physics);
    physics->world = world;
    physics->entities = entities;
    physics->settings = game_physics_defaults();
    physics->gravity = cf_v3(0, -EARTH_GRAVITY, 0);
    physics->pool = game_task_pool_acquire();
    physics->tasks = *game_task_pool_tasks(physics->pool);
    return physics;
}

void game_physics3d_free(game_physics3d *physics)
{
    destroy_world(physics);
    free_meshes(physics);
    afree(physics->meshes);
    afree(physics->bodies);
    afree(physics->previous);
    afree(physics->current);
    afree(physics->contacts);
    afree(physics->shapes);
    afree(physics->pushes);
    game_task_pool_release(physics->pool);
    free(physics);
}

void game_physics3d_reset(game_physics3d *physics)
{
    destroy_world(physics);
    free_meshes(physics);
    physics->error[0] = 0;
}

const char *game_physics3d_error(const game_physics3d *physics)
{
    return physics->error;
}

int game_physics3d_configure(game_physics3d *physics, game_physics_settings settings)
{
    if (!game_physics_settings_valid(settings)) {
        snprintf(physics->error, sizeof physics->error, "Invalid physics settings: workers 0..32, substeps 1..16, contact frequency 0..240 Hz, damping ratio 0..100.");
        return FLOW_ERR_ARGUMENT;
    }
    if (settings.workers != physics->settings.workers) {
        if (B3_IS_NON_NULL(physics->simulation)) {
            snprintf(physics->error, sizeof physics->error, "3D physics worker changes require a reset world.");
            return FLOW_ERR_ARGUMENT;
        }
        game_task_pool *previous = physics->pool;
        physics->pool = game_task_pool_acquire_workers(settings.workers);
        physics->tasks = *game_task_pool_tasks(physics->pool);
        game_task_pool_release(previous);
    }
    physics->settings = settings;
    if (B3_IS_NON_NULL(physics->simulation)) {
        b3World_EnableSleeping(physics->simulation, settings.sleep);
        b3World_EnableContinuous(physics->simulation, settings.continuous);
        b3World_SetContactTuning(
            physics->simulation,
            settings.contact_hertz,
            settings.contact_damping,
            b3DefaultWorldDef().contactSpeed
        );
    }
    physics->error[0] = 0;
    return FLOW_OK;
}

static b3BodyType body_type(GameBodyType type)
{
    switch (type) {
        case GAME_BODY_STATIC: return b3_staticBody;
        case GAME_BODY_DYNAMIC: return b3_dynamicBody;
        case GAME_BODY_KINEMATIC: return b3_kinematicBody;
    }
    assert(false);
    return b3_staticBody;
}

static bool wake(b3ShapeId shape, void *)
{
    b3BodyId body = b3Shape_GetBody(shape);
    if (b3Body_GetType(body) != b3_staticBody) {
        b3Body_SetAwake(body, true);
    }
    return true;
}

/* Box3D leaves sleeping bodies asleep when a shape appears or teleports into
 * them, so that wakes every body it overlaps, its own included. */
static void wake_overlapping(game_physics3d *physics, b3ShapeId shape)
{
    b3AABB bounds = b3Shape_GetAABB(shape);
    b3World_OverlapAABB(physics->simulation, bounds, b3DefaultQueryFilter(), wake, NULL);
}

/* Whether Box3D keeps one of the triangles. It drops those smaller than a
 * hundredth of its linear slop squared, and leaks a mesh it keeps none of. */
static bool keeps_triangle(const b3Vec3 *points, const int *indices)
{
    const float least = .01f * B3_LINEAR_SLOP * B3_LINEAR_SLOP;
    for (int i = 0; i + 2 < acount(indices); i += 3) {
        b3Vec3 a = points[indices[i]];
        b3Vec3 normal = b3Cross(
            b3Sub(points[indices[i + 1]], a),
            b3Sub(points[indices[i + 2]], a)
        );
        if (b3Length(normal) / 2 >= least) {
            return true;
        }
    }
    return false;
}

/* Builds the hull or triangle mesh that shape needs from the asset, or
 * returns why it can't. */
static const char *build(collision_mesh *entry, GameCollider3DShape shape, const render_mesh_asset *asset)
{
    int count = acount(asset->vertices);
    b3Vec3 *points = xmalloc((size_t)count * sizeof *points);
    for (int i = 0; i < count; i++) {
        points[i] = to_b3(asset->vertices[i].position);
    }
    const char *problem = NULL;
    if (shape == GAME_COLLIDER3D_HULL && !entry->hull) {
        entry->hull = b3CreateHull(points, count, HULL_VERTICES);
        problem = entry->hull ? NULL
                              : "A Collider3D hull needs a mesh whose vertices "
                                "enclose a volume";
    }
    if (shape == GAME_COLLIDER3D_MESH && !entry->mesh) {
        static_assert(sizeof(int) == sizeof(int32_t), "Mesh indices pass through");
        b3MeshDef def = { .vertices = points,
                          .indices = (int32_t *)asset->indices,
                          .vertexCount = count,
                          .weldTolerance = WELD_TOLERANCE,
                          .triangleCount = acount(asset->indices) / 3,
                          .weldVertices = true,
                          .identifyEdges = true };
        entry->mesh = keeps_triangle(points, asset->indices)
                        ? b3CreateMesh(&def, NULL, 0)
                        : NULL;
        problem = entry->mesh
                    ? NULL
                    : "A Collider3D mesh needs a mesh with a triangle "
                      "of nonzero area";
    }
    free(points);
    return problem;
}

static bool uses_mesh(const GameCollider3D *collider)
{
    return collider->shape == GAME_COLLIDER3D_HULL ||
           collider->shape == GAME_COLLIDER3D_MESH;
}

/* 1 + the collision mesh a hull or mesh collider at scale uses, built on
 * first use, or 0 for another shape. -1 when it can't, after writing why to
 * problem. */
static int collision(
    game_physics3d *physics,
    const GameCollider3D *collider,
    CF_V3 scale,
    char *problem,
    size_t size
)
{
    if (!uses_mesh(collider)) {
        return 0;
    }
    const render_mesh_asset *asset = render_mesh_asset_load(collider->mesh, NULL, 0);
    if (!asset) {
        snprintf(
            problem,
            size,
            "Collider3D mesh %s is not a prepared content .mesh",
            collider->mesh
        );
        return -1;
    }
    int found = 0;
    for (int i = 0; !found && i < acount(physics->meshes); i++) {
        const collision_mesh *entry = &physics->meshes[i];
        if (entry->identity == asset->identity &&
            !strcmp(entry->source, collider->mesh)) {
            found = i + 1;
        }
    }
    if (!found) {
        collision_mesh added = {
            .source = xstrdup(collider->mesh),
            .identity = asset->identity,
            .extent = cf_max_v3(cf_abs_v3(asset->min), cf_abs_v3(asset->max)),
        };
        apush(physics->meshes, added);
        found = acount(physics->meshes);
    }
    collision_mesh *entry = &physics->meshes[found - 1];
    const char *built = within(cf_mul_v3(entry->extent, cf_abs_v3(scale)), FARTHEST)
                          ? build(entry, collider->shape, asset)
                          : "A Collider3D hull or mesh must fit within 100 km "
                            "after scale";
    if (built) {
        snprintf(problem, size, "%s", built);
        return -1;
    }
    return found;
}

/* The collision mesh for the entity's collider, or -1 after failing. */
static int entity_collision(
    game_physics3d *physics,
    ecs_entity entity,
    const GameCollider3D *collider,
    CF_V3 scale
)
{
    char problem[160];
    int mesh = collision(physics, collider, scale, problem, sizeof problem);
    return mesh < 0 ? fail(physics, entity, problem) : mesh;
}

/* A collider's shape, scaled, at an offset in the frame of the body or query
 * that holds it. */
typedef struct
{
    GameCollider3DShape kind;
    union
    {
        b3BoxHull box;
        b3Sphere sphere;
        b3Capsule capsule;
        const b3HullData *hull; /* Its collision mesh's, or owned. */
        b3Mesh mesh;
    };
    b3HullData *owned; /* A transformed hull that release_geometry frees. */
} geometry;

/* A box, sphere or capsule collider's shape. */
static geometry make_geometry(const GameCollider3D *collider, CF_V3 scale, b3Transform offset)
{
    geometry out = { .kind = collider->shape };
    float radius, half;
    scale = cf_abs_v3(scale);
    switch (collider->shape) {
        case GAME_COLLIDER3D_BOX: {
            CF_V3 extent = cf_mul_v3_f(cf_mul_v3(collider->size, scale), .5f);
            out.box = b3MakeTransformedBoxHull(extent.x, extent.y, extent.z, offset);
            return out;
        }
        case GAME_COLLIDER3D_SPHERE:
            out.sphere = (b3Sphere){ offset.p,
                                     collider->radius *
                                         fmaxf(scale.x, fmaxf(scale.y, scale.z)) };
            return out;
        case GAME_COLLIDER3D_CAPSULE:
            capsule_extent(collider, scale, &radius, &half);
            out.capsule = (b3Capsule){
                b3TransformPoint(offset, (b3Vec3){ 0, -half, 0 }),
                b3TransformPoint(offset, (b3Vec3){ 0, half, 0 }),
                radius
            };
            return out;
        default: break;
    }
    assert(false);
    return out;
}

static void release_geometry(geometry *shape)
{
    if (shape->owned) {
        b3DestroyHull(shape->owned);
    }
}

/* A box's or hull's convex hull, or NULL for another shape. */
static const b3HullData *geometry_hull(const geometry *shape)
{
    switch (shape->kind) {
        case GAME_COLLIDER3D_BOX: return &shape->box.base;
        case GAME_COLLIDER3D_HULL: return shape->hull;
        default: return NULL;
    }
}

static const collision_mesh *mesh_data(const game_physics3d *physics, int mesh)
{
    return mesh ? &physics->meshes[mesh - 1] : NULL;
}

static b3ShapeId create_shape(
    const game_physics3d *physics,
    const body_record *record,
    b3BodyId body,
    b3Transform offset
)
{
    const GameCollider3D *collider = &record->collider;
    b3ShapeDef def = b3DefaultShapeDef();
    def.userData = (void *)(uintptr_t)record->entity.slot;
    def.baseMaterial.friction = collider->friction;
    def.baseMaterial.restitution = collider->restitution;
    def.filter.categoryBits = collider->category;
    def.filter.maskBits = collider->mask;
    def.isSensor = collider->sensor;
    def.enableSensorEvents = true;
    if (record->follower && collider->sensor) {
        def.density = 0; /* A carried sensor adds no mass. */
    }
    const collision_mesh *data = mesh_data(physics, record->mesh);
    switch (collider->shape) {
        case GAME_COLLIDER3D_HULL:
            /* Box3D shares the transformed hull between shapes that match. */
            return b3CreateTransformedHullShape(
                body,
                &def,
                data->hull,
                offset,
                to_b3(record->scale)
            );
        case GAME_COLLIDER3D_MESH:
            return b3CreateMeshShape(body, &def, data->mesh, to_b3(record->scale));
        default: break;
    }
    geometry shape = make_geometry(collider, record->scale, offset);
    if (record->character) {
        /* The mover leaves its capsule a slop deep in what it touches. A body
         * two slops narrower stops short of it, so it never shoves a dynamic
         * body it walks up to. */
        shape.capsule.radius -= 2 * B3_LINEAR_SLOP;
    }
    switch (shape.kind) {
        case GAME_COLLIDER3D_BOX:
            return b3CreateHullShape(body, &def, &shape.box.base);
        case GAME_COLLIDER3D_SPHERE:
            return b3CreateSphereShape(body, &def, &shape.sphere);
        case GAME_COLLIDER3D_CAPSULE:
            return b3CreateCapsuleShape(body, &def, &shape.capsule);
        default: break;
    }
    assert(false);
    return (b3ShapeId){ 0 };
}

/* Puts the record's collider at offset on body and wakes what it overlaps. */
static void add_shape(game_physics3d *physics, body_record *record, b3BodyId body, b3Transform offset)
{
    record->shape = create_shape(physics, record, body, offset);
    wake_overlapping(physics, record->shape);
}

/* A body of the record's own, with its collider at the body's origin. */
static void create_body(game_physics3d *physics, body_record *record, pose at)
{
    b3BodyDef def = b3DefaultBodyDef();
    /* A follower without an anchor moves only when its parents do. A static
     * and a kinematic body touch the same bodies. */
    def.type = record->follower ? b3_kinematicBody : body_type(record->type);
    def.position = to_b3(at.position);
    def.rotation = quat_to_b3(at.rotation);
    record->body = b3CreateBody(physics->simulation, &def);
    record->carrier = record->entity.slot;
    add_shape(physics, record, record->body, b3Transform_identity);
    record->start = record->end = at;
    physics->body_count++;
}

/* Destroying a body destroys the shapes of the followers it carries, which
 * the next placement puts back. */
static void destroy_body(game_physics3d *physics, body_record *record)
{
    if (b3Body_GetShapeCount(record->body) > 1) {
        for (int i = 0; i < acount(physics->bodies); i++) {
            body_record *carried = &physics->bodies[i];
            if (B3_IS_NULL(carried->body) && carried->carrier == record->entity.slot) {
                carried->shape = b3_nullShapeId;
            }
        }
    }
    b3DestroyBody(record->body);
    record->body = b3_nullBodyId;
    record->shape = b3_nullShapeId;
    physics->body_count--;
}

static void remove_record(game_physics3d *physics, body_record *record)
{
    if (B3_IS_NON_NULL(record->body)) {
        destroy_body(physics, record);
    } else if (B3_IS_NON_NULL(record->shape)) {
        b3DestroyShape(record->shape, true);
    }
    *record = (body_record){ 0 };
}

static bool same_collider(const GameCollider3D *a, const GameCollider3D *b)
{
    return a->shape == b->shape && same_v3(a->size, b->size) &&
           a->radius == b->radius && a->height == b->height &&
           a->category == b->category && a->mask == b->mask &&
           a->sensor == b->sensor && a->friction == b->friction &&
           a->restitution == b->restitution;
}

/* Box3D also leaves bodies asleep when what they rest on moves away, so a
 * teleport wakes the bodies touching the body where it was, then those its
 * shapes overlap where it lands. */
static void teleport(game_physics3d *physics, body_record *record, pose at)
{
    int capacity = b3Body_GetContactCapacity(record->body);
    asetlen(physics->contacts, capacity);
    int count = b3Body_GetContactData(record->body, physics->contacts, capacity);
    for (int i = 0; i < count; i++) {
        wake(physics->contacts[i].shapeIdA, NULL);
        wake(physics->contacts[i].shapeIdB, NULL);
    }
    b3Body_SetTransform(record->body, to_b3(at.position), quat_to_b3(at.rotation));
    asetlen(physics->shapes, b3Body_GetShapeCount(record->body));
    count = b3Body_GetShapes(record->body, physics->shapes, acount(physics->shapes));
    for (int i = 0; i < count; i++) {
        wake_overlapping(physics, physics->shapes[i]);
    }
}

/* A root teleports when something other than physics moved its Transform3D,
 * and a follower with a body of its own when its parents moved. */
static void move(game_physics3d *physics, body_record *record, placement at)
{
    if (at.follower) {
        if (!same_pose(at.pose, record->placed)) {
            teleport(physics, record, at.pose);
        }
        return;
    }
    const GameTransform3D
        *local = GAME_GameTransform3D_get_const(physics->world, record->entity);
    if (!same_pose(local_pose(local), local_pose(&record->written))) {
        teleport(physics, record, at.pose);
    }
}

static bool same_scale(CF_V3 a, CF_V3 b)
{
    CF_V3 change = cf_abs_v3(cf_sub_v3(a, b));
    CF_V3 limit = cf_mul_v3_f(cf_abs_v3(b), SCALE_TOLERANCE);
    return change.x <= limit.x && change.y <= limit.y && change.z <= limit.z;
}

/* Kinematic and dynamic roots take their authored velocities when they
 * change; dynamic ones are written back after each step. */
static void set_velocities(body_record *record, const GameBody3D *body)
{
    if (record->type == GAME_BODY_STATIC || record->follower) {
        return;
    }
    if (!same_v3(body->linear_velocity, record->linear_velocity)) {
        b3Body_SetLinearVelocity(record->body, to_b3(body->linear_velocity));
        record->linear_velocity = body->linear_velocity;
    }
    if (!same_v3(body->angular_velocity, record->angular_velocity)) {
        b3Body_SetAngularVelocity(record->body, to_b3(body->angular_velocity));
        record->angular_velocity = body->angular_velocity;
    }
}

/* Gives the record a body of its own at the placement, with a new shape when
 * its collider, collision mesh or scale changed. */
static void own_body(
    game_physics3d *physics,
    body_record *record,
    const GameCollider3D *collider,
    int mesh,
    placement at
)
{
    bool reshaped = !same_collider(collider, &record->collider) ||
                    record->mesh != mesh || !same_scale(at.scale, record->scale);
    record->collider = *collider;
    record->mesh = mesh;
    record->scale = at.scale;
    if (B3_IS_NULL(record->body)) {
        if (B3_IS_NON_NULL(record->shape)) {
            b3DestroyShape(record->shape, true);
        }
        create_body(physics, record, at.pose);
        return;
    }
    if (reshaped) {
        b3DestroyShape(record->shape, true);
        add_shape(physics, record, record->body, b3Transform_identity);
    }
    move(physics, record, at);
}

static int sync_entity(game_physics3d *physics, ecs_entity entity)
{
    ecs_world *world = physics->world;
    const GameCollider3D *collider = GAME_GameCollider3D_get_const(world, entity);
    const GameBody3D *authored = GAME_GameBody3D_get_const(world, entity);
    GameBody3D body = authored ? *authored : (GameBody3D){ .type = GAME_BODY_STATIC };
    placement at = place(world, entity);
    bool character = GAME_GameCharacter3D_get_const(world, entity) != NULL;
    const char *problem = invalid(at, collider, &body);
    if (!problem && character) {
        problem = invalid_character(at, collider, &body);
    }
    if (problem) {
        return fail(physics, entity, problem);
    }
    int mesh = at.follower ? 0 : entity_collision(physics, entity, collider, at.scale);
    if (mesh < 0) {
        return FLOW_ERR_ARGUMENT;
    }
    if (B3_IS_NULL(physics->simulation)) {
        create_world(physics);
    }
    if (entity.slot >= (unsigned)acount(physics->bodies)) {
        int previous = acount(physics->bodies);
        asetlen(physics->bodies, (int)entity.slot + 1);
        memset(
            physics->bodies + previous,
            0,
            (size_t)(acount(physics->bodies) - previous) * sizeof *physics->bodies
        );
    }
    body_record *record = &physics->bodies[entity.slot];
    if (record->entity.slot &&
        (!same_entity(record->entity, entity) || record->type != body.type ||
         record->follower != at.follower || record->character != character)) {
        remove_record(physics, record);
    }
    if (!record->entity.slot) {
        *record = (body_record){ .entity = entity,
                                 .type = body.type,
                                 .follower = at.follower,
                                 .character = character };
    }
    record->seen = true;
    if (at.follower) {
        return FLOW_OK; /* Placed once every root has its body. */
    }
    own_body(physics, record, collider, mesh, at);
    record->written = *GAME_GameTransform3D_get_const(world, entity);
    record->placed = at.pose;
    if (character) {
        /* The mover sweeps the capsule as it stands, so it never spins. */
        body.angular_velocity = (CF_V3){ 0 };
        GAME_GameBody3D_get(world, entity)->angular_velocity = body.angular_velocity;
    }
    set_velocities(record, &body);
    return FLOW_OK;
}

/* The body a follower rides on: its nearest ancestor with a body that follows
 * nothing, or NULL when none places it. */
static const body_record *anchor(const game_physics3d *physics, ecs_entity entity)
{
    for (ecs_entity at = game_transform_parent_3d(physics->world, entity); at.slot;
         at = game_transform_parent_3d(physics->world, at)) {
        const body_record *record = at.slot < (unsigned)acount(physics->bodies)
                                      ? &physics->bodies[at.slot]
                                      : NULL;
        if (record && B3_IS_NON_NULL(record->body) && !record->follower &&
            same_entity(record->entity, at)) {
            return record;
        }
    }
    return NULL;
}

/* Puts the follower's shape on its anchor's body at its carried placement,
 * so the solver moves it with that body and its hits act on it. A changed
 * placement gets a new shape. */
static void carry(game_physics3d *physics, body_record *record, const body_record *with, int mesh, placement at)
{
    const GameCollider3D
        *collider = GAME_GameCollider3D_get_const(physics->world, record->entity);
    if (B3_IS_NON_NULL(record->body)) {
        destroy_body(physics, record);
    }
    if (B3_IS_NON_NULL(record->shape) && record->carrier == with->entity.slot &&
        same_collider(collider, &record->collider) && record->mesh == mesh &&
        same_pose(at.pose, record->placed) && same_scale(at.scale, record->scale)) {
        return;
    }
    if (B3_IS_NON_NULL(record->shape)) {
        b3DestroyShape(record->shape, true);
    }
    record->carrier = with->entity.slot;
    record->collider = *collider;
    record->mesh = mesh;
    record->scale = at.scale;
    record->placed = at.pose;
    add_shape(
        physics,
        record,
        with->body,
        (b3Transform){ to_b3(at.pose.position), quat_to_b3(at.pose.rotation) }
    );
}

/* Each follower rides on its anchor's body. One without an anchor has a
 * kinematic body of its own that its parents place. The mover moves only a
 * character's capsule, so a character carries only sensors. */
static int place_followers(game_physics3d *physics)
{
    for (int i = 0; i < acount(physics->bodies); i++) {
        body_record *record = &physics->bodies[i];
        if (!record->entity.slot || !record->follower) {
            continue;
        }
        const GameCollider3D
            *collider = GAME_GameCollider3D_get_const(physics->world, record->entity);
        const body_record *with = anchor(physics, record->entity);
        if (with && with->character && !collider->sensor) {
            return fail(physics, record->entity, "A Collider3D under a Character3D must be a sensor");
        }
        placement at = with
                         ? carried_placement(physics->world, record->entity, with->entity)
                         : place(physics->world, record->entity);
        int mesh = entity_collision(physics, record->entity, collider, at.scale);
        if (mesh < 0) {
            return FLOW_ERR_ARGUMENT;
        }
        if (with) {
            carry(physics, record, with, mesh, at);
            continue;
        }
        own_body(physics, record, collider, mesh, at);
        record->placed = at.pose;
    }
    return FLOW_OK;
}

static int gravity(game_physics3d *physics, CF_V3 *out)
{
    *out = cf_v3(0, -EARTH_GRAVITY, 0);
    ecs_component_id id = GAME_COMPONENT_GameGravity3D.id;
    if (!ecs_world_component(physics->world, id)) {
        return FLOW_OK;
    }
    ecs_query query;
    int result = ecs_query_begin(physics->world, &query, &id, 1);
    if (result != ECS_OK) {
        return result;
    }
    int count = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        *out = GAME_GameGravity3D_get_const(physics->world, entity)->acceleration;
        count++;
    }
    ecs_query_end(&query);
    if (count > 1) {
        snprintf(physics->error, sizeof physics->error, "Only one entity may have Gravity3D; %d do.", count);
        return FLOW_ERR_ARGUMENT;
    }
    if (!within(*out, FARTHEST)) {
        snprintf(physics->error, sizeof physics->error, "Gravity3D acceleration must be finite.");
        return FLOW_ERR_ARGUMENT;
    }
    return FLOW_OK;
}

static bool synced(const game_physics3d *physics, ecs_entity entity)
{
    return entity.slot < (unsigned)acount(physics->bodies) &&
           same_entity(physics->bodies[entity.slot].entity, entity) &&
           physics->bodies[entity.slot].seen;
}

/* The collider query skips a character without a Transform3D or Collider3D. */
static int check_characters(game_physics3d *physics)
{
    ecs_component_id id = GAME_COMPONENT_GameCharacter3D.id;
    if (!ecs_world_component(physics->world, id)) {
        return FLOW_OK;
    }
    ecs_query query;
    int result = ecs_query_begin(physics->world, &query, &id, 1);
    if (result != ECS_OK) {
        return result;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (!game_entities_is_retiring(physics->entities, entity) &&
            !synced(physics, entity)) {
            result = fail(physics, entity, "A Character3D needs a Transform3D and a Collider3D");
            break;
        }
    }
    ecs_query_end(&query);
    return result;
}

static int synchronize(game_physics3d *physics)
{
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics->bodies[i].seen = false;
    }
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform3D.id,
                                    GAME_COMPONENT_GameCollider3D.id };
    int result = FLOW_OK;
    if (ecs_world_component(physics->world, required[0]) &&
        ecs_world_component(physics->world, required[1])) {
        ecs_query query;
        result = ecs_query_begin(physics->world, &query, required, 2);
        if (result != ECS_OK) {
            return result;
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (game_entities_is_retiring(physics->entities, entity)) {
                continue;
            }
            if ((result = sync_entity(physics, entity)) < 0) {
                break;
            }
        }
        ecs_query_end(&query);
    }
    if (result < 0 || (result = check_characters(physics)) < 0) {
        return result;
    }
    for (int i = 0; i < acount(physics->bodies); i++) {
        body_record *record = &physics->bodies[i];
        if (record->entity.slot && !record->seen) {
            remove_record(physics, record);
        }
    }
    return place_followers(physics);
}

static bool moving_root(const body_record *record)
{
    return B3_IS_NON_NULL(record->body) && record->type != GAME_BODY_STATIC &&
           !record->follower;
}

static pose body_pose(b3BodyId body)
{
    b3WorldTransform transform = b3Body_GetTransform(body);
    return (pose){ from_b3(transform.p), quat_from_b3(transform.q) };
}

static void write_back(game_physics3d *physics)
{
    for (int i = 0; i < acount(physics->bodies); i++) {
        body_record *record = &physics->bodies[i];
        if (!moving_root(record)) {
            continue;
        }
        record->end = body_pose(record->body);
        GameTransform3D *local = GAME_GameTransform3D_get(physics->world, record->entity);
        local->position = record->end.position;
        local->rotation = record->end.rotation;
        record->written = *local;
        if (record->type != GAME_BODY_DYNAMIC) {
            continue;
        }
        GameBody3D *body = GAME_GameBody3D_get(physics->world, record->entity);
        body->linear_velocity = from_b3(b3Body_GetLinearVelocity(record->body));
        body->angular_velocity = from_b3(b3Body_GetAngularVelocity(record->body));
        body->mass = b3Body_GetMass(record->body);
        record->linear_velocity = body->linear_velocity;
        record->angular_velocity = body->angular_velocity;
    }
}

static uint64_t shape_entity(const game_physics3d *physics, b3ShapeId shape)
{
    uintptr_t slot = (uintptr_t)b3Shape_GetUserData(shape);
    return ecs_entity_to_u64(physics->bodies[slot].entity);
}

static void add_pair(game_physics3d *physics, uint64_t a, uint64_t b, CF_V3 point)
{
    apush(
        physics->current,
        ((contact_pair){ .first = a < b ? a : b, .second = a < b ? b : a, .point = point })
    );
}

/* The average of the contact's manifold points, or false while it is not
 * touching. */
static bool contact_point(const b3ContactData *contact, CF_V3 *out)
{
    CF_V3 center = from_b3(b3Body_GetWorldCenterOfMass(b3Shape_GetBody(contact->shapeIdA)));
    CF_V3 sum = { 0 };
    int count = 0;
    for (int i = 0; i < contact->manifoldCount; i++) {
        const b3Manifold *manifold = &contact->manifolds[i];
        for (int j = 0; j < manifold->pointCount; j++) {
            sum = cf_add_v3(sum, from_b3(manifold->points[j].anchorA));
            count++;
        }
    }
    if (!count) {
        return false;
    }
    *out = cf_add_v3(center, cf_div_v3_f(sum, (float)count));
    return true;
}

static void collect_contacts(game_physics3d *physics, const body_record *record)
{
    int capacity = b3Body_GetContactCapacity(record->body);
    asetlen(physics->contacts, capacity);
    int count = b3Body_GetContactData(record->body, physics->contacts, capacity);
    for (int i = 0; i < count; i++) {
        const b3ContactData *contact = &physics->contacts[i];
        CF_V3 point;
        if (contact_point(contact, &point)) {
            add_pair(
                physics,
                shape_entity(physics, contact->shapeIdA),
                shape_entity(physics, contact->shapeIdB),
                point
            );
        }
    }
}

/* A sensor overlap reports the center of the two shapes' overlapping bounds. */
static void collect_visitors(game_physics3d *physics, const body_record *record)
{
    int capacity = b3Shape_GetSensorCapacity(record->shape);
    asetlen(physics->shapes, capacity);
    int count = b3Shape_GetSensorData(record->shape, physics->shapes, capacity);
    b3AABB sensor = b3Shape_GetAABB(record->shape);
    for (int i = 0; i < count; i++) {
        b3ShapeId visitor = physics->shapes[i];
        assert(b3Shape_IsValid(visitor)); /* Nothing is destroyed after the step. */
        b3AABB other = b3Shape_GetAABB(visitor);
        CF_V3 low = cf_max_v3(from_b3(sensor.lowerBound), from_b3(other.lowerBound));
        CF_V3 high = cf_min_v3(from_b3(sensor.upperBound), from_b3(other.upperBound));
        add_pair(
            physics,
            ecs_entity_to_u64(record->entity),
            shape_entity(physics, visitor),
            cf_mul_v3_f(cf_add_v3(low, high), .5f)
        );
    }
}

static int compare_pairs(const void *left, const void *right)
{
    const contact_pair *a = left, *b = right;
    if (a->first != b->first) {
        return (a->first > b->first) - (a->first < b->first);
    }
    return (a->second > b->second) - (a->second < b->second);
}

/* Both contacting bodies list a contact, so each pair appears once or twice. */
static void current_pairs(game_physics3d *physics)
{
    aclear(physics->current);
    for (int i = 0; i < acount(physics->bodies); i++) {
        const body_record *record = &physics->bodies[i];
        if (!record->entity.slot) {
            continue;
        }
        if (record->collider.sensor) {
            collect_visitors(physics, record);
        }
        if (B3_IS_NON_NULL(record->body) &&
            b3Body_GetType(record->body) != b3_staticBody) {
            collect_contacts(physics, record);
        }
    }
    asort(physics->current, compare_pairs);
    int count = acount(physics->current);
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (!kept || compare_pairs(&physics->current[kept - 1], &physics->current[i])) {
            physics->current[kept++] = physics->current[i];
        }
    }
    asetlen(physics->current, kept);
}

static int queue(game_physics3d *physics, game_host_event event, const contact_pair *pair)
{
    const uint64_t targets[] = { pair->first, pair->second };
    for (int i = 0; i < 2; i++) {
        pin_value values[] = {
            { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = targets[1 - i] },
            { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = (void *)&pair->point },
        };
        int result = game_entities_request_event(
            physics->entities,
            ecs_entity_from_u64(targets[i]),
            game_host_event_name(event),
            (execution_arguments){
                .values = values,
                .count = event == GAME_HOST_EVENT_CONTACT_BEGIN_3D ? 2 : 1 }
        );
        if (result < 0) {
            return result;
        }
    }
    return FLOW_OK;
}

/* Pairs that ended, then pairs that began, each in pair order. */
static int dispatch_pairs(game_physics3d *physics)
{
    const contact_pair *before = physics->previous, *after = physics->current;
    int before_count = acount(before), after_count = acount(after);
    for (int i = 0, j = 0; i < before_count; i++) {
        while (j < after_count && compare_pairs(&after[j], &before[i]) < 0) {
            j++;
        }
        int result = j < after_count && !compare_pairs(&after[j], &before[i])
                       ? FLOW_OK
                       : queue(physics, GAME_HOST_EVENT_CONTACT_END_3D, &before[i]);
        if (result < 0) {
            return result;
        }
    }
    for (int i = 0, j = 0; i < after_count; i++) {
        while (j < before_count && compare_pairs(&before[j], &after[i]) < 0) {
            j++;
        }
        int result = j < before_count && !compare_pairs(&before[j], &after[i])
                       ? FLOW_OK
                       : queue(physics, GAME_HOST_EVENT_CONTACT_BEGIN_3D, &after[i]);
        if (result < 0) {
            return result;
        }
    }
    contact_pair *swap = physics->previous;
    physics->previous = physics->current;
    physics->current = swap;
    return FLOW_OK;
}

/* Box3D leaves sleeping bodies asleep through a gravity change. */
static void set_gravity(game_physics3d *physics, CF_V3 acceleration)
{
    b3World_SetGravity(physics->simulation, to_b3(acceleration));
    physics->gravity = acceleration;
    for (int i = 0; i < acount(physics->bodies); i++) {
        const body_record *record = &physics->bodies[i];
        if (B3_IS_NON_NULL(record->body) && record->type != GAME_BODY_STATIC) {
            b3Body_SetAwake(record->body, true);
        }
    }
}

/* The most planes a character slides along at once. */
#define CHARACTER_PLANES 32
/* Box3D's mover runs at most this many cast and slide passes per step, and
 * stops early once a pass moves less than the tolerance. The planes touching
 * the capsule where it stops find the floor, and those that hold back the rest
 * of its motion clip its velocity. */
#define CHARACTER_PASSES 5
static const float CHARACTER_TOLERANCE = .01f * GAME_UNITS_PER_METER;
/* The cosine of the steepest floor, 45 degrees. */
static const float FLOOR_COSINE = .70710678f;

/* What a character touches: solid colliders on other bodies. */
typedef struct
{
    b3BodyId self;
    b3CollisionPlane planes[CHARACTER_PLANES];
    int count;
} character_contact;

static bool blocks(b3ShapeId shape, void *context)
{
    const character_contact *contact = context;
    return !b3Shape_IsSensor(shape) &&
           !B3_ID_EQUALS(b3Shape_GetBody(shape), contact->self);
}

static bool gather_planes(b3ShapeId shape, const b3PlaneResult *planes, int count, void *context)
{
    character_contact *contact = context;
    if (!blocks(shape, contact)) {
        return true;
    }
    for (int i = 0; i < count && contact->count < CHARACTER_PLANES; i++) {
        contact->planes[contact->count++] = (b3CollisionPlane){ planes[i].plane,
                                                                FLT_MAX,
                                                                0,
                                                                true };
    }
    return true;
}

/* What the character stands on: the plane it touches that faces up the most,
 * when that is within 45 degrees of up. */
static GameCharacter3D stand(const character_contact *contact, b3Vec3 up)
{
    GameCharacter3D out = { 0 };
    float best = FLOOR_COSINE;
    for (int i = 0; i < contact->count; i++) {
        b3Vec3 normal = contact->planes[i].plane.normal;
        if (b3Dot(normal, up) >= best) {
            best = b3Dot(normal, up);
            out = (GameCharacter3D){ true, from_b3(normal) };
        }
    }
    return out;
}

/* Moves the character's capsule by its velocity over delta with Box3D's
 * mover, puts its body where the capsule ends, and writes what it stands on. */
static void move_character(game_physics3d *physics, body_record *record, float delta, b3Vec3 up)
{
    GameBody3D *body = GAME_GameBody3D_get(physics->world, record->entity);
    pose at = body_pose(record->body);
    b3Vec3 origin = to_b3(at.position);
    float radius, half;
    capsule_extent(&record->collider, record->scale, &radius, &half);
    b3Vec3 axis = to_b3(cf_mul_q_v3(at.rotation, cf_v3(0, half, 0)));
    b3Capsule local = { b3Neg(axis), axis, radius };
    b3QueryFilter filter = { .categoryBits = record->collider.category,
                             .maskBits = record->collider.mask };
    character_contact contact = { .self = record->body };
    b3Vec3 target = b3MulSV(delta, to_b3(body->linear_velocity)),
           moved = b3Vec3_zero, step = b3Vec3_zero;
    for (int pass = 0;; pass++) {
        b3Capsule mover = { b3Add(local.center1, moved),
                            b3Add(local.center2, moved),
                            radius };
        contact.count = 0;
        b3World_CollideMover(physics->simulation, origin, &mover, filter, gather_planes, &contact);
        b3PlaneSolverResult
            solved = b3SolvePlanes(b3Sub(target, moved), contact.planes, contact.count);
        if (pass == CHARACTER_PASSES ||
            (pass > 0 &&
             b3LengthSquared(step) < CHARACTER_TOLERANCE * CHARACTER_TOLERANCE)) {
            break;
        }
        float fraction = b3World_CastMover(
            physics->simulation,
            origin,
            &mover,
            solved.delta,
            filter,
            blocks,
            &contact
        );
        step = b3MulSV(fraction, solved.delta);
        moved = b3Add(moved, step);
    }
    body->linear_velocity = from_b3(
        b3ClipVector(to_b3(body->linear_velocity), contact.planes, contact.count)
    );
    record->linear_velocity = body->linear_velocity;
    b3Body_SetTransform(record->body, b3Add(origin, moved), quat_to_b3(at.rotation));
    *GAME_GameCharacter3D_get(physics->world, record->entity) = stand(&contact, up);
}

static bool moving_character(const body_record *record)
{
    return record->character && B3_IS_NON_NULL(record->body);
}

/* Moves every character before the step, one after another so each meets the
 * characters before it where they end up. Then each goes back to where the
 * step starts it, with the velocity that carries it to its end over the step.
 * Up opposes gravity. */
static void move_characters(game_physics3d *physics, float delta, CF_V3 acceleration)
{
    b3Vec3 up = b3LengthSquared(to_b3(acceleration)) > 0
                  ? b3Normalize(b3Neg(to_b3(acceleration)))
                  : (b3Vec3){ 0, 1, 0 };
    for (int i = 0; i < acount(physics->bodies); i++) {
        body_record *record = &physics->bodies[i];
        if (moving_character(record)) {
            record->start = body_pose(record->body);
            move_character(physics, record, delta, up);
        }
    }
    for (int i = 0; i < acount(physics->bodies); i++) {
        body_record *record = &physics->bodies[i];
        if (!moving_character(record)) {
            continue;
        }
        CF_V3 moved = cf_sub_v3(body_pose(record->body).position, record->start.position);
        b3Body_SetTransform(
            record->body,
            to_b3(record->start.position),
            quat_to_b3(record->start.rotation)
        );
        b3Body_SetLinearVelocity(record->body, b3MulSV(1 / delta, to_b3(moved)));
    }
}

static void step(game_physics3d *physics, float delta)
{
    for (int i = 0; i < acount(physics->bodies); i++) {
        body_record *record = &physics->bodies[i];
        if (moving_root(record)) {
            record->start = body_pose(record->body);
        }
    }
    physics->slot_count = 0;
    game_task_pool_begin(physics->pool, physics->body_count >= PARALLEL_BODY_MINIMUM);
    b3World_Step(physics->simulation, delta, physics->settings.substeps);
    game_task_pool_end(physics->pool);
}

/* Applies the queued pushes to the dynamic bodies they still name. */
static void apply_pushes(game_physics3d *physics)
{
    for (int i = 0; i < acount(physics->pushes); i++) {
        push queued = physics->pushes[i];
        unsigned slot = queued.entity.slot;
        const body_record *record = slot < (unsigned)acount(physics->bodies)
                                      ? &physics->bodies[slot]
                                      : NULL;
        if (!record || !same_entity(record->entity, queued.entity) ||
            record->type != GAME_BODY_DYNAMIC || B3_IS_NULL(record->body)) {
            continue;
        }
        if (queued.impulse) {
            b3Body_ApplyLinearImpulseToCenter(record->body, to_b3(queued.amount), true);
            continue;
        }
        b3Body_ApplyForceToCenter(record->body, to_b3(queued.amount), true);
    }
    aclear(physics->pushes);
}

int game_physics3d_tick(game_physics3d *physics, float delta_seconds)
{
    physics->error[0] = 0;
    if (!isfinite(delta_seconds) || delta_seconds < 0) {
        snprintf(physics->error, sizeof physics->error, "Physics delta must be finite and nonnegative.");
        return FLOW_ERR_ARGUMENT;
    }
    CF_V3 acceleration;
    int result = gravity(physics, &acceleration);
    if (result < 0 || (result = synchronize(physics)) < 0) {
        return result;
    }
    if (B3_IS_NULL(physics->simulation)) {
        aclear(physics->pushes); /* No collider, so no body to push. */
        return FLOW_OK;
    }
    if (!same_v3(acceleration, physics->gravity)) {
        set_gravity(physics, acceleration);
    }
    if (delta_seconds > 0) {
        move_characters(physics, delta_seconds, acceleration);
        apply_pushes(physics);
    }
    step(physics, delta_seconds);
    write_back(physics);
    current_pairs(physics);
    return dispatch_pairs(physics);
}

static int queue_push(game_physics3d *physics, ecs_entity entity, CF_V3 amount, bool impulse)
{
    const GameBody3D *body = ecs_entity_is_alive(physics->world, entity)
                               ? GAME_GameBody3D_get_const(physics->world, entity)
                               : NULL;
    if (!body || body->type != GAME_BODY_DYNAMIC ||
        !isfinite(amount.x + amount.y + amount.z)) {
        return FLOW_ERR_ARGUMENT;
    }
    apush(physics->pushes, ((push){ entity, amount, impulse }));
    return FLOW_OK;
}

int game_physics3d_apply_impulse(game_physics3d *physics, ecs_entity entity, CF_V3 impulse)
{
    return queue_push(physics, entity, impulse, true);
}

int game_physics3d_apply_force(game_physics3d *physics, ecs_entity entity, CF_V3 force)
{
    return queue_push(physics, entity, force, false);
}

bool game_physics3d_interpolation(
    const game_physics3d *physics,
    ecs_entity entity,
    float alpha,
    CF_Quat *rotation,
    CF_V3 *translation
)
{
    assert(rotation && translation && isfinite(alpha));
    /* Records last until the next step, past the entity or its Transform3D. */
    if (!entity.slot || entity.slot >= (unsigned)acount(physics->bodies) ||
        !ecs_entity_is_alive(physics->world, entity) ||
        game_entities_is_retiring(physics->entities, entity)) {
        return false;
    }
    const body_record *record = &physics->bodies[entity.slot];
    const GameTransform3D *local = GAME_GameTransform3D_get_const(physics->world, entity);
    if (!moving_root(record) || !same_entity(record->entity, entity) || !local) {
        return false;
    }
    if (!same_pose(local_pose(local), local_pose(&record->written))) {
        *rotation = cf_quat_identity();
        *translation = cf_v3(0, 0, 0);
        return true;
    }
    float t = fminf(1, fmaxf(0, alpha));
    CF_Quat shown = cf_quat_nlerp(record->start.rotation, record->end.rotation, t);
    *rotation = cf_mul_q(shown, cf_quat_conjugate(record->end.rotation));
    *translation = cf_sub_v3(
        cf_lerp_v3(record->start.position, record->end.position, t),
        cf_mul_q_v3(*rotation, record->end.position)
    );
    return true;
}

/* A collider where a query finds it. */
typedef struct
{
    ecs_entity entity;
    ecs_entity owner; /* The entity whose body holds it. */
    GameCollider3D collider;
    int mesh; /* 1 + its collision mesh, or 0. */
    b3Transform transform;
    CF_V3 scale;
} query_shape;

/* A convex shape's points and radius in the world, for casting and overlap. */
typedef struct
{
    b3Vec3 points[HULL_VERTICES];
    int count;
    float radius;
} query_proxy;

static bool live_collider(const game_physics3d *physics, ecs_entity entity)
{
    const ecs_world *world = physics->world;
    return ecs_entity_is_alive(world, entity) &&
           !game_entities_is_retiring(physics->entities, entity) &&
           GAME_GameTransform3D_get_const(world, entity) &&
           GAME_GameCollider3D_get_const(world, entity);
}

/* The top of the entity's Transform3D hierarchy carries it when that has a
 * collider, as anchor() does in the simulation; otherwise it has its own. */
static ecs_entity body_owner(const game_physics3d *physics, ecs_entity entity)
{
    ecs_entity top = entity;
    for (ecs_entity at = game_transform_parent_3d(physics->world, entity); at.slot;
         at = game_transform_parent_3d(physics->world, at)) {
        top = at;
    }
    return live_collider(physics, top) ? top : entity;
}

static bool read_shape(game_physics3d *physics, ecs_entity entity, query_shape *out)
{
    const GameCollider3D *collider = GAME_GameCollider3D_get_const(physics->world, entity);
    placement at = place(physics->world, entity);
    if (invalid(at, collider, &(GameBody3D){ .type = GAME_BODY_STATIC })) {
        return false;
    }
    int mesh = collision(physics, collider, at.scale, NULL, 0);
    *out = (query_shape){ entity,
                          body_owner(physics, entity),
                          *collider,
                          mesh,
                          { to_b3(at.pose.position), quat_to_b3(at.pose.rotation) },
                          at.scale };
    return mesh >= 0;
}

/* Every live collider, or FLOW_ERR_ARGUMENT at the first invalid one. */
static int read_shapes(game_physics3d *physics, query_shape **out)
{
    ecs_world *world = physics->world;
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform3D.id,
                                    GAME_COMPONENT_GameCollider3D.id };
    if (!ecs_world_component(world, required[0]) ||
        !ecs_world_component(world, required[1])) {
        return FLOW_OK;
    }
    ecs_query query;
    int result = ecs_query_begin(world, &query, required, 2);
    if (result != ECS_OK) {
        return result;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (game_entities_is_retiring(physics->entities, entity)) {
            continue;
        }
        query_shape shape;
        if (!read_shape(physics, entity, &shape)) {
            result = FLOW_ERR_ARGUMENT;
            break;
        }
        apush(*out, shape);
    }
    ecs_query_end(&query);
    return result;
}

/* The shape in its collider's frame, for release_geometry to free. */
static geometry query_geometry(const game_physics3d *physics, const query_shape *shape)
{
    const collision_mesh *data = mesh_data(physics, shape->mesh);
    geometry out = { .kind = shape->collider.shape };
    switch (out.kind) {
        case GAME_COLLIDER3D_HULL:
            /* Box3D bakes a scale into a copy of the hull. */
            out.hull = data->hull;
            if (!same_v3(shape->scale, (CF_V3){ 1, 1, 1 })) {
                out.hull = out.owned = b3CloneAndTransformHull(
                    data->hull,
                    b3Transform_identity,
                    to_b3(shape->scale)
                );
                assert(out.hull);
            }
            return out;
        case GAME_COLLIDER3D_MESH:
            out.mesh = (b3Mesh){ data->mesh, to_b3(shape->scale) };
            return out;
        default:
            return make_geometry(&shape->collider, shape->scale, b3Transform_identity);
    }
}

/* The shape in the world, or false for a mesh, which isn't convex. */
static bool world_proxy(const game_physics3d *physics, const query_shape *shape, query_proxy *out)
{
    if (shape->collider.shape == GAME_COLLIDER3D_MESH) {
        return false;
    }
    geometry local = query_geometry(physics, shape);
    const b3HullData *hull = geometry_hull(&local);
    *out = (query_proxy){ 0 };
    if (hull) {
        out->count = hull->vertexCount;
        assert(out->count <= HULL_VERTICES);
        memcpy(
            out->points,
            b3GetHullPoints(hull),
            (size_t)out->count * sizeof *out->points
        );
    }
    if (local.kind == GAME_COLLIDER3D_SPHERE) {
        out->points[0] = local.sphere.center;
        out->count = 1;
        out->radius = local.sphere.radius;
    }
    if (local.kind == GAME_COLLIDER3D_CAPSULE) {
        out->points[0] = local.capsule.center1;
        out->points[1] = local.capsule.center2;
        out->count = 2;
        out->radius = local.capsule.radius;
    }
    release_geometry(&local);
    for (int i = 0; i < out->count; i++) {
        out->points[i] = b3TransformPoint(shape->transform, out->points[i]);
    }
    return true;
}

static b3CastOutput ray_cast(const game_physics3d *physics, const query_shape *shape, const b3RayCastInput *input)
{
    geometry local = query_geometry(physics, shape);
    b3CastOutput out = { 0 };
    switch (local.kind) {
        case GAME_COLLIDER3D_BOX:
        case GAME_COLLIDER3D_HULL:
            out = b3RayCastHull(geometry_hull(&local), input);
            break;
        case GAME_COLLIDER3D_SPHERE:
            out = b3RayCastSphere(&local.sphere, input);
            break;
        case GAME_COLLIDER3D_CAPSULE:
            out = b3RayCastCapsule(&local.capsule, input);
            break;
        case GAME_COLLIDER3D_MESH:
            out = b3RayCastMesh(&local.mesh, input);
            break;
    }
    release_geometry(&local);
    return out;
}

static b3CastOutput shape_cast(
    const game_physics3d *physics,
    const query_shape *shape,
    const b3ShapeCastInput *input
)
{
    geometry local = query_geometry(physics, shape);
    b3CastOutput out = { 0 };
    switch (local.kind) {
        case GAME_COLLIDER3D_BOX:
        case GAME_COLLIDER3D_HULL:
            out = b3ShapeCastHull(geometry_hull(&local), input);
            break;
        case GAME_COLLIDER3D_SPHERE:
            out = b3ShapeCastSphere(&local.sphere, input);
            break;
        case GAME_COLLIDER3D_CAPSULE:
            out = b3ShapeCastCapsule(&local.capsule, input);
            break;
        case GAME_COLLIDER3D_MESH:
            out = b3ShapeCastMesh(&local.mesh, input);
            break;
    }
    release_geometry(&local);
    return out;
}

static bool overlap(const game_physics3d *physics, const query_shape *shape, const b3ShapeProxy *proxy)
{
    geometry local = query_geometry(physics, shape);
    bool out = false;
    switch (local.kind) {
        case GAME_COLLIDER3D_BOX:
        case GAME_COLLIDER3D_HULL:
            out = b3OverlapHull(geometry_hull(&local), shape->transform, proxy);
            break;
        case GAME_COLLIDER3D_SPHERE:
            out = b3OverlapSphere(&local.sphere, shape->transform, proxy);
            break;
        case GAME_COLLIDER3D_CAPSULE:
            out = b3OverlapCapsule(&local.capsule, shape->transform, proxy);
            break;
        case GAME_COLLIDER3D_MESH:
            out = b3OverlapMesh(&local.mesh, shape->transform, proxy);
            break;
    }
    release_geometry(&local);
    return out;
}

/* Whether self, convex at probe or a mesh without one, meets shape with
 * margin added to the convex one's radius. A mesh meets only convex shapes. */
static bool touching(
    const game_physics3d *physics,
    const query_shape *self,
    const query_proxy *probe,
    const query_shape *shape,
    float margin
)
{
    if (probe) {
        return overlap(
            physics,
            shape,
            &(b3ShapeProxy){ probe->points, probe->count, probe->radius + margin }
        );
    }
    query_proxy other;
    return world_proxy(physics, shape, &other) &&
           overlap(physics, self, &(b3ShapeProxy){ other.points, other.count, other.radius + margin });
}

/* Keeps a cast's hit on shape when it is nearer than any so far. Box3D casts
 * in the shape's own frame. */
static void keep_nearest(
    const query_shape *shape,
    b3CastOutput cast,
    float length,
    float *nearest,
    game_physics3d_hit *out
)
{
    if (!cast.hit || cast.fraction >= *nearest) {
        return;
    }
    *nearest = cast.fraction;
    *out = (game_physics3d_hit){
        true,
        shape->entity,
        from_b3(b3TransformPoint(shape->transform, cast.point)),
        from_b3(b3RotateVector(shape->transform.q, cast.normal)),
        cast.fraction * length,
    };
}

static bool queried(const query_shape *shape, unsigned categories, bool sensors, ecs_entity skip)
{
    return (shape->collider.category & categories) &&
           (sensors || !shape->collider.sensor) && !same_entity(shape->owner, skip);
}

int game_physics3d_raycast(
    game_physics3d *physics,
    CF_V3 start,
    CF_V3 end,
    unsigned categories,
    ecs_entity exclude,
    bool sensors,
    game_physics3d_hit *out
)
{
    assert(physics && out);
    use_project_units();
    *out = (game_physics3d_hit){ 0 };
    if (!within(start, FARTHEST) || !within(end, FARTHEST)) {
        return FLOW_ERR_ARGUMENT;
    }
    ecs_entity skip = live_collider(physics, exclude) ? body_owner(physics, exclude)
                                                      : exclude;
    CF_V3 translation = cf_sub_v3(end, start);
    float length = cf_len_v3(translation), nearest = INFINITY;
    query_shape *shapes = NULL;
    int result = read_shapes(physics, &shapes);
    for (int i = 0; result >= 0 && i < acount(shapes); i++) {
        const query_shape *shape = &shapes[i];
        if (!queried(shape, categories, sensors, skip)) {
            continue;
        }
        b3RayCastInput input = {
            b3InvTransformPoint(shape->transform, to_b3(start)),
            b3InvRotateVector(shape->transform.q, to_b3(translation)),
            1,
        };
        keep_nearest(shape, ray_cast(physics, shape, &input), length, &nearest, out);
    }
    afree(shapes);
    if (result < 0) {
        *out = (game_physics3d_hit){ 0 };
    }
    return result;
}

int game_physics3d_shape_cast(
    game_physics3d *physics,
    ecs_entity caster,
    CF_V3 translation,
    unsigned categories,
    bool sensors,
    game_physics3d_hit *out
)
{
    assert(physics && out);
    use_project_units();
    *out = (game_physics3d_hit){ 0 };
    if (!within(translation, FARTHEST)) {
        return FLOW_ERR_ARGUMENT;
    }
    if (!live_collider(physics, caster)) {
        return FLOW_OK;
    }
    query_shape self;
    query_proxy moving;
    if (!read_shape(physics, caster, &self) || !world_proxy(physics, &self, &moving)) {
        return FLOW_ERR_ARGUMENT;
    }
    float length = cf_len_v3(translation), nearest = INFINITY;
    query_shape *shapes = NULL;
    int result = read_shapes(physics, &shapes);
    for (int i = 0; result >= 0 && i < acount(shapes); i++) {
        const query_shape *shape = &shapes[i];
        if (!queried(shape, categories, sensors, self.owner)) {
            continue;
        }
        b3Vec3 points[HULL_VERTICES];
        for (int j = 0; j < moving.count; j++) {
            points[j] = b3InvTransformPoint(shape->transform, moving.points[j]);
        }
        b3ShapeCastInput input = {
            { points, moving.count, moving.radius },
            b3InvRotateVector(shape->transform.q, to_b3(translation)),
            1,
            false,
        };
        keep_nearest(shape, shape_cast(physics, shape, &input), length, &nearest, out);
    }
    afree(shapes);
    if (result < 0) {
        *out = (game_physics3d_hit){ 0 };
    }
    return result;
}

int game_physics3d_contact_entities(
    game_physics3d *physics,
    ecs_entity entity,
    unsigned categories,
    ecs_entity **out
)
{
    assert(physics && out);
    use_project_units();
    aclear(*out);
    if (!categories || !live_collider(physics, entity)) {
        return FLOW_OK;
    }
    query_shape self;
    if (!read_shape(physics, entity, &self)) {
        return FLOW_ERR_ARGUMENT;
    }
    query_proxy probe;
    bool convex = world_proxy(physics, &self, &probe);
    query_shape *shapes = NULL;
    int result = read_shapes(physics, &shapes);
    for (int i = 0; result >= 0 && i < acount(shapes); i++) {
        const query_shape *shape = &shapes[i];
        if (!queried(shape, categories, true, self.owner) ||
            !(shape->collider.category & self.collider.mask) ||
            !(self.collider.category & shape->collider.mask)) {
            continue;
        }
        /* A solid contact begins within Box3D's speculative distance. */
        float margin = self.collider.sensor || shape->collider.sensor
                         ? 0
                         : B3_SPECULATIVE_DISTANCE;
        if (touching(physics, &self, convex ? &probe : NULL, shape, margin)) {
            apush(*out, shape->entity);
        }
    }
    afree(shapes);
    if (result < 0) {
        aclear(*out);
    }
    return result;
}
