#include "runtime/scenes/physics.h"

#include "diagnostics/tracy.h"
#include "execution/status.h"
#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/events.h"
#include "runtime/materials.h"
#include "runtime/task_pool.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <box2d/box2d.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
    PARALLEL_BODY_MINIMUM = 256
};

typedef struct
{
    material_rect rect;
    b2ShapeId shape;
    int index;
} mask_shape;

/* One per entity slot while the entity has a collider. */
typedef struct
{
    ecs_entity entity;
    /* The body holding its shapes, its own or the one carrying it, or null
     * while a follower waits to be placed. */
    b2BodyId body;
    b2ShapeId shape; /* The collider's shape when it has no mask. */
    mask_shape *shapes;
    b2ShapeProxy geometry;
    b2ShapeProxy *parts;
    const material_mask *mask; /* Borrowed only by immediate authored queries. */
    unsigned mask_revision;
    uint64_t mask_generation;
    b2Transform start, end;
    b2Transform offset; /* Where its shapes sit on the body: identity on its own. */
    b2AABB tree_bounds;
    Collider collider;
    Body settings;
    int proxy;
    float angle, stop_fraction;
    bool seen, pierceable_fragment, masked;
    bool follower; /* Has a ChildOf parent, so it follows it and never writes back. */
    bool carries; /* Has carried a follower on its body since it was placed. */
    int fluid_part; /* 1 + the fluid body its body's colliders are parts of this tick, or 0. */
    /* The slot of the body it moves with: its own for a root, its anchor's
     * for a follower carried on that body, or 0 for a follower with none. */
    unsigned group;
} physics_body;

typedef struct
{
    uint64_t first, second;
    b2Vec2 point;
    float fraction;
} contact_pair;

typedef struct
{
    contact_pair *pairs;
    b2ContactData *contacts;
    unsigned *visited;
} pair_buffer;

typedef struct
{
    b2BodyId body;
    b2Vec2 force;
} drive_force;

/* One per Joint entity slot while its joint exists. */
typedef struct
{
    ecs_entity entity;
    b2JointId joint;
    Joint settings;     /* As the joint was made. */
    b2BodyId a, b;      /* Other's body or the ground, and the entity's. */
    unsigned owners[2]; /* The slots owning a (0 for the ground) and b. */
    b2Transform frame;  /* The entity on body b. */
    bool seen;
} joint_record;

/* An impulse or force for a dynamic body's next advancing step. */
typedef struct
{
    ecs_entity entity;
    b2Vec2 amount;
    bool impulse;
} push;

struct physics
{
    ecs_world *world;
    entities *entities;
    b2WorldId simulation;
    b2DynamicTree sweeps;
    physics_settings configuration;
    physics_body *bodies;
    mask_shape *shape_scratch;
    contact_pair *previous, *current, *sort_scratch, *begun;
    b2ContactData *contacts;
    unsigned *visited;
    drive_force *forces;
    push *pushes;
    joint_record *joints;
    uint64_t *apart; /* Sorted owner slot pairs joints keep from touching. */
    b2BodyId ground; /* A static body for joints to the world, once one is. */
    fluid_body *fluid_bodies;
    fluid_obstacle *fluid_obstacles;
    b2BodyId *fluid_ids;
    bool profiling;
    physics_profile profile;
    task_system tasks;
    task_pool *pool;
    int body_count, active_workers;
    pair_buffer *workers;
    char error[256];
};

static double clock_ms()
{
    struct timespec now;
    int result = clock_gettime(CLOCK_MONOTONIC, &now);
    assert(!result);
    (void)result;
    return now.tv_sec * 1000.0 + now.tv_nsec * 1e-6;
}

static void profile_phase(physics *physics, double *start, double *elapsed)
{
    if (!physics->profiling) {
        return;
    }
    double now = clock_ms();
    *elapsed = now - *start;
    *start = now;
}

void physics_set_profiling(physics *physics, bool enabled)
{
    physics->profiling = enabled;
    physics->profile = (physics_profile){ 0 };
}

const physics_profile *physics_last_profile(const physics *physics)
{
    return &physics->profile;
}

static uint64_t physics_hash_byte(uint64_t hash, unsigned char byte)
{
    return (hash ^ byte) * UINT64_C(1099511628211);
}

static uint64_t physics_hash_u64(uint64_t hash, uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8) {
        hash = physics_hash_byte(hash, (unsigned char)(value >> shift));
    }
    return hash;
}

static uint64_t physics_hash_float(uint64_t hash, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    for (int shift = 0; shift < 32; shift += 8) {
        hash = physics_hash_byte(hash, (unsigned char)(bits >> shift));
    }
    return hash;
}

physics_hashes physics_hash(const physics *physics)
{
    const uint64_t seed = UINT64_C(14695981039346656037);
    physics_hashes hashes = { seed, seed, seed };
    uint64_t body_count = 0;
    for (int i = 0; i < acount(physics->bodies); i++) {
        const physics_body *body = &physics->bodies[i];
        if (!body->body.index1) {
            continue;
        }
        b2Transform transform = b2Body_GetTransform(body->body);
        b2Vec2 velocity = b2Body_GetLinearVelocity(body->body);
        hashes.body_state = physics_hash_u64(hashes.body_state, ecs_entity_to_u64(body->entity));
        hashes.body_state = physics_hash_float(hashes.body_state, transform.p.x);
        hashes.body_state = physics_hash_float(hashes.body_state, transform.p.y);
        hashes.body_state = physics_hash_float(hashes.body_state, transform.q.c);
        hashes.body_state = physics_hash_float(hashes.body_state, transform.q.s);
        hashes.body_state = physics_hash_float(hashes.body_state, velocity.x);
        hashes.body_state = physics_hash_float(hashes.body_state, velocity.y);
        hashes.body_state = physics_hash_float(hashes.body_state, b2Body_GetAngularVelocity(body->body));
        body_count++;
    }
    hashes.body_state = physics_hash_u64(hashes.body_state, body_count);
    int pair_count = acount(physics->previous);
    hashes.contact_pairs = physics_hash_u64(hashes.contact_pairs, (uint64_t)pair_count);
    for (int i = 0; i < pair_count; i++) {
        contact_pair pair = physics->previous[i];
        hashes.contact_pairs = physics_hash_u64(hashes.contact_pairs, pair.first);
        hashes.contact_pairs = physics_hash_u64(hashes.contact_pairs, pair.second);
    }
    int event_count = acount(physics->begun);
    hashes.contact_events = physics_hash_u64(hashes.contact_events, (uint64_t)event_count);
    for (int i = 0; i < event_count; i++) {
        contact_pair pair = physics->begun[i];
        hashes.contact_events = physics_hash_u64(hashes.contact_events, pair.first);
        hashes.contact_events = physics_hash_u64(hashes.contact_events, pair.second);
        hashes.contact_events = physics_hash_float(
            hashes.contact_events,
            pair.point.x
        );
        hashes.contact_events = physics_hash_float(
            hashes.contact_events,
            pair.point.y
        );
        hashes.contact_events = physics_hash_float(hashes.contact_events, pair.fraction);
    }
    return hashes;
}

static bool same_entity(ecs_entity a, ecs_entity b)
{
    return ecs_entity_to_u64(a) == ecs_entity_to_u64(b);
}

static bool eligible(const physics_body *a, const physics_body *b)
{
    return !same_entity(a->entity, b->entity) &&
           (!a->group || a->group != b->group) &&
           (a->collider.category & b->collider.mask) &&
           (b->collider.category & a->collider.mask) &&
           a->collider.ignore_entity != ecs_entity_to_u64(b->entity) &&
           b->collider.ignore_entity != ecs_entity_to_u64(a->entity);
}

static physics_body *shape_body(physics *physics, b2ShapeId shape)
{
    return &physics->bodies[(uintptr_t)b2Shape_GetUserData(shape)];
}

static bool collision_filter(b2ShapeId first, b2ShapeId second, void *user)
{
    physics *physics = user;
    return eligible(shape_body(physics, first), shape_body(physics, second));
}

static void create_world(physics *physics)
{
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = (b2Vec2){ 0 };
    def.enableSleep = physics->configuration.sleep;
    def.enableContinuous = physics->configuration.continuous;
    def.contactHertz = physics->configuration.contact_hertz;
    def.contactDampingRatio = physics->configuration.contact_damping;
    if (physics->tasks.workers) {
        def.workerCount = physics->tasks.workers;
        def.enqueueTask = physics->tasks.enqueue;
        def.finishTask = physics->tasks.finish;
        def.userTaskContext = physics->tasks.user;
    }
    physics->simulation = b2CreateWorld(&def);
    physics->sweeps = b2DynamicTree_Create();
    b2World_SetCustomFilterCallback(physics->simulation, collision_filter, physics);
}

static void free_workers(physics *physics)
{
    for (int i = 0; i < acount(physics->workers); i++) {
        afree(physics->workers[i].pairs);
        afree(physics->workers[i].contacts);
        afree(physics->workers[i].visited);
    }
    afree(physics->workers);
}

static void configure_tasks(physics *physics, const task_system *tasks)
{
    free_workers(physics);
    physics->tasks = tasks ? *tasks : (task_system){ 0 };
    if (tasks && tasks->workers > 1) {
        asetlen(physics->workers, tasks->workers);
        memset(physics->workers, 0, (size_t)tasks->workers * sizeof *physics->workers);
    }
    physics->active_workers = 1;
}

physics *physics_new(ecs_world *world, entities *entities)
{
    assert(world && entities);
    /* Box2D's unit configuration is process-wide, and precedes all defaults. */
    static bool initialized;
    if (!initialized) {
        b2SetLengthUnitsPerMeter(UNITS_PER_METER);
        initialized = true;
    }
    physics *physics = xcalloc(1, sizeof *physics);
    physics->world = world;
    physics->entities = entities;
    physics->configuration = physics_defaults();
    physics->pool = task_pool_acquire();
    configure_tasks(physics, task_pool_tasks(physics->pool));
    create_world(physics);
    return physics;
}

void physics_free(physics *physics)
{
    if (!physics) {
        return;
    }
    b2DestroyWorld(physics->simulation);
    b2DynamicTree_Destroy(&physics->sweeps);
    for (int i = 0; i < acount(physics->bodies); i++) {
        afree(physics->bodies[i].parts);
        afree(physics->bodies[i].shapes);
    }
    afree(physics->bodies);
    afree(physics->shape_scratch);
    afree(physics->previous);
    afree(physics->current);
    afree(physics->sort_scratch);
    afree(physics->begun);
    afree(physics->contacts);
    afree(physics->visited);
    afree(physics->forces);
    afree(physics->pushes);
    afree(physics->joints);
    afree(physics->apart);
    afree(physics->fluid_bodies);
    afree(physics->fluid_obstacles);
    afree(physics->fluid_ids);
    free_workers(physics);
    if (physics->pool) {
        task_pool_release(physics->pool);
    }
    free(physics);
}

void physics_reset(physics *physics)
{
    physics->profile = (physics_profile){ 0 };
    b2DestroyWorld(physics->simulation);
    b2DynamicTree_Destroy(&physics->sweeps);
    for (int i = 0; i < acount(physics->bodies); i++) {
        afree(physics->bodies[i].parts);
        afree(physics->bodies[i].shapes);
    }
    aclear(physics->bodies);
    physics->body_count = 0;
    physics->active_workers = 1;
    aclear(physics->previous);
    aclear(physics->current);
    aclear(physics->pushes);
    aclear(physics->joints);
    aclear(physics->apart);
    physics->ground = b2_nullBodyId;
    physics->error[0] = 0;
    create_world(physics);
}

const char *physics_error(const physics *physics)
{
    return physics->error;
}

int physics_configure(physics *physics, physics_settings settings)
{
    if (!physics_settings_valid(settings)) {
        snprintf(physics->error, sizeof physics->error, "Invalid physics settings: workers 0..32, substeps 1..16, contact frequency 0..240 Hz, damping ratio 0..100.");
        return FLOW_ERR_ARGUMENT;
    }
    if (settings.workers != physics->configuration.workers) {
        if (acount(physics->bodies) || !physics->pool) {
            snprintf(physics->error, sizeof physics->error, "Physics worker changes require a reset world with the runtime executor.");
            return FLOW_ERR_ARGUMENT;
        }
        task_pool *previous = physics->pool;
        physics->pool = task_pool_acquire_workers(settings.workers);
        configure_tasks(physics, task_pool_tasks(physics->pool));
        physics->configuration = settings;
        physics_reset(physics);
        task_pool_release(previous);
    }
    physics->configuration = settings;
    b2World_EnableSleeping(physics->simulation, settings.sleep);
    b2World_EnableContinuous(physics->simulation, settings.continuous);
    b2World_SetContactTuning(
        physics->simulation,
        settings.contact_hertz,
        settings.contact_damping,
        b2DefaultWorldDef().maxContactPushSpeed
    );
    physics->error[0] = 0;
    return FLOW_OK;
}

physics_settings physics_configuration(const physics *physics)
{
    return physics->configuration;
}

int physics_workers(const physics *physics)
{
    return physics->active_workers;
}

bool physics_interpolation_offset(const physics *physics, ecs_entity entity, float alpha, CF_V2 *out)
{
    assert(out && isfinite(alpha));
    if (!entity.slot || entity.slot >= (unsigned)acount(physics->bodies) ||
        !ecs_entity_is_alive(physics->world, entity) ||
        entities_is_retiring(physics->entities, entity)) {
        return false;
    }
    const physics_body *body = &physics->bodies[entity.slot];
    if (!body->body.index1 || !same_entity(body->entity, entity) || body->follower) {
        return false;
    }
    const Transform *authored = Transform_get_const(physics->world, entity);
    if (!authored) {
        return false;
    }
    if (authored->x != body->end.p.x || authored->y != body->end.p.y) {
        *out = (CF_V2){ 0 };
        return true;
    }
    b2Vec2 position = b2Lerp(body->start.p, body->end.p, fminf(1, fmaxf(0, alpha)));
    *out = (CF_V2){ position.x - body->end.p.x, position.y - body->end.p.y };
    return true;
}

bool physics_inspect_body(const physics *physics, ecs_entity entity, physics_body_state *out)
{
    assert(out);
    if (!ecs_entity_is_alive(physics->world, entity) ||
        entity.slot >= (unsigned)acount(physics->bodies)) {
        return false;
    }
    const physics_body *body = &physics->bodies[entity.slot];
    if (!body->body.index1 || !same_entity(body->entity, entity)) {
        return false;
    }
    b2MassData mass = b2Body_GetMassData(body->body);
    *out = (physics_body_state){ .mass = mass.mass,
                                 .local_center = { mass.center.x, mass.center.y },
                                 .inertia = mass.rotationalInertia,
                                 .awake = b2Body_IsAwake(body->body),
                                 .shape_count = b2Body_GetShapeCount(body->body) };
    return true;
}

/* Bodies live in world space. Returns whether a parent places the entity, so
 * its body follows that parent and never writes its pose back. */
static bool world_pose(const ecs_world *world, ecs_entity entity, Transform *position, float *angle)
{
    GlobalTransform2D pose = transform_world_2d(world, entity);
    *position = pose.position;
    *angle = pose.radians;
    return transform_parent(world, entity).slot != 0;
}

static bool valid(const Transform *position, float angle, const Collider *shape, const Body *body, CF_V2 velocity)
{
    /* Match Box2D's large-coordinate bound before creating any proxies. */
    float maximum = 100000 * UNITS_PER_METER;
    float extent = shape->shape == COLLIDER_CIRCLE
                     ? shape->radius
                     : fmaxf(shape->width, shape->height);
    return isfinite(position->x) && isfinite(position->y) && isfinite(angle) &&
           fabsf(position->x) + extent < maximum &&
           fabsf(position->y) + extent < maximum && isfinite(velocity.x) &&
           isfinite(velocity.y) && body->type >= BODY_STATIC &&
           body->type <= BODY_KINEMATIC && isfinite(body->angular_velocity) &&
           isfinite(shape->restitution) && shape->restitution >= 0 &&
           shape->restitution <= 1 && isfinite(shape->friction) &&
           shape->friction >= 0 &&
           ((shape->shape == COLLIDER_CIRCLE && isfinite(shape->radius) &&
             shape->radius >= 0) ||
            (shape->shape == COLLIDER_BOX && isfinite(shape->width) &&
             shape->width > 0 && isfinite(shape->height) && shape->height > 0));
}

static bool same_configuration(const physics_body *body, const Collider *shape, const Body *settings)
{
    const Collider *old = &body->collider;
    return body->settings.type == settings->type &&
           body->settings.continuous == settings->continuous &&
           body->settings.allow_rotation == settings->allow_rotation &&
           old->shape == shape->shape && old->radius == shape->radius &&
           old->width == shape->width && old->height == shape->height &&
           old->category == shape->category && old->mask == shape->mask &&
           old->sensor == shape->sensor &&
           old->pierce_fragments == shape->pierce_fragments &&
           old->ignore_entity == shape->ignore_entity &&
           old->restitution == shape->restitution && old->friction == shape->friction;
}

/* Whether its shapes are on the body of the anchor it follows. */
static bool carried(const physics_body *body)
{
    return body->group && body->group != body->entity.slot;
}

/* The slot of the record that owns the body holding its shapes. */
static unsigned owner(const physics_body *body)
{
    return carried(body) ? body->group : body->entity.slot;
}

static uint64_t slot_pair(unsigned a, unsigned b)
{
    return a < b ? (uint64_t)a << 32 | b : (uint64_t)b << 32 | a;
}

static int compare_slot_pairs(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

/* Whether a joint without collide joins the bodies a and b ride, so they
 * report no contact, as Box2D keeps them from touching. */
static bool kept_apart(const physics *physics, const physics_body *a, const physics_body *b)
{
    if (!acount(physics->apart)) {
        return false;
    }
    uint64_t key = slot_pair(owner(a), owner(b));
    return bsearch(&key, physics->apart, (size_t)acount(physics->apart), sizeof key, compare_slot_pairs);
}

static int own_shape_count(const physics_body *body)
{
    return acount(body->shapes) + B2_IS_NON_NULL(body->shape);
}

/* Forgets the shapes a destroyed body took with it and leaves the swept
 * tree, until the record is placed again. */
static void unplace(physics *physics, physics_body *body)
{
    if (body->proxy >= 0) {
        b2DynamicTree_DestroyProxy(&physics->sweeps, body->proxy);
    }
    body->proxy = -1;
    body->body = b2_nullBodyId;
    body->shape = b2_nullShapeId;
    body->carries = false;
    aclear(body->shapes);
}

/* Takes a carried collider's shapes and their mass off its carrier. */
static void remove_shapes(physics *physics, physics_body *body)
{
    if (physics->profiling) {
        physics->profile.shape_removals += own_shape_count(body);
    }
    if (B2_IS_NON_NULL(body->shape)) {
        b2DestroyShape(body->shape, false);
    }
    for (int i = 0; i < acount(body->shapes); i++) {
        b2DestroyShape(body->shapes[i].shape, false);
    }
    b2Body_ApplyMassFromShapes(body->body);
    b2Body_SetAwake(body->body, true);
    unplace(physics, body);
}

/* Destroying a body destroys the shapes it carries, which placing the
 * followers puts back. */
static void destroy_body(physics *physics, physics_body *body)
{
    if (physics->profiling) {
        physics->profile.shape_removals += b2Body_GetShapeCount(body->body);
    }
    if (body->carries) {
        for (int i = 0; i < acount(physics->bodies); i++) {
            physics_body *other = &physics->bodies[i];
            if (other != body && B2_ID_EQUALS(other->body, body->body)) {
                unplace(physics, other);
            }
        }
    }
    b2DestroyBody(body->body);
    unplace(physics, body);
}

static void remove_body(physics *physics, physics_body *body)
{
    if (body->body.index1 && carried(body)) {
        remove_shapes(physics, body);
    } else if (body->body.index1) {
        destroy_body(physics, body);
    }
    assert(body->proxy < 0);
    afree(body->parts);
    afree(body->shapes);
    *body = (physics_body){ 0 };
}

static b2Polygon rect_polygon(material_rect r)
{
    return b2MakeOffsetBox(r.width * .5f, r.height * .5f, (b2Vec2){ r.x, r.y }, b2Rot_identity);
}

/* The rectangle where the body's offset puts it on the body it rides. */
static b2Polygon placed_rect(const physics_body *body, material_rect r)
{
    b2Vec2 center = b2TransformPoint(body->offset, (b2Vec2){ r.x, r.y });
    return b2MakeOffsetBox(r.width * .5f, r.height * .5f, center, body->offset.q);
}

static int geometry_count(const physics_body *body)
{
    if (body->masked) {
        return acount(body->parts);
    }
    return body->mask ? material_mask_collision(body->mask).count : 1;
}

static b2ShapeProxy geometry_at(const physics_body *body, int index)
{
    if (body->parts) {
        return body->parts[index];
    }
    if (body->mask) {
        b2Polygon box = rect_polygon(material_mask_collision(body->mask).rects[index]);
        return b2MakeProxy(box.vertices, box.count, box.radius);
    }
    return body->geometry;
}

static b2AABB bounds(const physics_body *body, b2Transform transform)
{
    b2AABB result = { transform.p, transform.p };
    for (int part = 0; part < geometry_count(body); part++) {
        b2ShapeProxy geometry = geometry_at(body, part);
        b2Vec2 radius = { geometry.radius, geometry.radius };
        for (int i = 0; i < geometry.count; i++) {
            b2Vec2 p = b2TransformPoint(transform, geometry.points[i]);
            if (!part && !i) {
                result = (b2AABB){ b2Sub(p, radius), b2Add(p, radius) };
            }
            result.lowerBound = b2Min(result.lowerBound, b2Sub(p, radius));
            result.upperBound = b2Max(result.upperBound, b2Add(p, radius));
        }
    }
    return result;
}

static b2AABB swept_bounds(const physics_body *body)
{
    b2AABB start = bounds(body, body->start), end = bounds(body, body->end);
    return (b2AABB){ b2Min(start.lowerBound, end.lowerBound),
                     b2Max(start.upperBound, end.upperBound) };
}

static b2ShapeDef shape_definition(const physics_body *body)
{
    b2ShapeDef shape = b2DefaultShapeDef();
    shape.userData = (void *)(uintptr_t)body->entity.slot;
    shape.material.friction = body->collider.friction;
    shape.material.restitution = body->collider.restitution;
    /* A carried sensor adds no mass to its carrier. */
    shape.density = carried(body) && body->collider.sensor
                      ? 0
                      : 1.0f / (UNITS_PER_METER * UNITS_PER_METER);
    shape.filter.categoryBits = body->collider.category;
    shape.filter.maskBits = body->collider.mask;
    shape.isSensor = body->collider.sensor;
    /* Gameplay contacts are collected below with current-pose and swept-sensor
     * semantics. No consumer reads Box2D's separate buffered event streams. */
    shape.enableSensorEvents = false;
    shape.enableContactEvents = false;
    return shape;
}

/* Centers are unique in the nonoverlapping rectangle decomposition. Matching
 * them leaves unchanged shapes/contact caches intact and needs only one merge. */
static int compare_shape_center(const void *left, const void *right)
{
    const material_rect *a = &((const mask_shape *)left)->rect;
    const material_rect *b = &((const mask_shape *)right)->rect;
    if (a->y != b->y) {
        return a->y < b->y ? -1 : 1;
    }
    return a->x != b->x ? (a->x < b->x ? -1 : 1) : 0;
}

static void update_mask_shapes(physics *physics, physics_body *body, const material_mask *mask)
{
    material_collision_view collision = material_mask_collision(mask);
    asetlen(physics->shape_scratch, collision.count);
    for (int i = 0; i < collision.count; i++) {
        physics->shape_scratch[i] = (mask_shape){ .rect = collision.rects[i],
                                                  .index = i };
    }
    int old_count = acount(body->shapes);
    if (old_count > 1) {
        qsort(body->shapes, (size_t)old_count, sizeof *body->shapes, compare_shape_center);
    }
    if (collision.count > 1) {
        qsort(physics->shape_scratch, (size_t)collision.count, sizeof *physics->shape_scratch, compare_shape_center);
    }
    b2ShapeDef definition = shape_definition(body);
    definition.updateBodyMass = false;
    definition.invokeContactCreation = true;
    bool changed = false;
    int old = 0, next = 0;
    while (old < old_count || next < collision.count) {
        mask_shape *before = old < old_count ? &body->shapes[old] : NULL;
        mask_shape *after = next < collision.count ? &physics->shape_scratch[next]
                                                   : NULL;
        int order = !before ? 1
                  : !after  ? -1
                            : compare_shape_center(before, after);
        if (!order) {
            after->shape = before->shape;
            if (before->rect.width != after->rect.width ||
                before->rect.height != after->rect.height) {
                b2Polygon polygon = placed_rect(body, after->rect);
                /* This Box2D version keeps a cached CCD centroid in SetPolygon.
                 * Equal centers make that cache valid; moved parts are recreated. */
                b2Polygon previous = b2Shape_GetPolygon(before->shape);
                if (previous.centroid.x == polygon.centroid.x &&
                    previous.centroid.y == polygon.centroid.y) {
                    b2Shape_SetPolygon(after->shape, &polygon);
                    if (physics->profiling) {
                        physics->profile.shape_updates++;
                    }
                } else {
                    b2DestroyShape(before->shape, false);
                    after->shape = b2CreatePolygonShape(body->body, &definition, &polygon);
                    if (physics->profiling) {
                        physics->profile.shape_removals++;
                        physics->profile.shape_creations++;
                    }
                }
                changed = true;
            }
            old++;
            next++;
        } else if (order < 0) {
            b2DestroyShape(before->shape, false);
            if (physics->profiling) {
                physics->profile.shape_removals++;
            }
            changed = true;
            old++;
        } else {
            b2Polygon polygon = placed_rect(body, after->rect);
            after->shape = b2CreatePolygonShape(body->body, &definition, &polygon);
            if (physics->profiling) {
                physics->profile.shape_creations++;
            }
            changed = true;
            next++;
        }
    }
    asetlen(body->shapes, collision.count);
    asetlen(body->parts, collision.count);
    for (int i = 0; i < collision.count; i++) {
        mask_shape part = physics->shape_scratch[i];
        body->shapes[part.index] = part;
        b2Polygon polygon = rect_polygon(part.rect);
        body->parts[part.index] = b2MakeProxy(
            polygon.vertices,
            polygon.count,
            polygon.radius
        );
    }
    body->mask_revision = mask->revision;
    if (!changed) {
        return;
    }
    b2Body_SetAwake(body->body, true);
    b2Body_ApplyMassFromShapes(body->body);
    if (physics->profiling) {
        physics->profile.mass_updates++;
    }
    /* The auxiliary swept-target tree must also lose old bounds after a cut. */
    if (body->proxy >= 0) {
        b2DynamicTree_DestroyProxy(&physics->sweeps, body->proxy);
        body->proxy = -1;
    }
}

/* Puts the collider's shapes on its body where its offset places them. */
static void create_shapes(physics *physics, physics_body *body)
{
    b2ShapeDef shape = shape_definition(body);
    materials *materials = entities_materials(physics->entities);
    const material_mask *mask = materials_mask(materials, body->entity);
    body->mask_generation = materials_mask_generation(materials, body->entity);
    body->masked = mask != NULL;
    body->mask_revision = mask ? mask->revision : 0;
    aclear(body->shapes);
    aclear(body->parts);
    int count = 1;
    if (mask) {
        material_collision_view collision = material_mask_collision(mask);
        shape.updateBodyMass = false;
        for (int i = 0; i < collision.count; i++) {
            b2Polygon placed = placed_rect(body, collision.rects[i]);
            b2Polygon box = rect_polygon(collision.rects[i]);
            b2ShapeId id = b2CreatePolygonShape(body->body, &shape, &placed);
            apush(body->shapes, ((mask_shape){ .rect = collision.rects[i], .shape = id, .index = i }));
            apush(body->parts, b2MakeProxy(box.vertices, box.count, box.radius));
        }
        b2Body_ApplyMassFromShapes(body->body);
        count = collision.count;
    } else if (body->collider.shape == COLLIDER_CIRCLE) {
        b2Circle circle = { .center = body->offset.p, .radius = body->collider.radius };
        body->shape = b2CreateCircleShape(body->body, &shape, &circle);
        body->geometry = b2MakeProxy(&b2Vec2_zero, 1, circle.radius);
    } else {
        float half_width = body->collider.width * .5f,
              half_height = body->collider.height * .5f;
        b2Polygon placed = b2MakeOffsetBox(
            half_width,
            half_height,
            body->offset.p,
            body->offset.q
        );
        b2Polygon box = b2MakeBox(half_width, half_height);
        body->shape = b2CreatePolygonShape(body->body, &shape, &placed);
        body->geometry = b2MakeProxy(box.vertices, box.count, box.radius);
    }
    if (physics->profiling) {
        physics->profile.shape_creations += count;
        physics->profile.mass_updates++;
    }
    body->proxy = -1;
}

/* A body of the record's own, with its collider at the body's origin. */
static void create_body(physics *physics, physics_body *body, b2Vec2 position, float angle)
{
    b2BodyDef def = b2DefaultBodyDef();
    switch (body->settings.type) {
        case BODY_STATIC: def.type = b2_staticBody; break;
        case BODY_DYNAMIC: def.type = b2_dynamicBody; break;
        case BODY_KINEMATIC: def.type = b2_kinematicBody; break;
    }
    /* A follower with no anchor moves only when its parents do. A static and
     * a kinematic body touch the same bodies. */
    if (body->follower) {
        def.type = b2_kinematicBody;
    }
    def.position = position;
    def.rotation = b2MakeRot(angle);
    def.fixedRotation = !body->follower && body->settings.type != BODY_KINEMATIC &&
                        !body->settings.allow_rotation;
    /* Box2D excludes bullet targets from CCD, so static/kinematic colliders
     * must stay eligible even when their authored Continuous flag is set. */
    def.isBullet = body->settings.type == BODY_DYNAMIC &&
                   body->settings.continuous && !body->collider.sensor;
    body->body = b2CreateBody(physics->simulation, &def);
    if (physics->profiling) {
        physics->profile.body_creations++;
    }
    body->offset = b2Transform_identity;
    create_shapes(physics, body);
    body->start = body->end = b2Body_GetTransform(body->body);
}

static int sync_entity(physics *physics, ecs_entity entity)
{
    ecs_world *world = physics->world;
    Transform position;
    float angle;
    bool follower = world_pose(world, entity, &position, &angle);
    Collider collider = *Collider_get_const(world, entity);
    const Body *authored = Body_get_const(world, entity);
    Body settings = authored ? *authored : (Body){ .type = BODY_STATIC };
    const Orientation *orientation = Orientation_get_const(world, entity);
    const Velocity *velocity = Velocity_get_const(world, entity);
    const Drive *drive = follower ? NULL : Drive_get_const(world, entity);
    CF_V2 movement = velocity && !follower ? *velocity : (CF_V2){ 0 };
    if (!valid(&position, angle, &collider, &settings, movement)) {
        snprintf(
            physics->error,
            sizeof physics->error,
            "Invalid Body, Collider, Transform or Velocity on entity %u.",
            entity.slot
        );
        return FLOW_ERR_ARGUMENT;
    }
    if (follower && settings.type == BODY_DYNAMIC) {
        snprintf(
            physics->error,
            sizeof physics->error,
            "A dynamic Body must be a root entity; entity %u has a parent.",
            entity.slot
        );
        return FLOW_ERR_ARGUMENT;
    }
    if (settings.angular_velocity != 0 &&
        (settings.type != BODY_KINEMATIC || !orientation || follower)) {
        snprintf(
            physics->error,
            sizeof physics->error,
            "Angular velocity requires a kinematic root Body and Orientation "
            "on entity %u.",
            entity.slot
        );
        return FLOW_ERR_ARGUMENT;
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
    physics_body *body = &physics->bodies[entity.slot];
    const material_mask *mask = materials_mask(entities_materials(physics->entities), entity);
    unsigned revision = mask ? mask->revision : 0;
    uint64_t generation = materials_mask_generation(entities_materials(physics->entities), entity);
    if (body->entity.slot && (!same_entity(body->entity, entity) ||
                              body->masked != (mask != NULL) || body->follower != follower ||
                              !same_configuration(body, &collider, &settings))) {
        remove_body(physics, body);
    }
    if (body->body.index1 &&
        (body->mask_revision != revision || body->mask_generation != generation)) {
        update_mask_shapes(physics, body, mask);
        body->mask_generation = generation;
    }
    if (!body->entity.slot) {
        *body = (physics_body){ .offset = b2Transform_identity, .proxy = -1 };
    }
    body->entity = entity;
    body->collider = collider;
    body->settings = settings;
    body->seen = true;
    body->follower = follower;
    body->pierceable_fragment = materials_is_pierceable_fragment(
        entities_materials(physics->entities),
        entity
    );
    body->stop_fraction = 1;
    if (follower) {
        return FLOW_OK; /* Placed once every root has its body. */
    }
    body->group = entity.slot;
    b2Vec2 point = { position.x, position.y };
    if (!body->body.index1) {
        create_body(physics, body, point, angle);
        if (drive) {
            b2Body_SetLinearVelocity(body->body, (b2Vec2){ movement.x, movement.y });
        }
    } else if (position.x != body->end.p.x || position.y != body->end.p.y || angle != body->angle) {
        b2Body_SetTransform(body->body, point, b2MakeRot(angle));
    }
    body->angle = angle;
    if (settings.type == BODY_KINEMATIC) {
        b2Body_SetAngularVelocity(body->body, settings.angular_velocity);
    }
    body->start = b2Body_GetTransform(body->body);
    return FLOW_OK;
}

/* Sets a root's velocity, or queues the force that brings it to its Drive
 * target, once its followers have added their mass. */
static int set_motion(physics *physics, physics_body *body, float delta)
{
    const Velocity *velocity = Velocity_get_const(physics->world, body->entity);
    const Drive *drive = Drive_get_const(physics->world, body->entity);
    CF_V2 movement = velocity ? *velocity : (CF_V2){ 0 };
    bool dynamic = body->settings.type == BODY_DYNAMIC && delta > 0;
    if (drive && dynamic) {
        if (!isfinite(drive->target.x + drive->target.y + drive->acceleration) ||
            drive->acceleration < 0) {
            snprintf(
                physics->error,
                sizeof physics->error,
                "Drive requires a finite target and nonnegative acceleration "
                "on entity %u.",
                body->entity.slot
            );
            return FLOW_ERR_ARGUMENT;
        }
        b2Vec2 current = b2Body_GetLinearVelocity(body->body);
        b2Vec2 change = b2Sub((b2Vec2){ drive->target.x, drive->target.y }, current);
        float length = b2Length(change), limit = drive->acceleration * delta;
        if (length > limit) {
            change = b2MulSV(limit / length, change);
        }
        b2Vec2 force = b2MulSV(b2Body_GetMass(body->body) / delta, change);
        apush(physics->forces, ((drive_force){ body->body, force }));
        return FLOW_OK;
    }
    if (physics->configuration.force_drive && dynamic) {
        b2Vec2 current = b2Body_GetLinearVelocity(body->body);
        double scale = (double)b2Body_GetMass(body->body) / delta;
        b2Vec2 force = { (float)(scale * (movement.x - current.x)),
                         (float)(scale * (movement.y - current.y)) };
        if (!isfinite(force.x) || !isfinite(force.y)) {
            snprintf(
                physics->error,
                sizeof physics->error,
                "Non-finite velocity drive force on entity %u.",
                body->entity.slot
            );
            return FLOW_ERR_ARGUMENT;
        }
        if (force.x != 0 || force.y != 0) {
            apush(physics->forces, ((drive_force){ body->body, force }));
        }
        return FLOW_OK;
    }
    b2Body_SetLinearVelocity(body->body, (b2Vec2){ movement.x, movement.y });
    return FLOW_OK;
}

/* The body a follower moves with: its nearest ancestor body that is not a
 * follower itself, or NULL when no body places its parents. */
static physics_body *anchor(physics *physics, ecs_entity entity)
{
    for (ecs_entity at = ecs_parent(physics->world, entity); at.slot;
         at = ecs_parent(physics->world, at)) {
        physics_body *body = at.slot < (unsigned)acount(physics->bodies)
                               ? &physics->bodies[at.slot]
                               : NULL;
        if (body && body->body.index1 && body->seen && !body->follower &&
            same_entity(body->entity, at)) {
            return body;
        }
    }
    return NULL;
}

static bool continuous_sensor(const physics_body *body)
{
    return body->settings.continuous && body->collider.sensor;
}

static bool same_transform(b2Transform a, b2Transform b)
{
    return a.p.x == b.p.x && a.p.y == b.p.y && a.q.c == b.q.c && a.q.s == b.q.s;
}

/* Where a follower sits on its anchor's body: its own and its parents'
 * Transform and Orientation below the anchor. It holds still while the
 * anchor moves and turns. */
static b2Transform carried_offset(const ecs_world *world, ecs_entity entity, ecs_entity anchor)
{
    b2Transform offset = b2Transform_identity;
    for (ecs_entity at = entity; !same_entity(at, anchor);
         at = transform_parent(world, at)) {
        assert(at.slot);
        const Transform *local = Transform_get_const(world, at);
        const Orientation *orientation = Orientation_get_const(world, at);
        b2Transform placed = { { local->x, local->y },
                               b2MakeRot(orientation ? orientation->radians : 0) };
        offset = b2MulTransforms(placed, offset);
    }
    return offset;
}

/* Puts the follower's shapes on its anchor's body, so the solver moves them
 * with that body, they add their mass, and what they touch pushes it. A
 * changed placement below the anchor gets new shapes. */
static void carry(physics *physics, physics_body *body, physics_body *with)
{
    b2Transform offset = carried_offset(physics->world, body->entity, with->entity);
    if (!body->body.index1 || !B2_ID_EQUALS(body->body, with->body) ||
        !same_transform(offset, body->offset)) {
        if (body->body.index1 && carried(body)) {
            remove_shapes(physics, body);
        } else if (body->body.index1) {
            destroy_body(physics, body);
        }
        body->body = with->body;
        body->group = with->entity.slot;
        body->offset = offset;
        with->carries = true;
        create_shapes(physics, body);
        b2Body_SetAwake(body->body, true);
    }
    body->start = b2MulTransforms(with->start, offset);
}

/* A follower without an anchor has a kinematic body of its own, which
 * teleports when its parents move. */
static void place_alone(physics *physics, physics_body *body)
{
    Transform position;
    float angle;
    world_pose(physics->world, body->entity, &position, &angle);
    b2Vec2 point = { position.x, position.y };
    if (body->body.index1 && carried(body)) {
        remove_shapes(physics, body);
    }
    body->group = 0;
    if (!body->body.index1) {
        create_body(physics, body, point, angle);
    } else if (position.x != body->end.p.x || position.y != body->end.p.y || angle != body->angle) {
        b2Body_SetTransform(body->body, point, b2MakeRot(angle));
    }
    b2Body_SetLinearVelocity(body->body, b2Vec2_zero);
    b2Body_SetAngularVelocity(body->body, 0);
    body->angle = angle;
    body->start = b2Body_GetTransform(body->body);
}

/* Places each follower once every root has its body. A continuous sensor
 * stops where it first meets a blocker, after what it carries was tested at
 * its full sweep, so it may carry nothing. */
static int place_followers(physics *physics)
{
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        if (!body->seen || !body->follower) {
            continue;
        }
        physics_body *with = anchor(physics, body->entity);
        if (with && continuous_sensor(with)) {
            snprintf(
                physics->error,
                sizeof physics->error,
                "A continuous sensor Body cannot carry colliders; entity %u is "
                "under entity %u.",
                body->entity.slot,
                with->entity.slot
            );
            return FLOW_ERR_ARGUMENT;
        }
        if (with) {
            carry(physics, body, with);
        } else {
            place_alone(physics, body);
        }
    }
    return FLOW_OK;
}

static int joint_error(physics *physics, ecs_entity entity, const char *problem)
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

/* Whether the entity or one of its parents is being destroyed, which
 * releases a joint on or under it. */
static bool released(const physics *physics, ecs_entity entity)
{
    for (ecs_entity at = entity; at.slot; at = ecs_parent(physics->world, at)) {
        if (entities_is_retiring(physics->entities, at)) {
            return true;
        }
    }
    return false;
}

/* The body a joint on or under entity holds: the nearest at or above it
 * that no other body carries, or NULL. */
static const physics_body *joined_body(const physics *physics, ecs_entity entity)
{
    for (ecs_entity at = entity; at.slot; at = transform_parent(physics->world, at)) {
        const physics_body *body = at.slot < (unsigned)acount(physics->bodies)
                                     ? &physics->bodies[at.slot]
                                     : NULL;
        if (body && body->seen && same_entity(body->entity, at) &&
            body->body.index1 && !carried(body)) {
            return body;
        }
    }
    return NULL;
}

static const char *invalid_joint(const Joint *joint)
{
    if ((unsigned)joint->kind > JOINT_DISTANCE) {
        return "Unknown Joint kind";
    }
    if (!isfinite(
            joint->lower + joint->upper + joint->speed + joint->strength +
            joint->hertz + joint->damping
        )) {
        return "Joint values must be finite";
    }
    if (joint->strength < 0 || joint->hertz < 0 || joint->damping < 0) {
        return "Joint strength, hertz and damping must not be negative";
    }
    if (joint->limit && joint->lower > joint->upper) {
        return "A Joint limit needs lower at most upper";
    }
    if (joint->limit && joint->kind == JOINT_DISTANCE && joint->lower < 0) {
        return "A distance Joint limit needs lower at least 0";
    }
    /* Box2D keeps a hinge's limits within 0.99 pi. */
    if (joint->limit && joint->kind == JOINT_HINGE &&
        (joint->lower < -.99f * B2_PI || joint->upper > .99f * B2_PI)) {
        return "A hinge Joint limit needs lower and upper within 0.99 pi";
    }
    return NULL;
}

static bool same_joint(const Joint *a, const Joint *b)
{
    return a->kind == b->kind && a->other == b->other && a->collide == b->collide &&
           a->limit == b->limit && a->lower == b->lower &&
           a->upper == b->upper && a->motor == b->motor && a->speed == b->speed &&
           a->strength == b->strength && a->spring == b->spring &&
           a->hertz == b->hertz && a->damping == b->damping;
}

static b2BodyId ground(physics *physics)
{
    if (B2_IS_NULL(physics->ground)) {
        b2BodyDef def = b2DefaultBodyDef();
        physics->ground = b2CreateBody(physics->simulation, &def);
    }
    return physics->ground;
}

/* Made from the bodies' current poses, with its anchors where the entity is
 * now, except that a distance joint's anchor on a is other's origin. */
static b2JointId create_joint(physics *physics, const joint_record *record, b2Vec2 other)
{
    /* Box2D checks a hinge's and a slider's limits even when they are off. */
    Joint joint = record->settings;
    if (!joint.limit) {
        joint.lower = joint.upper = 0;
    }
    b2Transform a = b2Body_GetTransform(record->a),
                b = b2Body_GetTransform(record->b);
    b2Transform at = b2MulTransforms(b, record->frame);
    b2Vec2 on_a = b2InvTransformPoint(a, at.p), on_b = record->frame.p;
    float reference = b2RelativeAngle(b.q, a.q);
    b2WorldId world = physics->simulation;
    switch (joint.kind) {
        case JOINT_HINGE: {
            b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
            def.bodyIdA = record->a;
            def.bodyIdB = record->b;
            def.localAnchorA = on_a;
            def.localAnchorB = on_b;
            def.collideConnected = joint.collide;
            def.referenceAngle = reference;
            def.enableLimit = joint.limit;
            def.lowerAngle = joint.lower;
            def.upperAngle = joint.upper;
            def.enableMotor = joint.motor;
            def.motorSpeed = joint.speed;
            def.maxMotorTorque = joint.strength;
            def.enableSpring = joint.spring;
            def.hertz = joint.hertz;
            def.dampingRatio = joint.damping;
            return b2CreateRevoluteJoint(world, &def);
        }
        case JOINT_SLIDER: {
            b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
            def.bodyIdA = record->a;
            def.bodyIdB = record->b;
            def.localAnchorA = on_a;
            def.localAnchorB = on_b;
            def.collideConnected = joint.collide;
            def.localAxisA = b2InvRotateVector(a.q, b2RotateVector(at.q, (b2Vec2){ 0, 1 }));
            def.referenceAngle = reference;
            def.enableLimit = joint.limit;
            def.lowerTranslation = joint.lower;
            def.upperTranslation = joint.upper;
            def.enableMotor = joint.motor;
            def.motorSpeed = joint.speed;
            def.maxMotorForce = joint.strength;
            def.enableSpring = joint.spring;
            def.hertz = joint.hertz;
            def.dampingRatio = joint.damping;
            return b2CreatePrismaticJoint(world, &def);
        }
        case JOINT_WELD: {
            b2WeldJointDef def = b2DefaultWeldJointDef();
            def.bodyIdA = record->a;
            def.bodyIdB = record->b;
            def.localAnchorA = on_a;
            def.localAnchorB = on_b;
            def.collideConnected = joint.collide;
            def.referenceAngle = reference;
            /* Zero hertz is rigid. */
            def.linearHertz = def.angularHertz = joint.spring ? joint.hertz : 0;
            def.linearDampingRatio = def.angularDampingRatio = joint.damping;
            return b2CreateWeldJoint(world, &def);
        }
        case JOINT_DISTANCE: {
            b2DistanceJointDef def = b2DefaultDistanceJointDef();
            def.bodyIdA = record->a;
            def.bodyIdB = record->b;
            def.localAnchorA = b2InvTransformPoint(a, other);
            def.localAnchorB = on_b;
            def.collideConnected = joint.collide;
            /* Box2D needs a positive length and keeps at least a slop. */
            def.length = fmaxf(b2Distance(at.p, other), FLT_MIN);
            def.enableLimit = joint.limit;
            def.minLength = joint.lower;
            def.maxLength = joint.upper;
            def.enableMotor = joint.motor;
            def.motorSpeed = joint.speed;
            def.maxMotorForce = joint.strength;
            def.enableSpring = joint.spring;
            def.hertz = joint.hertz;
            def.dampingRatio = joint.damping;
            return b2CreateDistanceJoint(world, &def);
        }
    }
    assert(false);
    return b2_nullJointId;
}

static void remove_joint(joint_record *record)
{
    if (b2Joint_IsValid(record->joint)) {
        b2DestroyJoint(record->joint);
    }
    *record = (joint_record){ 0 };
}

/* Makes the entity's joint when it has none, its bodies were remade, or its
 * fields or place on its body changed. A joint whose other is gone holds
 * nothing until it names a living one. */
static int sync_joint(physics *physics, ecs_entity entity)
{
    ecs_world *world = physics->world;
    const Joint *joint = Joint_get_const(world, entity);
    const char *problem = Transform_get_const(world, entity)
                            ? invalid_joint(joint)
                            : "A Joint needs a Transform";
    if (problem) {
        return joint_error(physics, entity, problem);
    }
    const physics_body *b = joined_body(physics, entity);
    if (!b) {
        return joint_error(physics, entity, "A Joint must be on or under a body");
    }
    ecs_entity other = ecs_entity_from_u64(joint->other);
    bool alive = other.slot && ecs_entity_is_alive(world, other) &&
                 !released(physics, other);
    if (other.slot && !alive) {
        return FLOW_OK;
    }
    const physics_body *a = alive && Transform_get_const(world, other)
                              ? joined_body(physics, other)
                              : NULL;
    if (alive && !a) {
        return joint_error(physics, entity, "A Joint's other must be on or under a body");
    }
    if (a == b) {
        return joint_error(physics, entity, "A Joint must join two different bodies");
    }
    if (!alive && joint->kind == JOINT_DISTANCE) {
        return joint_error(physics, entity, "A distance Joint needs another body");
    }
    Transform here, there = { 0 };
    float angle, other_angle = 0;
    world_pose(world, entity, &here, &angle);
    if (alive) {
        world_pose(world, other, &there, &other_angle);
    }
    if (!isfinite(here.x + here.y + angle + there.x + there.y + other_angle)) {
        return joint_error(physics, entity, "A Joint and its other need finite Transforms and Orientations");
    }
    if (entity.slot >= (unsigned)acount(physics->joints)) {
        int previous = acount(physics->joints);
        asetlen(physics->joints, (int)entity.slot + 1);
        memset(
            physics->joints + previous,
            0,
            (size_t)(acount(physics->joints) - previous) * sizeof *physics->joints
        );
    }
    joint_record *record = &physics->joints[entity.slot];
    joint_record want = { .entity = entity,
                          .settings = *joint,
                          .a = a ? a->body : ground(physics),
                          .b = b->body,
                          .owners = { a ? a->entity.slot : 0, b->entity.slot },
                          .frame = carried_offset(world, entity, b->entity),
                          .seen = true };
    if (b2Joint_IsValid(record->joint) && same_entity(record->entity, entity) &&
        same_joint(&record->settings, joint) && B2_ID_EQUALS(record->a, want.a) &&
        B2_ID_EQUALS(record->b, want.b) && same_transform(record->frame, want.frame)) {
        record->seen = true;
        return FLOW_OK;
    }
    remove_joint(record);
    want.joint = create_joint(physics, &want, (b2Vec2){ there.x, there.y });
    b2Joint_WakeBodies(want.joint);
    *record = want;
    return FLOW_OK;
}

static int sync_joints(physics *physics)
{
    for (int i = 0; i < acount(physics->joints); i++) {
        physics->joints[i].seen = false;
    }
    ecs_component_id id = COMPONENT_Joint.id;
    int result = FLOW_OK;
    if (ecs_world_component(physics->world, id)) {
        ecs_query query;
        result = ecs_query_begin(physics->world, &query, &id, 1);
        if (result != ECS_OK) {
            return result;
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (!released(physics, entity) &&
                (result = sync_joint(physics, entity)) < 0) {
                break;
            }
        }
        ecs_query_end(&query);
    }
    if (result < 0) {
        return result;
    }
    aclear(physics->apart);
    for (int i = 0; i < acount(physics->joints); i++) {
        joint_record *record = &physics->joints[i];
        if (record->entity.slot && !record->seen) {
            remove_joint(record);
        } else if (record->entity.slot && !record->settings.collide && record->owners[0]) {
            apush(physics->apart, slot_pair(record->owners[0], record->owners[1]));
        }
    }
    asort(physics->apart, compare_slot_pairs);
    return FLOW_OK;
}

static int synchronize(physics *physics, float delta)
{
    aclear(physics->forces);
    physics->body_count = 0;
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics->bodies[i].seen = false;
    }
    ecs_component_id required[] = { COMPONENT_Transform.id, COMPONENT_Collider.id };
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
            if (entities_is_retiring(physics->entities, entity)) {
                continue;
            }
            result = sync_entity(physics, entity);
            if (result < 0) {
                break;
            }
            physics->body_count++;
        }
        ecs_query_end(&query);
    }
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        if (body->entity.slot && !body->seen) {
            remove_body(physics, body);
        }
    }
    if (result == FLOW_OK) {
        result = place_followers(physics);
    }
    if (result == FLOW_OK) {
        result = sync_joints(physics);
    }
    for (int i = 0; i < acount(physics->bodies) && result == FLOW_OK; i++) {
        physics_body *body = &physics->bodies[i];
        if (body->seen && !body->follower) {
            result = set_motion(physics, body, delta);
        }
    }
    /* Commit drive forces only after every body validates. A failed sync can
     * be retried without accumulating force from the aborted tick. */
    if (result == FLOW_OK) {
        for (int i = 0; i < acount(physics->forces); i++) {
            drive_force force = physics->forces[i];
            b2Body_ApplyForceToCenter(force.body, force.force, true);
        }
    }
    aclear(physics->forces);
    return result;
}

static contact_pair *add_pair(contact_pair **pairs, const physics_body *a, const physics_body *b)
{
    uint64_t first = ecs_entity_to_u64(a->entity),
             second = ecs_entity_to_u64(b->entity);
    apush(
        *pairs,
        ((contact_pair){ .first = first < second ? first : second,
                         .second = first < second ? second : first,
                         .point = b2Lerp(a->end.p, b->end.p, .5f),
                         .fraction = 1 })
    );
    return &(*pairs)[acount(*pairs) - 1];
}

static float distance(const physics_body *a, b2Transform first, const physics_body *b, b2Transform second)
{
    float nearest = INFINITY;
    for (int i = 0; i < geometry_count(a); i++) {
        for (int j = 0; j < geometry_count(b); j++) {
            b2DistanceInput input = { .proxyA = geometry_at(a, i),
                                      .proxyB = geometry_at(b, j),
                                      .transformA = first,
                                      .transformB = second,
                                      .useRadii = true };
            b2SimplexCache cache = { 0 };
            nearest = fminf(nearest, b2ShapeDistance(&input, &cache, NULL, 0).distance);
            if (nearest == 0) {
                return 0;
            }
        }
    }
    return nearest;
}

/* The group a step gives the entity, read from the ECS: the topmost placed
 * entity it moves with, when that one has a collider. */
static unsigned read_group(const ecs_world *world, entities *entities, ecs_entity entity)
{
    ecs_entity top = entity;
    for (ecs_entity at = transform_parent(world, top); at.slot;
         at = transform_parent(world, top)) {
        top = at;
    }
    return Collider_has(world, top) && !entities_is_retiring(entities, top) ? top.slot
                                                                            : 0;
}

static bool read_geometry(ecs_world *world, entities *entities, ecs_entity entity, physics_body *body)
{
    const Collider *collider = Collider_get_const(world, entity);
    if (!Transform_has(world, entity) || !collider) {
        return false;
    }
    Transform position;
    float angle;
    world_pose(world, entity, &position, &angle);
    if (!valid(&position, angle, collider, &(Body){ 0 }, (CF_V2){ 0 })) {
        return false;
    }
    body->entity = entity;
    body->group = read_group(world, entities, entity);
    body->collider = *collider;
    body->mask = materials_mask(entities_materials(entities), entity);
    body->end = (b2Transform){ { position.x, position.y }, b2MakeRot(angle) };
    if (collider->shape == COLLIDER_CIRCLE) {
        b2Vec2 center = { 0 };
        body->geometry = b2MakeProxy(&center, 1, collider->radius);
        return true;
    }
    b2Polygon box = b2MakeBox(collider->width * .5f, collider->height * .5f);
    body->geometry = b2MakeProxy(box.vertices, box.count, box.radius);
    return true;
}

typedef struct
{
    ecs_entity entity;
    bool circle, sensor;
    b2Transform transform;
    b2Circle disk;
    b2Polygon box;
} ray_shape;
struct ray_scene
{
    ray_shape *shapes;
    b2DynamicTree tree;
};

ray_scene *ray_scene_new()
{
    ray_scene *scene = xcalloc(1, sizeof *scene);
    scene->tree = b2DynamicTree_Create();
    return scene;
}
void ray_scene_free(ray_scene *scene)
{
    b2DynamicTree_Destroy(&scene->tree);
    afree(scene->shapes);
    free(scene);
}

int ray_scene_refresh(ray_scene *scene, ecs_world *world, entities *entities)
{
    aclear(scene->shapes);
    b2DynamicTree_Destroy(&scene->tree);
    scene->tree = b2DynamicTree_Create();
    ecs_component_id required[] = { COMPONENT_Transform.id, COMPONENT_Collider.id };
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
        if (entities_is_retiring(entities, entity)) {
            continue;
        }
        physics_body body = { 0 };
        if (!read_geometry(world, entities, entity, &body)) {
            result = FLOW_ERR_ARGUMENT;
            break;
        }
        for (int i = 0; i < geometry_count(&body); i++) {
            ray_shape shape = { .entity = entity,
                                .circle = !body.mask && body.collider.shape == COLLIDER_CIRCLE,
                                .sensor = body.collider.sensor,
                                .transform = body.end };
            if (shape.circle) {
                shape.disk.radius = body.collider.radius;
            } else {
                shape.box = body.mask
                              ? rect_polygon(material_mask_collision(body.mask).rects[i])
                              : b2MakeBox(
                                    body.collider.width * .5f,
                                    body.collider.height * .5f
                                );
            }
            physics_body part = { .geometry = geometry_at(&body, i) };
            b2DynamicTree_CreateProxy(
                &scene->tree,
                bounds(&part, body.end),
                body.collider.category,
                (uint64_t)acount(scene->shapes)
            );
            apush(scene->shapes, shape);
        }
    }
    ecs_query_end(&query);
    return result;
}

typedef struct
{
    const ray_scene *scene;
    ecs_entity exclude;
    bool sensors;
    float length, nearest;
    ray_hit *out;
} ray_query;
static float cast_shape(const b2RayCastInput *ray, int, uint64_t index, void *user)
{
    ray_query *query = user;
    const ray_shape *shape = &query->scene->shapes[index];
    if (same_entity(shape->entity, query->exclude) ||
        (!query->sensors && shape->sensor)) {
        return ray->maxFraction;
    }
    b2RayCastInput local = {
        .origin = b2InvTransformPoint(shape->transform, ray->origin),
        .translation = b2InvRotateVector(shape->transform.q, ray->translation),
        .maxFraction = ray->maxFraction
    };
    bool inside = shape->circle ? b2PointInCircle(local.origin, &shape->disk)
                                : b2PointInPolygon(local.origin, &shape->box);
    b2CastOutput hit = shape->circle ? b2RayCastCircle(&local, &shape->disk)
                                     : b2RayCastPolygon(&local, &shape->box);
    if (inside) {
        hit = (b2CastOutput){ .hit = true,
                              .point = local.origin,
                              .normal = b2Neg(b2Normalize(local.translation)) };
    }
    ray_hit *out = query->out;
    if (!hit.hit || hit.fraction > query->nearest ||
        (out->found && hit.fraction == query->nearest &&
         ecs_entity_to_u64(shape->entity) > ecs_entity_to_u64(out->entity))) {
        return ray->maxFraction;
    }
    query->nearest = hit.fraction;
    b2Vec2 point = b2TransformPoint(shape->transform, hit.point),
           normal = b2RotateVector(shape->transform.q, hit.normal);
    *out = (ray_hit){ .found = true,
                      .entity = shape->entity,
                      .point = { point.x, point.y },
                      .normal = { normal.x, normal.y },
                      .distance = hit.fraction * query->length };
    /* Keep searching at fraction zero so overlapping origin hits have stable
     * handle ordering regardless of tree insertion order. */
    return fmaxf(hit.fraction, 0.0000001f);
}

int ray_scene_cast(
    const ray_scene *scene,
    CF_V2 start,
    CF_V2 end,
    unsigned categories,
    ecs_entity exclude,
    bool sensors,
    ray_hit *out
)
{
    *out = (ray_hit){ .point = end };
    b2RayCastInput ray = { .origin = { start.x, start.y },
                           .translation = { end.x - start.x, end.y - start.y },
                           .maxFraction = 1 };
    float maximum = 100000 * UNITS_PER_METER;
    if (!b2IsValidRay(&ray) || fabsf(start.x) >= maximum || fabsf(start.y) >= maximum ||
        fabsf(end.x) >= maximum || fabsf(end.y) >= maximum) {
        return FLOW_ERR_ARGUMENT;
    }
    ray_query query = { scene, exclude, sensors, b2Length(ray.translation),
                        1,     out };
    out->distance = query.length;
    if (categories) {
        b2DynamicTree_RayCast(&scene->tree, &ray, categories, cast_shape, &query);
    }
    return FLOW_OK;
}

int physics_raycast(
    ecs_world *world,
    entities *entities,
    CF_V2 start,
    CF_V2 end,
    unsigned categories,
    ecs_entity exclude,
    bool sensors,
    ray_hit *out
)
{
    ray_scene *scene = ray_scene_new();
    int result = ray_scene_refresh(scene, world, entities);
    if (result >= 0) {
        result = ray_scene_cast(scene, start, end, categories, exclude, sensors, out);
    }
    ray_scene_free(scene);
    return result;
}

/* The slot of the body a joint on or under entity holds, read from its
 * components as sync places it: the nearest collider at or above it, or the
 * root collider carrying that one. */
static unsigned read_owner(const ecs_world *world, entities *entities, ecs_entity entity)
{
    for (ecs_entity at = entity; at.slot; at = transform_parent(world, at)) {
        if (Collider_has(world, at)) {
            unsigned group = read_group(world, entities, at);
            return group ? group : at.slot;
        }
    }
    return 0;
}

/* Collects the bodies joints without collide join to owner's body. */
static int read_joined(ecs_world *world, entities *entities, unsigned owner, unsigned **out)
{
    ecs_component_id id = COMPONENT_Joint.id;
    if (!ecs_world_component(world, id)) {
        return FLOW_OK;
    }
    ecs_query query;
    int result = ecs_query_begin(world, &query, &id, 1);
    if (result != ECS_OK) {
        return result;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const Joint *joint = Joint_get_const(world, entity);
        ecs_entity other = ecs_entity_from_u64(joint->other);
        if (joint->collide || !other.slot || !ecs_entity_is_alive(world, other) ||
            entities_is_retiring(entities, entity) ||
            entities_is_retiring(entities, other) ||
            !Transform_has(world, entity) || !Transform_has(world, other)) {
            continue;
        }
        unsigned a = read_owner(world, entities, entity),
                 b = read_owner(world, entities, other);
        if (a == owner && b) {
            apush(*out, b);
        } else if (b == owner && a) {
            apush(*out, a);
        }
    }
    ecs_query_end(&query);
    return FLOW_OK;
}

static bool has_slot(const unsigned *slots, unsigned slot)
{
    for (int i = 0; i < acount(slots); i++) {
        if (slots[i] == slot) {
            return true;
        }
    }
    return false;
}

int physics_contact_entities(
    ecs_world *world,
    entities *entities,
    ecs_entity entity,
    unsigned categories,
    ecs_entity **out
)
{
    aclear(*out);
    if (!categories || !ecs_entity_is_alive(world, entity) ||
        entities_is_retiring(entities, entity)) {
        return FLOW_OK;
    }
    physics_body source = { 0 };
    if (!read_geometry(world, entities, entity, &source)) {
        return FLOW_ERR_ARGUMENT;
    }
    unsigned *joined = NULL;
    int result = read_joined(
        world,
        entities,
        source.group ? source.group : entity.slot,
        &joined
    );
    ecs_component_id required[] = { COMPONENT_Transform.id, COMPONENT_Collider.id };
    ecs_query query;
    if (result == FLOW_OK) {
        result = ecs_query_begin(world, &query, required, 2);
    }
    if (result != ECS_OK) {
        afree(joined);
        return result;
    }
    ecs_entity candidate;
    while (ecs_query_next(&query, &candidate)) {
        if (same_entity(entity, candidate) ||
            entities_is_retiring(entities, candidate)) {
            continue;
        }
        const Collider *collider = Collider_get_const(world, candidate);
        if (!(collider->category & categories)) {
            continue;
        }
        physics_body target = { 0 };
        if (!read_geometry(world, entities, candidate, &target)) {
            result = FLOW_ERR_ARGUMENT;
            break;
        }
        if (!eligible(&source, &target) ||
            has_slot(joined, target.group ? target.group : candidate.slot)) {
            continue;
        }
        /* Match Box2D's speculative solid contact distance. The query is rare
         * and reads ECS directly to include unsynchronized spawns/teleports. */
        float tolerance = source.collider.sensor || target.collider.sensor
                            ? 0
                            : .02f * UNITS_PER_METER;
        if (distance(&source, source.end, &target, target.end) <= tolerance) {
            apush(*out, candidate);
        }
    }
    ecs_query_end(&query);
    afree(joined);
    if (result < 0) {
        aclear(*out);
    }
    return result;
}

typedef struct
{
    physics *physics;
    physics_body *body, *nearest;
    contact_pair **pairs;
    unsigned **visited;
    float fraction;
    b2Vec2 point;
} pair_query;

static bool pair_seen(const pair_query *query, unsigned slot)
{
    for (int i = 0; i < acount(*query->visited); i++) {
        if ((*query->visited)[i] == slot) {
            return true;
        }
    }
    return false;
}

static bool overlap_candidate(b2ShapeId shape, void *user)
{
    pair_query *query = user;
    physics_body *a = query->body, *b = shape_body(query->physics, shape);
    if (b->entity.slot <= a->entity.slot || continuous_sensor(b) ||
        !eligible(a, b) || kept_apart(query->physics, a, b)) {
        return true;
    }
    /* A compound collider appears once per shape. Once any piece touches,
     * skip its remaining callbacks; misses must still allow later pieces. */
    if (pair_seen(query, b->entity.slot)) {
        return true;
    }
    physics_body part = { 0 };
    const physics_body *candidate = b;
    b2Transform at = b->end;
    if (b->parts) {
        /* The tree already selected this polygon. Comparing against every
         * polygon in its parent repeats distant terrain work for each hit.
         * The polygon sits where the body it rides on placed it. */
        b2Polygon polygon = b2Shape_GetPolygon(shape);
        part.geometry = b2MakeProxy(polygon.vertices, polygon.count, polygon.radius);
        candidate = &part;
        at = b2Body_GetTransform(b2Shape_GetBody(shape));
    }
    if (distance(a, a->end, candidate, at) == 0) {
        apush(*query->visited, b->entity.slot);
        add_pair(query->pairs, a, b);
    }
    return true;
}

static bool sweep_candidate(int, uint64_t slot, void *user)
{
    pair_query *query = user;
    physics_body *a = query->body, *b = &query->physics->bodies[slot];
    if (!eligible(a, b) || kept_apart(query->physics, a, b)) {
        return true;
    }
    float fraction = 0;
    b2Vec2 point = a->start.p;
    if (distance(a, a->start, b, b->start) > 0) {
        fraction = INFINITY;
        for (int i = 0; i < geometry_count(a); i++) {
            for (int j = 0; j < geometry_count(b); j++) {
                b2ShapeCastPairInput input = {
                    .proxyA = geometry_at(b, j),
                    .proxyB = geometry_at(a, i),
                    .transformA = b->start,
                    .transformB = a->start,
                    .translationB = b2Sub(
                        b2Sub(a->end.p, a->start.p),
                        b2Sub(b->end.p, b->start.p)
                    ),
                    .maxFraction = fminf(query->fraction, fraction)
                };
                b2CastOutput cast = b2ShapeCast(&input);
                if (cast.hit && cast.fraction < fraction) {
                    fraction = cast.fraction;
                    point = b2MulAdd(
                        cast.point,
                        fraction,
                        b2Sub(b->end.p, b->start.p)
                    );
                }
            }
        }
        if (!isfinite(fraction)) {
            return true;
        }
    }
    if (a->collider.pierce_fragments && b->pierceable_fragment) {
        contact_pair *pair = add_pair(query->pairs, a, b);
        pair->fraction = fraction;
        pair->point = point;
        return true;
    }
    if (fraction < query->fraction ||
        (fraction == query->fraction &&
         (!query->nearest || ecs_entity_to_u64(b->entity) <
                                 ecs_entity_to_u64(query->nearest->entity)))) {
        query->fraction = fraction;
        query->nearest = b;
        query->point = point;
    }
    return true;
}

typedef struct
{
    physics *physics;
    bool solved;
} collect_context;

static void collect_solved_contacts(pair_query *query, pair_buffer *buffer)
{
    physics *physics = query->physics;
    physics_body *body = query->body;
    /* Solved solids may have a small separation within Box2D's tolerance.
     * These pairs already use the same midpoint and fraction as overlaps. */
    int capacity = b2Body_GetContactCapacity(body->body);
    asetlen(buffer->contacts, capacity);
    int count = b2Body_GetContactData(body->body, buffer->contacts, capacity);
    for (int j = 0; j < count; j++) {
        b2ContactData contact = buffer->contacts[j];
        physics_body *a = shape_body(physics, contact.shapeIdA),
                     *b = shape_body(physics, contact.shapeIdB);
        /* Both bodies expose the contact. Only the lower slot owns the pair. */
        if (body->entity.slot !=
            (a->entity.slot < b->entity.slot ? a->entity.slot : b->entity.slot)) {
            continue;
        }
        physics_body *other = a == body ? b : a;
        if (!eligible(a, b) || continuous_sensor(a) || continuous_sensor(b) ||
            pair_seen(query, other->entity.slot)) {
            continue;
        }
        apush(*query->visited, other->entity.slot);
        add_pair(query->pairs, a, b);
    }
}

static void collect_body(const collect_context *context, int index, pair_buffer *buffer)
{
    physics *physics = context->physics;
    physics_body *body = &physics->bodies[index];
    if (!body->body.index1) {
        return;
    }
    pair_query query = { .physics = physics,
                         .body = body,
                         .pairs = &buffer->pairs,
                         .visited = &buffer->visited,
                         .fraction = 1 };
    aclear(buffer->visited);
    bool swept = continuous_sensor(body);
    if (context->solved && !swept && !body->collider.sensor) {
        /* Seed compound dedup before querying the spatial index, avoiding
         * repeated distance tests and sort entries for accepted contacts. */
        collect_solved_contacts(&query, buffer);
    }
    if (swept) {
        int first = acount(buffer->pairs);
        b2DynamicTree_Query(&physics->sweeps, swept_bounds(body), body->collider.mask, sweep_candidate, &query);
        /* Broad-phase order is arbitrary. Discard debris beyond the nearest
         * blocking surface, even if it was visited before that surface. */
        int count = first;
        for (int i = first; i < acount(buffer->pairs); i++) {
            if (buffer->pairs[i].fraction <= query.fraction) {
                buffer->pairs[count++] = buffer->pairs[i];
            }
        }
        asetlen(buffer->pairs, count);
    } else {
        /* Reuse the solver's spatial index. Keep the exact distance test and
         * current-pose semantics, including zero-delta synchronization. */
        b2QueryFilter filter = { .categoryBits = body->collider.category,
                                 .maskBits = body->collider.mask };
        b2World_OverlapAABB(physics->simulation, bounds(body, body->end), filter, overlap_candidate, &query);
    }
    if (query.nearest) {
        contact_pair *pair = add_pair(&buffer->pairs, body, query.nearest);
        pair->point = query.point;
        pair->fraction = query.fraction;
        body->stop_fraction = query.fraction;
    }
}

static void collect_range(int start, int end, uint32_t worker, void *user)
{
    const collect_context *context = user;
    assert(worker < (uint32_t)acount(context->physics->workers));
    pair_buffer *buffer = &context->physics->workers[worker];
    for (int i = start; i < end; i++) {
        collect_body(context, i, buffer);
    }
}

static void collect_pairs(physics *physics, bool solved, double *start)
{
    collect_context context = { physics, solved };
    int count = acount(physics->bodies);
    if (physics->active_workers < 2 || physics->body_count < PARALLEL_BODY_MINIMUM) {
        pair_buffer buffer = { physics->current, physics->contacts, physics->visited };
        for (int i = 0; i < count; i++) {
            collect_body(&context, i, &buffer);
        }
        physics->current = buffer.pairs;
        physics->contacts = buffer.contacts;
        physics->visited = buffer.visited;
        profile_phase(physics, start, &physics->profile.contact_collection_ms);
        return;
    }
    /* The solver and swept-tree writes have finished. Queries read immutable
     * geometry; only each body's own stop fraction and worker scratch change.
     * Merge after completion, then sort and deliver events on the caller. */
    for (int i = 0; i < acount(physics->workers); i++) {
        aclear(physics->workers[i].pairs);
    }
    void *task = physics->tasks
                     .enqueue(collect_range, count, 64, &context, physics->tasks.user);
    profile_phase(physics, start, &physics->profile.contact_enqueue_ms);
    if (task) {
        physics->tasks.finish(task, physics->tasks.user);
        profile_phase(physics, start, &physics->profile.contact_join_ms);
    }
    for (int i = 0; i < acount(physics->workers); i++) {
        const contact_pair *pairs = physics->workers[i].pairs;
        int length = acount(pairs), at = acount(physics->current);
        if (!length) {
            continue;
        }
        asetlen(physics->current, at + length);
        memcpy(physics->current + at, pairs, (size_t)length * sizeof *pairs);
    }
    profile_phase(physics, start, &physics->profile.contact_merge_ms);
}

static int compare_pairs(const void *left, const void *right)
{
    const contact_pair *a = left, *b = right;
    if (a->first != b->first) {
        return (a->first > b->first) - (a->first < b->first);
    }
    return (a->second > b->second) - (a->second < b->second);
}

/* Contact Begin or Contact End to both entities of pair; only Begin carries
 * the point. */
static int queue_contact(physics *physics, host_event event, contact_pair pair)
{
    const uint64_t targets[] = { pair.first, pair.second };
    CF_V2 position = { pair.point.x, pair.point.y };
    int result = FLOW_OK;
    for (int i = 0; i < 2; i++) {
        pin_value values[] = {
            { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = targets[1 - i] },
            { .type = PIN_STRUCT, .type_name = "vec2", .ptr = &position },
        };
        int queued = entities_request_event(
            physics->entities,
            ecs_entity_from_u64(targets[i]),
            host_event_name(event),
            (execution_arguments){ .values = values,
                                   .count = event == HOST_EVENT_CONTACT_BEGIN ? 2 : 1 }
        );
        if (queued < 0 && result == FLOW_OK) {
            result = queued;
        }
    }
    return result;
}

static void sort_pairs(physics *physics)
{
    int count = acount(physics->current);
    if (count < 64) {
        qsort(physics->current, (size_t)count, sizeof *physics->current, compare_pairs);
        return;
    }
    asetlen(physics->sort_scratch, count);
    contact_pair *input = physics->current, *output = physics->sort_scratch;
    uint64_t varying[2] = { 0 };
    for (int i = 1; i < count; i++) {
        varying[0] |= input[0].second ^ input[i].second;
        varying[1] |= input[0].first ^ input[i].first;
    }
    /* Stable radix passes establish the same unsigned lexicographic order as
     * compare_pairs. Generational handles often have only a few varying bytes. */
    for (int key = 0; key < 2; key++) {
        for (int shift = 0; shift < 64; shift += 8) {
            if (!((varying[key] >> shift) & 255)) {
                continue;
            }
            int offsets[256] = { 0 };
            for (int i = 0; i < count; i++) {
                uint64_t value = key ? input[i].first : input[i].second;
                offsets[(value >> shift) & 255]++;
            }
            int start = 0;
            for (int i = 0; i < 256; i++) {
                int length = offsets[i];
                offsets[i] = start;
                start += length;
            }
            for (int i = 0; i < count; i++) {
                uint64_t value = key ? input[i].first : input[i].second;
                output[offsets[(value >> shift) & 255]++] = input[i];
            }
            contact_pair *swap = input;
            input = output;
            output = swap;
        }
    }
    if (input != physics->current) {
        memcpy(physics->current, input, (size_t)count * sizeof *input);
    }
}

static int compare_hit_times(const void *left, const void *right)
{
    const contact_pair *a = left, *b = right;
    if (a->fraction != b->fraction) {
        return (a->fraction > b->fraction) - (a->fraction < b->fraction);
    }
    return compare_pairs(left, right);
}

/* Queues Contact End for the previous step's pairs that stopped touching,
 * then Contact Begin for this step's new pairs in travel order. */
static int dispatch_pairs(physics *physics)
{
    CHIBI_TRACE_ZONE_BEGIN(sort_zone, "Physics sort pairs");
    if (acount(physics->current) > 1) {
        sort_pairs(physics);
    }
    CHIBI_TRACE_ZONE_END(sort_zone);
    CHIBI_TRACE_ZONE_BEGIN(diff_zone, "Physics diff contacts");
    int count = 0, result = FLOW_OK;
    for (int i = 0; i < acount(physics->current); i++) {
        if (!count ||
            compare_pairs(&physics->current[i], &physics->current[count - 1])) {
            physics->current[count++] = physics->current[i];
        }
    }
    asetlen(physics->current, count);
    /* Both frames are sorted. Advance through the other frame once rather
     * than searching it separately for every pair. */
    const contact_pair *before = physics->previous, *after = physics->current;
    int before_count = acount(before);
    for (int i = 0, j = 0; i < before_count; i++) {
        while (j < count && compare_pairs(&after[j], &before[i]) < 0) {
            j++;
        }
        if (j == count || compare_pairs(&after[j], &before[i])) {
            int queued = queue_contact(physics, HOST_EVENT_CONTACT_END, before[i]);
            result = result == FLOW_OK ? queued : result;
        }
    }
    aclear(physics->begun);
    for (int i = 0, j = 0; i < count; i++) {
        while (j < before_count && compare_pairs(&before[j], &after[i]) < 0) {
            j++;
        }
        if (j == before_count || compare_pairs(&before[j], &after[i])) {
            apush(physics->begun, after[i]);
        }
    }
    /* Deliver swept impacts in travel order so a later blocking hit cannot
     * retire the projectile before earlier debris contacts carve their hulls. */
    if (acount(physics->begun) > 1) {
        asort(physics->begun, compare_hit_times);
    }
    for (int i = 0; i < acount(physics->begun); i++) {
        int queued = queue_contact(physics, HOST_EVENT_CONTACT_BEGIN, physics->begun[i]);
        result = result == FLOW_OK ? queued : result;
    }
    contact_pair *old = physics->previous;
    physics->previous = physics->current;
    physics->current = old;
    aclear(physics->current);
    CHIBI_TRACE_ZONE_END(diff_zone);
    return result;
}

static void update_poses(physics *physics)
{
    bool swept = false;
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        if (!body->body.index1) {
            continue;
        }
        if (physics->profiling) {
            physics->profile.bodies++;
        }
        body->end = b2Body_GetTransform(body->body);
        if (carried(body)) {
            body->end = b2MulTransforms(body->end, body->offset);
        }
        swept |= continuous_sensor(body);
    }
    if (!swept) {
        return;
    }
    /* Box2D's world queries index current shapes. A moving-target cast needs
     * bounds covering both endpoints, only while continuous sensors exist. */
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        if (!body->body.index1) {
            continue;
        }
        b2AABB next = swept_bounds(body);
        if (body->proxy >= 0 &&
            body->tree_bounds.lowerBound.x <= next.lowerBound.x &&
            body->tree_bounds.lowerBound.y <= next.lowerBound.y &&
            body->tree_bounds.upperBound.x >= next.upperBound.x &&
            body->tree_bounds.upperBound.y >= next.upperBound.y) {
            continue;
        }
        /* Keep conservative slack so steady motion does not remove/reinsert
         * every target each step. Exact relative-motion casts still decide hits. */
        b2Vec2 margin = { .5f + 4 * fabsf(body->end.p.x - body->start.p.x),
                          .5f + 4 * fabsf(body->end.p.y - body->start.p.y) };
        next.lowerBound = b2Sub(next.lowerBound, margin);
        next.upperBound = b2Add(next.upperBound, margin);
        if (body->proxy < 0) {
            body->proxy = b2DynamicTree_CreateProxy(
                &physics->sweeps,
                next,
                body->collider.category,
                body->entity.slot
            );
        } else {
            b2DynamicTree_EnlargeProxy(&physics->sweeps, body->proxy, next);
        }
        body->tree_bounds = next;
    }
    /* Repair moved branches together before any worker queries the tree.
     * Dense moving populations otherwise remove/reinsert each target. */
    b2DynamicTree_Rebuild(&physics->sweeps, false);
}

static void solve_fluid(physics *physics, float delta, double *start)
{
    materials *materials = entities_materials(physics->entities);
    if (!materials_enabled(materials) || delta <= 0) {
        return;
    }
    aclear(physics->fluid_bodies);
    aclear(physics->fluid_obstacles);
    aclear(physics->fluid_ids);
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics->bodies[i].fluid_part = 0;
    }
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        if (!body->body.index1 ||
            (body->collider.sensor &&
             !Projectile_get_const(physics->world, body->entity))) {
            continue;
        }
        /* A carried collider moves with its carrier's body, so what fluid
         * does to it pushes and turns that body as its carrier allows, and
         * every collider on that body shares one response. */
        physics_body *rigid = carried(body) ? &physics->bodies[body->group] : body;
        const Body *settings = &rigid->settings;
        b2Vec2 p = body->end.p, v = b2Body_GetLinearVelocity(body->body);
        b2Vec2 previous = body->start.p;
        float angle = b2Rot_GetAngle(body->end.q),
              previous_angle = b2Rot_GetAngle(body->start.q),
              angle_delta = remainderf(angle - previous_angle, 6.28318530718f);
        float dx = p.x - previous.x, dy = p.y - previous.y;
        float mass = b2Body_GetMass(body->body);
        float inertia = b2Body_GetRotationalInertia(body->body);
        b2Vec2 center = b2Body_GetWorldCenterOfMass(body->body);
        const Igniter *igniter = Igniter_get_const(physics->world, body->entity);
        int index = acount(physics->fluid_bodies), part_of = rigid->fluid_part;
        rigid->fluid_part = part_of ? part_of : index + 1;
        apush(physics->fluid_ids, body->body);
        apush(
            physics->fluid_bodies,
            ((fluid_body){
                .position = { p.x, p.y },
                .velocity = { v.x, v.y },
                .center_offset = { center.x - p.x, center.y - p.y },
                .previous_position = { previous.x, previous.y },
                .angle = angle,
                .previous_angle = previous_angle,
                .angular_velocity = b2Body_GetAngularVelocity(body->body),
                .heat = igniter ? igniter->heat : 0,
                .owner = ecs_entity_to_u64(body->entity),
                .swept = delta > 0 &&
                         (dx * dx + dy * dy > 1e-6f || fabsf(angle_delta) > 1e-4f),
                .inverse_inertia = settings->type == BODY_DYNAMIC &&
                                           settings->allow_rotation && inertia > 0
                                     ? 1 / inertia
                                     : 0,
                .inverse_mass = settings->type == BODY_DYNAMIC && mass > 0 ? 1 / mass : 0,
                .part_of = part_of })
        );
        for (int part = 0; part < geometry_count(body); part++) {
            b2ShapeProxy geometry = geometry_at(body, part);
            fluid_obstacle obstacle = { .body = index };
            if (geometry.count == 1) {
                obstacle.center = (fluid_vec){ geometry.points[0].x,
                                               geometry.points[0].y };
                obstacle.radius = geometry.radius;
            } else {
                b2Vec2 min = geometry.points[0], max = min;
                for (int j = 1; j < geometry.count; j++) {
                    min = b2Min(min, geometry.points[j]);
                    max = b2Max(max, geometry.points[j]);
                }
                obstacle.center = (fluid_vec){ (min.x + max.x) * .5f,
                                               (min.y + max.y) * .5f };
                obstacle.half_size = (fluid_vec){ (max.x - min.x) * .5f,
                                                  (max.y - min.y) * .5f };
            }
            apush(physics->fluid_obstacles, obstacle);
        }
    }
    profile_phase(physics, start, &physics->profile.fluid_prepare_ms);
    CHIBI_TRACE_ZONE_BEGIN(fluid_zone, "Fluid simulation");
    materials_tick_profiled(
        materials,
        delta,
        physics->fluid_bodies,
        acount(physics->fluid_bodies),
        physics->fluid_obstacles,
        acount(physics->fluid_obstacles),
        physics->profiling ? &physics->profile.material_fluid : NULL
    );
    CHIBI_TRACE_ZONE_END(fluid_zone);
    profile_phase(physics, start, &physics->profile.material_tick_ms);
    for (int i = 0; i < acount(physics->fluid_bodies); i++) {
        fluid_body body = physics->fluid_bodies[i];
        if (body.inverse_mass > 0 && (body.impulse.x || body.impulse.y)) {
            b2Body_ApplyLinearImpulseToCenter(
                physics->fluid_ids[i],
                (b2Vec2){ body.impulse.x, body.impulse.y },
                true
            );
        }
        if (body.inverse_inertia > 0 && body.angular_impulse != 0) {
            b2Body_ApplyAngularImpulse(physics->fluid_ids[i], body.angular_impulse, true);
        }
    }
    profile_phase(physics, start, &physics->profile.fluid_impulse_ms);
}

static int queue_push(physics *physics, ecs_entity entity, CF_V2 amount, bool impulse)
{
    const Body *body = ecs_entity_is_alive(physics->world, entity)
                         ? Body_get_const(physics->world, entity)
                         : NULL;
    if (!body || body->type != BODY_DYNAMIC || !isfinite(amount.x + amount.y)) {
        return FLOW_ERR_ARGUMENT;
    }
    apush(physics->pushes, ((push){ entity, { amount.x, amount.y }, impulse }));
    return FLOW_OK;
}

int physics_apply_impulse(physics *physics, ecs_entity entity, CF_V2 impulse)
{
    return queue_push(physics, entity, impulse, true);
}

int physics_apply_force(physics *physics, ecs_entity entity, CF_V2 force)
{
    return queue_push(physics, entity, force, false);
}

/* Applies the queued pushes to the dynamic bodies they still name. */
static void apply_pushes(physics *physics)
{
    for (int i = 0; i < acount(physics->pushes); i++) {
        push queued = physics->pushes[i];
        unsigned slot = queued.entity.slot;
        const physics_body *body = slot < (unsigned)acount(physics->bodies)
                                     ? &physics->bodies[slot]
                                     : NULL;
        if (!body ||
            ecs_entity_to_u64(body->entity) != ecs_entity_to_u64(queued.entity) ||
            !body->body.index1 || body->follower ||
            body->settings.type != BODY_DYNAMIC) {
            continue;
        }
        if (queued.impulse) {
            b2Body_ApplyLinearImpulseToCenter(body->body, queued.amount, true);
            continue;
        }
        b2Body_ApplyForceToCenter(body->body, queued.amount, true);
    }
    aclear(physics->pushes);
}

/* The scene's gravity: one entity's Gravity, or none. */
static int gravity(physics *physics, b2Vec2 *out)
{
    *out = (b2Vec2){ 0 };
    ecs_component_id id = COMPONENT_Gravity.id;
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
        CF_V2 acceleration = Gravity_get_const(physics->world, entity)->acceleration;
        *out = (b2Vec2){ acceleration.x, acceleration.y };
        count++;
    }
    ecs_query_end(&query);
    if (count > 1) {
        snprintf(physics->error, sizeof physics->error, "Only one entity may have Gravity; %d do.", count);
        return FLOW_ERR_ARGUMENT;
    }
    if (!isfinite(out->x) || !isfinite(out->y)) {
        snprintf(physics->error, sizeof physics->error, "Gravity acceleration must be finite.");
        return FLOW_ERR_ARGUMENT;
    }
    return FLOW_OK;
}

/* Box2D leaves sleeping bodies asleep through a gravity change. */
static void set_gravity(physics *physics, b2Vec2 acceleration)
{
    b2Vec2 current = b2World_GetGravity(physics->simulation);
    if (current.x == acceleration.x && current.y == acceleration.y) {
        return;
    }
    b2World_SetGravity(physics->simulation, acceleration);
    for (int i = 0; i < acount(physics->bodies); i++) {
        const physics_body *body = &physics->bodies[i];
        if (body->body.index1 && !body->follower && body->settings.type == BODY_DYNAMIC) {
            b2Body_SetAwake(body->body, true);
        }
    }
}

int physics_tick(physics *physics, float delta_seconds)
{
    physics->error[0] = 0;
    double start = 0;
    if (physics->profiling) {
        physics->profile = (physics_profile){ 0 };
        start = clock_ms();
    }
    if (!isfinite(delta_seconds) || delta_seconds < 0) {
        snprintf(physics->error, sizeof physics->error, "Physics delta must be finite and nonnegative.");
        return FLOW_ERR_ARGUMENT;
    }
    b2Vec2 acceleration;
    int result = gravity(physics, &acceleration);
    if (result < 0) {
        return result;
    }
    materials *materials = entities_materials(physics->entities);
    CHIBI_TRACE_ZONE_BEGIN(material_zone, "Material edits");
    const char *material_error = NULL;
    if (materials) {
        materials_set_gravity(
            materials,
            (fluid_vec){ acceleration.x, acceleration.y }
        );
        material_error = materials_prepare(materials);
    }
    CHIBI_TRACE_ZONE_END(material_zone);
    if (material_error) {
        snprintf(physics->error, sizeof physics->error, "%s", material_error);
        return FLOW_ERR_ARGUMENT;
    }
    set_gravity(physics, acceleration);
    CHIBI_TRACE_ZONE_BEGIN(sync_zone, "Physics synchronize");
    result = synchronize(physics, delta_seconds);
    CHIBI_TRACE_ZONE_END(sync_zone);
    profile_phase(physics, &start, &physics->profile.synchronize_ms);
    if (result < 0) {
        return result;
    }
    if (delta_seconds > 0) {
        apply_pushes(physics);
    }
    bool parallel = physics->body_count >= PARALLEL_BODY_MINIMUM;
    if (physics->pool) {
        task_pool_begin(physics->pool, parallel);
    }
    physics->active_workers = physics->tasks.workers > 1 && (!physics->pool || parallel)
                                ? physics->tasks.workers
                                : 1;
    CHIBI_TRACE_ZONE_BEGIN(solve_zone, "Physics solve");
    b2World_Step(physics->simulation, delta_seconds, physics->configuration.substeps);
    CHIBI_TRACE_ZONE_END(solve_zone);
    if (physics->profiling) {
        b2Profile profile = b2World_GetProfile(physics->simulation);
        physics->profile.box2d = (physics_box2d_profile){
            .step_ms = profile.step,
            .pairs_ms = profile.pairs,
            .collide_ms = profile.collide,
            .solve_ms = profile.solve,
            .merge_islands_ms = profile.mergeIslands,
            .prepare_stages_ms = profile.prepareStages,
            .solve_constraints_ms = profile.solveConstraints,
            .prepare_constraints_ms = profile.prepareConstraints,
            .integrate_velocities_ms = profile.integrateVelocities,
            .warm_start_ms = profile.warmStart,
            .solve_impulses_ms = profile.solveImpulses,
            .integrate_positions_ms = profile.integratePositions,
            .relax_impulses_ms = profile.relaxImpulses,
            .apply_restitution_ms = profile.applyRestitution,
            .store_impulses_ms = profile.storeImpulses,
            .split_islands_ms = profile.splitIslands,
            .transforms_ms = profile.transforms,
            .hit_events_ms = profile.hitEvents,
            .refit_ms = profile.refit,
            .bullets_ms = profile.bullets,
            .sleep_islands_ms = profile.sleepIslands,
            .sensors_ms = profile.sensors,
        };
    }
    profile_phase(physics, &start, &physics->profile.solve_ms);
    CHIBI_TRACE_ZONE_BEGIN(sweeps_zone, "Physics swept targets");
    update_poses(physics);
    CHIBI_TRACE_ZONE_END(sweeps_zone);
    profile_phase(physics, &start, &physics->profile.sweeps_ms);
    CHIBI_TRACE_ZONE_BEGIN(pairs_zone, "Physics collect pairs");
    double pairs_start = physics->profiling ? clock_ms() : 0;
    collect_pairs(physics, delta_seconds > 0, &start);
    if (physics->pool) {
        task_pool_end(physics->pool);
    }
    CHIBI_TRACE_ZONE_END(pairs_zone);
    if (physics->profiling) {
        physics->profile.pairs_ms = clock_ms() - pairs_start;
    }
    /* Fluid interaction must see the swept contact pose. Using the proposed
     * end pose lets a fast igniter heat fuel on the far side of a solid wall. */
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        /* A carried sensor never stops its carrier. */
        if (body->body.index1 && body->stop_fraction < 1 && !carried(body)) {
            body->end.p = b2Lerp(body->start.p, body->end.p, body->stop_fraction);
            b2Body_SetTransform(body->body, body->end.p, body->end.q);
        }
    }
    profile_phase(physics, &start, &physics->profile.position_clamp_ms);
    double fluid_start = physics->profiling ? clock_ms() : 0;
    solve_fluid(physics, delta_seconds, &start);
    if (physics->profiling) {
        physics->profile.fluid_ms = clock_ms() - fluid_start;
    }
    if (physics->profiling) {
        physics->profile.pairs = acount(physics->current);
    }
    CHIBI_TRACE_ZONE_BEGIN(writeback_zone, "Physics writeback");
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        if (!body->body.index1 || body->follower) {
            continue;
        }
        *Transform_get(physics->world, body->entity) = (Transform){ body->end.p.x,
                                                                    body->end.p.y };
        Orientation *orientation = Orientation_get(physics->world, body->entity);
        if (orientation && (body->settings.angular_velocity != 0 ||
                            body->settings.allow_rotation)) {
            orientation->radians = body->angle +
                                   remainderf(b2Rot_GetAngle(body->end.q) - body->angle, 6.28318530718f);
            /* Reading it back must not teleport the body: the angle's
             * round trip through a rotation is inexact and would drift. */
            body->angle = orientation->radians;
        }
        Velocity *velocity = Velocity_get(physics->world, body->entity);
        if (velocity) {
            b2Vec2 solved = b2Body_GetLinearVelocity(body->body);
            *velocity = (Velocity){ solved.x, solved.y };
        }
        if (body->settings.type == BODY_DYNAMIC) {
            Body_get(physics->world, body->entity)->mass = b2Body_GetMass(body->body);
        }
    }
    CHIBI_TRACE_ZONE_END(writeback_zone);
    profile_phase(physics, &start, &physics->profile.writeback_ms);
    CHIBI_TRACE_ZONE_BEGIN(dispatch_zone, "Physics queue contacts");
    result = dispatch_pairs(physics);
    CHIBI_TRACE_ZONE_END(dispatch_zone);
    profile_phase(physics, &start, &physics->profile.dispatch_ms);
    return result;
}
