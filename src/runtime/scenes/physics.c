#include "runtime/scenes/physics.h"

#include "diagnostics/tracy.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/value_array.h"
#include "runtime/behaviors/entities.h"
#include "runtime/generated.h"
#include "runtime/materials.h"
#include "runtime/task_pool.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <box2d/box2d.h>
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
    ecs_entity entity;
    b2BodyId body;
    b2ShapeId shape;
    b2ShapeProxy geometry;
    b2ShapeProxy *parts;
    const material_mask *mask; /* Borrowed only by immediate authored queries. */
    unsigned mask_revision;
    b2Transform start, end;
    b2AABB tree_bounds;
    GameCollider collider;
    GameBody settings;
    int proxy;
    float angle, stop_fraction;
    bool seen, pierceable_fragment;
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

struct game_physics
{
    ecs_world *world;
    game_entities *entities;
    b2WorldId simulation;
    b2DynamicTree sweeps;
    game_physics_settings configuration;
    physics_body *bodies;
    contact_pair *previous, *current, *sort_scratch, *begun;
    b2ContactData *contacts;
    unsigned *visited;
    drive_force *forces;
    fluid_body *fluid_bodies;
    fluid_obstacle *fluid_obstacles;
    b2BodyId *fluid_ids;
    bool profiling;
    game_physics_profile profile;
    game_task_system tasks;
    game_task_pool *pool;
    int body_count, active_workers;
    pair_buffer *workers;
    char error[256];
};

static double clock_ms()
{
    struct timespec now;
    int result = clock_gettime(CLOCK_MONOTONIC, &now);
    assert(!result);
    return now.tv_sec * 1000.0 + now.tv_nsec * 1e-6;
}

static void profile_phase(game_physics *physics, double *start, double *elapsed)
{
    if (!physics->profiling) {
        return;
    }
    double now = clock_ms();
    *elapsed = now - *start;
    *start = now;
}

void game_physics_set_profiling(game_physics *physics, bool enabled)
{
    physics->profiling = enabled;
    physics->profile = (game_physics_profile){ 0 };
}

const game_physics_profile *game_physics_last_profile(const game_physics *physics)
{
    return &physics->profile;
}

static bool same_entity(ecs_entity a, ecs_entity b)
{
    return ecs_entity_to_u64(a) == ecs_entity_to_u64(b);
}

static bool eligible(const physics_body *a, const physics_body *b)
{
    return !same_entity(a->entity, b->entity) &&
           (a->collider.category & b->collider.mask) &&
           (b->collider.category & a->collider.mask) &&
           a->collider.ignore_entity != ecs_entity_to_u64(b->entity) &&
           b->collider.ignore_entity != ecs_entity_to_u64(a->entity);
}

static physics_body *shape_body(game_physics *physics, b2ShapeId shape)
{
    return &physics->bodies[(uintptr_t)b2Shape_GetUserData(shape)];
}

static bool collision_filter(b2ShapeId first, b2ShapeId second, void *user)
{
    game_physics *physics = user;
    return eligible(shape_body(physics, first), shape_body(physics, second));
}

static void create_world(game_physics *physics)
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

static void free_workers(game_physics *physics)
{
    for (int i = 0; i < acount(physics->workers); i++) {
        afree(physics->workers[i].pairs);
        afree(physics->workers[i].contacts);
        afree(physics->workers[i].visited);
    }
    afree(physics->workers);
}

static void configure_tasks(game_physics *physics, const game_task_system *tasks)
{
    free_workers(physics);
    physics->tasks = tasks ? *tasks : (game_task_system){ 0 };
    if (tasks && tasks->workers > 1) {
        asetlen(physics->workers, tasks->workers);
        memset(physics->workers, 0, (size_t)tasks->workers * sizeof *physics->workers);
    }
    physics->active_workers = 1;
}

game_physics *game_physics_new(ecs_world *world, game_entities *entities)
{
    assert(world && entities);
    /* Box2D's unit configuration is process-wide, and precedes all defaults. */
    static bool initialized;
    if (!initialized) {
        b2SetLengthUnitsPerMeter(GAME_UNITS_PER_METER);
        initialized = true;
    }
    game_physics *physics = xcalloc(1, sizeof *physics);
    physics->world = world;
    physics->entities = entities;
    physics->configuration = game_physics_defaults();
    physics->pool = game_task_pool_acquire();
    configure_tasks(physics, game_task_pool_tasks(physics->pool));
    create_world(physics);
    return physics;
}

void game_physics_free(game_physics *physics)
{
    if (!physics) {
        return;
    }
    b2DestroyWorld(physics->simulation);
    b2DynamicTree_Destroy(&physics->sweeps);
    for (int i = 0; i < acount(physics->bodies); i++) {
        afree(physics->bodies[i].parts);
    }
    afree(physics->bodies);
    afree(physics->previous);
    afree(physics->current);
    afree(physics->sort_scratch);
    afree(physics->begun);
    afree(physics->contacts);
    afree(physics->visited);
    afree(physics->forces);
    afree(physics->fluid_bodies);
    afree(physics->fluid_obstacles);
    afree(physics->fluid_ids);
    free_workers(physics);
    if (physics->pool) {
        game_task_pool_release(physics->pool);
    }
    free(physics);
}

void game_physics_reset(game_physics *physics)
{
    physics->profile = (game_physics_profile){ 0 };
    b2DestroyWorld(physics->simulation);
    b2DynamicTree_Destroy(&physics->sweeps);
    for (int i = 0; i < acount(physics->bodies); i++) {
        afree(physics->bodies[i].parts);
    }
    aclear(physics->bodies);
    physics->body_count = 0;
    physics->active_workers = 1;
    aclear(physics->previous);
    aclear(physics->current);
    physics->error[0] = 0;
    create_world(physics);
}

const char *game_physics_error(const game_physics *physics)
{
    return physics->error;
}

int game_physics_configure(game_physics *physics, game_physics_settings settings)
{
    if (!game_physics_settings_valid(settings)) {
        snprintf(physics->error, sizeof physics->error, "Invalid physics settings: workers 0..32, substeps 1..16, contact frequency 0..240 Hz, damping ratio 0..100.");
        return FLOW_ERR_ARGUMENT;
    }
    if (settings.workers != physics->configuration.workers) {
        if (acount(physics->bodies) || !physics->pool) {
            snprintf(physics->error, sizeof physics->error, "Physics worker changes require a reset world with the runtime executor.");
            return FLOW_ERR_ARGUMENT;
        }
        game_task_pool *previous = physics->pool;
        physics->pool = game_task_pool_acquire_workers(settings.workers);
        configure_tasks(physics, game_task_pool_tasks(physics->pool));
        physics->configuration = settings;
        game_physics_reset(physics);
        game_task_pool_release(previous);
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

game_physics_settings game_physics_configuration(const game_physics *physics)
{
    return physics->configuration;
}

void game_physics_set_tasks(game_physics *physics, const game_task_system *tasks)
{
    assert(!acount(physics->bodies));
    assert(!tasks || (tasks->workers > 0 && tasks->enqueue && tasks->finish));
    game_task_pool *pool = physics->pool;
    physics->pool = NULL;
    configure_tasks(physics, tasks);
    game_physics_reset(physics);
    if (pool) {
        game_task_pool_release(pool);
    }
}

int game_physics_workers(const game_physics *physics)
{
    return physics->active_workers;
}

bool game_physics_interpolate(const game_physics *physics, ecs_entity entity, float alpha, GameTransform *out)
{
    assert(out && isfinite(alpha));
    if (!entity.slot || entity.slot >= (unsigned)acount(physics->bodies) ||
        !ecs_entity_is_alive(physics->world, entity) ||
        game_entities_is_retiring(physics->entities, entity)) {
        return false;
    }
    const physics_body *body = &physics->bodies[entity.slot];
    if (!body->body.index1 || !same_entity(body->entity, entity)) {
        return false;
    }
    const GameTransform *authored = GAME_GameTransform_get_const(physics->world, entity);
    if (!authored) {
        return false;
    }
    if (authored->x != body->end.p.x || authored->y != body->end.p.y) {
        *out = *authored;
        return true;
    }
    b2Vec2 position = b2Lerp(body->start.p, body->end.p, fminf(1, fmaxf(0, alpha)));
    *out = (GameTransform){ position.x, position.y };
    return true;
}

static bool valid(
    const GameTransform *position,
    float angle,
    const GameCollider *shape,
    const GameBody *body,
    CF_V2 velocity
)
{
    /* Match Box2D's large-coordinate bound before creating any proxies. */
    float maximum = 100000 * GAME_UNITS_PER_METER;
    float extent = shape->shape == GAME_COLLIDER_CIRCLE
                     ? shape->radius
                     : fmaxf(shape->width, shape->height);
    return isfinite(position->x) && isfinite(position->y) && isfinite(angle) &&
           fabsf(position->x) + extent < maximum &&
           fabsf(position->y) + extent < maximum && isfinite(velocity.x) &&
           isfinite(velocity.y) && body->type >= GAME_BODY_STATIC &&
           body->type <= GAME_BODY_KINEMATIC &&
           isfinite(body->angular_velocity) && isfinite(shape->restitution) &&
           shape->restitution >= 0 && shape->restitution <= 1 &&
           isfinite(shape->friction) && shape->friction >= 0 &&
           ((shape->shape == GAME_COLLIDER_CIRCLE && isfinite(shape->radius) &&
             shape->radius >= 0) ||
            (shape->shape == GAME_COLLIDER_BOX && isfinite(shape->width) &&
             shape->width > 0 && isfinite(shape->height) && shape->height > 0));
}

static bool same_configuration(const physics_body *body, const GameCollider *shape, const GameBody *settings)
{
    const GameCollider *old = &body->collider;
    return body->settings.type == settings->type &&
           body->settings.continuous == settings->continuous &&
           old->shape == shape->shape && old->radius == shape->radius &&
           old->width == shape->width && old->height == shape->height &&
           old->category == shape->category && old->mask == shape->mask &&
           old->sensor == shape->sensor &&
           old->pierce_fragments == shape->pierce_fragments &&
           old->ignore_entity == shape->ignore_entity &&
           old->restitution == shape->restitution && old->friction == shape->friction;
}

static void remove_body(game_physics *physics, physics_body *body)
{
    b2DestroyBody(body->body);
    if (body->proxy >= 0) {
        b2DynamicTree_DestroyProxy(&physics->sweeps, body->proxy);
    }
    afree(body->parts);
    *body = (physics_body){ 0 };
}

static b2Polygon rect_polygon(material_rect r)
{
    return b2MakeOffsetBox(r.width * .5f, r.height * .5f, (b2Vec2){ r.x, r.y }, b2Rot_identity);
}

static int geometry_count(const physics_body *body)
{
    if (body->parts) {
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

static void create_body(game_physics *physics, physics_body *body, b2Vec2 position, float angle)
{
    b2BodyDef def = b2DefaultBodyDef();
    switch (body->settings.type) {
        case GAME_BODY_STATIC: def.type = b2_staticBody; break;
        case GAME_BODY_DYNAMIC: def.type = b2_dynamicBody; break;
        case GAME_BODY_KINEMATIC: def.type = b2_kinematicBody; break;
    }
    def.position = position;
    def.rotation = b2MakeRot(angle);
    def.fixedRotation = body->settings.type != GAME_BODY_KINEMATIC;
    /* Box2D excludes bullet targets from CCD, so static/kinematic colliders
     * must stay eligible even when their authored Continuous flag is set. */
    def.isBullet = body->settings.type == GAME_BODY_DYNAMIC &&
                   body->settings.continuous && !body->collider.sensor;
    body->body = b2CreateBody(physics->simulation, &def);
    b2ShapeDef shape = b2DefaultShapeDef();
    shape.userData = (void *)(uintptr_t)body->entity.slot;
    shape.material.friction = body->collider.friction;
    shape.material.restitution = body->collider.restitution;
    shape.density = 1.0f / (GAME_UNITS_PER_METER * GAME_UNITS_PER_METER);
    shape.filter.categoryBits = body->collider.category;
    shape.filter.maskBits = body->collider.mask;
    shape.isSensor = body->collider.sensor;
    /* Gameplay contacts are collected below with current-pose and swept-sensor
     * semantics. No consumer reads Box2D's separate buffered event streams. */
    shape.enableSensorEvents = false;
    shape.enableContactEvents = false;
    const material_mask *mask = game_materials_mask(
        game_entities_materials(physics->entities),
        body->entity
    );
    body->mask_revision = mask ? mask->revision : 0;
    if (mask) {
        for (int i = 0; i < material_mask_collision(mask).count; i++) {
            b2Polygon box = rect_polygon(material_mask_collision(mask).rects[i]);
            body->shape = b2CreatePolygonShape(body->body, &shape, &box);
            apush(body->parts, b2MakeProxy(box.vertices, box.count, box.radius));
        }
    } else if (body->collider.shape == GAME_COLLIDER_CIRCLE) {
        b2Circle circle = { .radius = body->collider.radius };
        body->shape = b2CreateCircleShape(body->body, &shape, &circle);
        body->geometry = b2MakeProxy(&circle.center, 1, circle.radius);
    } else {
        b2Polygon box = b2MakeBox(body->collider.width * .5f, body->collider.height * .5f);
        body->shape = b2CreatePolygonShape(body->body, &shape, &box);
        body->geometry = b2MakeProxy(box.vertices, box.count, box.radius);
    }
    body->start = body->end = b2Body_GetTransform(body->body);
    body->proxy = -1;
}

static int sync_entity(game_physics *physics, ecs_entity entity, float delta)
{
    ecs_world *world = physics->world;
    GameTransform position = *GAME_GameTransform_get_const(world, entity);
    GameCollider collider = *GAME_GameCollider_get_const(world, entity);
    const GameBody *authored = GAME_GameBody_get_const(world, entity);
    GameBody settings = authored ? *authored : (GameBody){ .type = GAME_BODY_STATIC };
    const GameOrientation *orientation = GAME_GameOrientation_get_const(world, entity);
    float angle = orientation ? orientation->radians : 0;
    const GameVelocity *velocity = GAME_GameVelocity_get_const(world, entity);
    const GameDrive *drive = GAME_GameDrive_get_const(world, entity);
    CF_V2 movement = velocity ? *velocity : (CF_V2){ 0 };
    if (!valid(&position, angle, &collider, &settings, movement)) {
        snprintf(
            physics->error,
            sizeof physics->error,
            "Invalid Body, Collider, Transform or Velocity on entity %u.",
            entity.slot
        );
        return FLOW_ERR_ARGUMENT;
    }
    if (settings.angular_velocity != 0 &&
        (settings.type != GAME_BODY_KINEMATIC || !orientation)) {
        snprintf(
            physics->error,
            sizeof physics->error,
            "Angular velocity requires a kinematic Body and Orientation on "
            "entity %u.",
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
    const material_mask
        *mask = game_materials_mask(game_entities_materials(physics->entities), entity);
    unsigned revision = mask ? mask->revision : 0;
    if (body->body.index1 &&
        (!same_entity(body->entity, entity) || body->mask_revision != revision ||
         !same_configuration(body, &collider, &settings))) {
        remove_body(physics, body);
    }
    body->entity = entity;
    body->collider = collider;
    body->settings = settings;
    body->seen = true;
    body->pierceable_fragment = game_materials_is_pierceable_fragment(
        game_entities_materials(physics->entities),
        entity
    );
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
    body->stop_fraction = 1;
    b2Body_SetAngularVelocity(body->body, settings.angular_velocity);
    if (drive && settings.type == GAME_BODY_DYNAMIC && delta > 0) {
        if (!isfinite(drive->target.x + drive->target.y + drive->acceleration) ||
            drive->acceleration < 0) {
            snprintf(
                physics->error,
                sizeof physics->error,
                "Drive requires a finite target and nonnegative acceleration "
                "on entity %u.",
                entity.slot
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
    } else if (physics->configuration.force_drive && settings.type == GAME_BODY_DYNAMIC && delta > 0) {
        b2Vec2 current = b2Body_GetLinearVelocity(body->body);
        double scale = (double)b2Body_GetMass(body->body) / delta;
        b2Vec2 force = { (float)(scale * (movement.x - current.x)),
                         (float)(scale * (movement.y - current.y)) };
        if (!isfinite(force.x) || !isfinite(force.y)) {
            snprintf(
                physics->error,
                sizeof physics->error,
                "Non-finite velocity drive force on entity %u.",
                entity.slot
            );
            return FLOW_ERR_ARGUMENT;
        }
        if (force.x != 0 || force.y != 0) {
            apush(physics->forces, ((drive_force){ body->body, force }));
        }
    } else {
        b2Body_SetLinearVelocity(body->body, (b2Vec2){ movement.x, movement.y });
    }
    body->start = b2Body_GetTransform(body->body);
    return FLOW_OK;
}

static int synchronize(game_physics *physics, float delta)
{
    aclear(physics->forces);
    physics->body_count = 0;
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics->bodies[i].seen = false;
    }
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameCollider.id };
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
            result = sync_entity(physics, entity, delta);
            if (result < 0) {
                break;
            }
            physics->body_count++;
        }
        ecs_query_end(&query);
    }
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        if (body->body.index1 && !body->seen) {
            remove_body(physics, body);
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

static bool continuous_sensor(const physics_body *body)
{
    return body->settings.continuous && body->collider.sensor;
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

static bool read_geometry(ecs_world *world, game_entities *entities, ecs_entity entity, physics_body *body)
{
    const GameTransform *position = GAME_GameTransform_get_const(world, entity);
    const GameCollider *collider = GAME_GameCollider_get_const(world, entity);
    const GameOrientation *orientation = GAME_GameOrientation_get_const(world, entity);
    float angle = orientation ? orientation->radians : 0;
    if (!position || !collider ||
        !valid(position, angle, collider, &(GameBody){ 0 }, (CF_V2){ 0 })) {
        return false;
    }
    body->entity = entity;
    body->collider = *collider;
    body->mask = game_materials_mask(game_entities_materials(entities), entity);
    body->end = (b2Transform){ { position->x, position->y }, b2MakeRot(angle) };
    if (collider->shape == GAME_COLLIDER_CIRCLE) {
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
struct game_ray_scene
{
    ray_shape *shapes;
    b2DynamicTree tree;
};

game_ray_scene *game_ray_scene_new()
{
    game_ray_scene *scene = xcalloc(1, sizeof *scene);
    scene->tree = b2DynamicTree_Create();
    return scene;
}
void game_ray_scene_free(game_ray_scene *scene)
{
    b2DynamicTree_Destroy(&scene->tree);
    afree(scene->shapes);
    free(scene);
}

int game_ray_scene_refresh(game_ray_scene *scene, ecs_world *world, game_entities *entities)
{
    aclear(scene->shapes);
    b2DynamicTree_Destroy(&scene->tree);
    scene->tree = b2DynamicTree_Create();
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameCollider.id };
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
        if (game_entities_is_retiring(entities, entity)) {
            continue;
        }
        physics_body body = { 0 };
        if (!read_geometry(world, entities, entity, &body)) {
            result = FLOW_ERR_ARGUMENT;
            break;
        }
        for (int i = 0; i < geometry_count(&body); i++) {
            ray_shape shape = { .entity = entity,
                                .circle = !body.mask && body.collider.shape ==
                                                            GAME_COLLIDER_CIRCLE,
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
    const game_ray_scene *scene;
    ecs_entity exclude;
    bool sensors;
    float length, nearest;
    game_ray_hit *out;
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
    game_ray_hit *out = query->out;
    if (!hit.hit || hit.fraction > query->nearest ||
        (out->found && hit.fraction == query->nearest &&
         ecs_entity_to_u64(shape->entity) > ecs_entity_to_u64(out->entity))) {
        return ray->maxFraction;
    }
    query->nearest = hit.fraction;
    b2Vec2 point = b2TransformPoint(shape->transform, hit.point),
           normal = b2RotateVector(shape->transform.q, hit.normal);
    *out = (game_ray_hit){ .found = true,
                           .entity = shape->entity,
                           .point = { point.x, point.y },
                           .normal = { normal.x, normal.y },
                           .distance = hit.fraction * query->length };
    /* Keep searching at fraction zero so overlapping origin hits have stable
     * handle ordering regardless of tree insertion order. */
    return fmaxf(hit.fraction, 0.0000001f);
}

int game_ray_scene_cast(
    const game_ray_scene *scene,
    CF_V2 start,
    CF_V2 end,
    unsigned categories,
    ecs_entity exclude,
    bool sensors,
    game_ray_hit *out
)
{
    *out = (game_ray_hit){ .point = end };
    b2RayCastInput ray = { .origin = { start.x, start.y },
                           .translation = { end.x - start.x, end.y - start.y },
                           .maxFraction = 1 };
    float maximum = 100000 * GAME_UNITS_PER_METER;
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

int game_physics_raycast(
    ecs_world *world,
    game_entities *entities,
    CF_V2 start,
    CF_V2 end,
    unsigned categories,
    ecs_entity exclude,
    bool sensors,
    game_ray_hit *out
)
{
    game_ray_scene *scene = game_ray_scene_new();
    int result = game_ray_scene_refresh(scene, world, entities);
    if (result >= 0) {
        result = game_ray_scene_cast(scene, start, end, categories, exclude, sensors, out);
    }
    game_ray_scene_free(scene);
    return result;
}

static const char *raycast_node(
    const eval_context *ctx,
    const pin_value *inputs,
    arena *storage,
    pin_value *out
)
{
    ecs_world *world = eval_context_service(ctx, "ecs.world");
    game_entities *entities = eval_context_service(ctx, "game.entities");
    if (!world || !entities || !inputs[0].ptr || !inputs[1].ptr) {
        return "Raycast requires a game world and Start and End vectors";
    }
    game_ray_hit hit;
    if (game_physics_raycast(
            world,
            entities,
            *(CF_V2 *)inputs[0].ptr,
            *(CF_V2 *)inputs[1].ptr,
            inputs[2].u,
            ecs_entity_from_u64(inputs[3].u64),
            inputs[4].b,
            &hit
        ) < 0) {
        return "Raycast requires finite coordinates and valid colliders";
    }
    CF_V2 *point = arena_alloc(storage, sizeof *point);
    CF_V2 *normal = arena_alloc(storage, sizeof *normal);
    *point = hit.point;
    *normal = hit.normal;
    out[0] = (pin_value){ .type = PIN_BOOL, .b = hit.found };
    out[1] = (pin_value){ .type = PIN_HANDLE,
                          .type_name = "ecs.entity",
                          .u64 = ecs_entity_to_u64(hit.entity) };
    out[2] = (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = point };
    out[3] = (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = normal };
    out[4] = (pin_value){ .type = PIN_F32, .f = hit.distance };
    return NULL;
}

static pin_spec RAY_IN[] = {
    { .name = "Start", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "End", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Categories", .type = PIN_U32, .type_name = "unsigned int" },
    { .name = "Exclude", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Include Sensors", .type = PIN_BOOL },
};
static pin_spec RAY_OUT[] = {
    { .name = "Found", .type = PIN_BOOL },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Point", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Normal", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Distance", .type = PIN_F32 },
};
static const node_access RAY_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
node_desc GAME_NODE_Raycast = {
    .name = "Raycast",
    .input_count = 5,
    .inputs = RAY_IN,
    .output_count = 5,
    .outputs = RAY_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = raycast_node,
    .category = "Physics",
    .keywords = "ray segment visibility line sight wall occlusion ricochet "
                "normal",
    .access = &RAY_ACCESS,
};

int game_physics_contact_entities(
    ecs_world *world,
    game_entities *entities,
    ecs_entity entity,
    unsigned categories,
    ecs_entity **out
)
{
    aclear(*out);
    if (!categories || !ecs_entity_is_alive(world, entity) ||
        game_entities_is_retiring(entities, entity)) {
        return FLOW_OK;
    }
    physics_body source = { 0 };
    if (!read_geometry(world, entities, entity, &source)) {
        return FLOW_ERR_ARGUMENT;
    }
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameCollider.id };
    ecs_query query;
    int result = ecs_query_begin(world, &query, required, 2);
    if (result != ECS_OK) {
        return result;
    }
    ecs_entity candidate;
    while (ecs_query_next(&query, &candidate)) {
        if (same_entity(entity, candidate) ||
            game_entities_is_retiring(entities, candidate)) {
            continue;
        }
        const GameCollider *collider = GAME_GameCollider_get_const(world, candidate);
        if (!(collider->category & categories)) {
            continue;
        }
        physics_body target = { 0 };
        if (!read_geometry(world, entities, candidate, &target)) {
            result = FLOW_ERR_ARGUMENT;
            break;
        }
        if (!eligible(&source, &target)) {
            continue;
        }
        /* Match Box2D's speculative solid contact distance. The query is rare
         * and reads ECS directly to include unsynchronized spawns/teleports. */
        float tolerance = source.collider.sensor || target.collider.sensor
                            ? 0
                            : .02f * GAME_UNITS_PER_METER;
        if (distance(&source, source.end, &target, target.end) <= tolerance) {
            apush(*out, candidate);
        }
    }
    ecs_query_end(&query);
    if (result < 0) {
        aclear(*out);
    }
    return result;
}

static const char *contact_entities(
    const eval_context *ctx,
    const pin_value *inputs,
    arena *a,
    pin_value *out
)
{
    ecs_world *world = eval_context_service(ctx, "ecs.world");
    game_entities *entities = eval_context_service(ctx, "game.entities");
    if (!world || !entities) {
        return "Get Contact Entities requires a game world";
    }
    ecs_entity *handles = NULL;
    int result = game_physics_contact_entities(
        world,
        entities,
        ecs_entity_from_u64(inputs[0].u64),
        inputs[1].u,
        &handles
    );
    if (result < 0) {
        afree(handles);
        return "Get Contact Entities requires valid Collider and Transform "
               "components";
    }
    pin_array *array = arena_alloc(a, sizeof *array);
    *array = (pin_array){ .element_type = PIN_HANDLE,
                          .type_name = "ecs.entity",
                          .count = acount(handles),
                          .capacity = acount(handles) };
    if (array->count) {
        array->items = arena_alloc(a, (size_t)array->count * sizeof *array->items);
        for (int i = 0; i < array->count; i++) {
            array->items[i] = (pin_value){ .type = PIN_HANDLE,
                                           .type_name = "ecs.entity",
                                           .u64 = ecs_entity_to_u64(handles[i]) };
        }
    }
    afree(handles);
    *out = (pin_value){ .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = array };
    return NULL;
}

static pin_spec CONTACT_IN[] = {
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Categories", .type = PIN_U32, .type_name = "unsigned int" },
};
static pin_spec CONTACT_OUT[] = {
    { .name = "Entities", .type = PIN_HANDLE, .type_name = "ecs.entity", .container = PIN_CONTAINER_ARRAY },
};
static const node_access CONTACT_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
node_desc GAME_NODE_GetContactEntities = {
    .name = "Get Contact Entities",
    .input_count = 2,
    .inputs = CONTACT_IN,
    .output_count = 1,
    .outputs = CONTACT_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = contact_entities,
    .category = "Physics",
    .keywords = "contact overlap touching collider entities query categories",
    .access = &CONTACT_ACCESS,
};

typedef struct
{
    game_physics *physics;
    physics_body *body, *nearest;
    contact_pair **pairs;
    unsigned **visited;
    float fraction;
    b2Vec2 point;
} pair_query;

static bool overlap_candidate(b2ShapeId shape, void *user)
{
    pair_query *query = user;
    physics_body *a = query->body, *b = shape_body(query->physics, shape);
    if (b->entity.slot <= a->entity.slot || continuous_sensor(b) || !eligible(a, b)) {
        return true;
    }
    /* A compound collider appears once per shape. Once any piece touches,
     * skip its remaining callbacks; misses must still allow later pieces. */
    for (int i = 0; i < acount(*query->visited); i++) {
        if ((*query->visited)[i] == b->entity.slot) {
            return true;
        }
    }
    physics_body part = { 0 };
    const physics_body *candidate = b;
    if (b->parts) {
        /* The tree already selected this polygon. Comparing against every
         * polygon in its parent repeats distant terrain work for each hit. */
        b2Polygon polygon = b2Shape_GetPolygon(shape);
        part.geometry = b2MakeProxy(polygon.vertices, polygon.count, polygon.radius);
        candidate = &part;
    }
    if (distance(a, a->end, candidate, b->end) == 0) {
        apush(*query->visited, b->entity.slot);
        add_pair(query->pairs, a, b);
    }
    return true;
}

static bool sweep_candidate(int, uint64_t slot, void *user)
{
    pair_query *query = user;
    physics_body *a = query->body, *b = &query->physics->bodies[slot];
    if (!eligible(a, b)) {
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
    game_physics *physics;
    bool solved;
} collect_context;

static void collect_body(const collect_context *context, int index, pair_buffer *buffer)
{
    game_physics *physics = context->physics;
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
    if (!context->solved || swept || body->collider.sensor) {
        return;
    }
    /* Solved solid contacts can have a small separation within Box2D's
     * contact tolerance. Keep those pairs active instead of retriggering. */
    int capacity = b2Body_GetContactCapacity(body->body);
    asetlen(buffer->contacts, capacity);
    int count = b2Body_GetContactData(body->body, buffer->contacts, capacity);
    for (int j = 0; j < count; j++) {
        b2ContactData contact = buffer->contacts[j];
        physics_body *a = shape_body(physics, contact.shapeIdA),
                     *b = shape_body(physics, contact.shapeIdB);
        /* Box2D exposes this contact through both shapes. Collect it once;
         * the canonical event order is still established below. */
        if (body->entity.slot !=
            (a->entity.slot < b->entity.slot ? a->entity.slot : b->entity.slot)) {
            continue;
        }
        if (eligible(a, b) && !continuous_sensor(a) && !continuous_sensor(b)) {
            add_pair(&buffer->pairs, a, b);
        }
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

static void collect_pairs(game_physics *physics, bool solved)
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
    if (task) {
        physics->tasks.finish(task, physics->tasks.user);
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
}

static int compare_pairs(const void *left, const void *right)
{
    const contact_pair *a = left, *b = right;
    if (a->first != b->first) {
        return (a->first > b->first) - (a->first < b->first);
    }
    return (a->second > b->second) - (a->second < b->second);
}

static int queue_contact(game_physics *physics, uint64_t target, uint64_t other, b2Vec2 point)
{
    CF_V2 position = { point.x, point.y };
    pin_value values[] = {
        { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = other },
        { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &position },
    };
    return game_entities_request_event(
        physics->entities,
        ecs_entity_from_u64(target),
        "Contact Begin",
        (flow_inputs){ .values = values, .count = 2 }
    );
}

static void sort_pairs(game_physics *physics)
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

static int dispatch_pairs(game_physics *physics)
{
    CHIBI_TRACE_ZONE_BEGIN(sort_zone, "Physics sort pairs");
    if (acount(physics->current) > 1) {
        sort_pairs(physics);
    }
    CHIBI_TRACE_ZONE_END(sort_zone);
    CHIBI_TRACE_ZONE_BEGIN(diff_zone, "Physics diff contacts");
    int count = 0, previous = 0, result = FLOW_OK;
    aclear(physics->begun);
    for (int i = 0; i < acount(physics->current); i++) {
        contact_pair pair = physics->current[i];
        if (count && !compare_pairs(&pair, &physics->current[count - 1])) {
            continue;
        }
        physics->current[count++] = pair;
        /* Both frames are sorted. Advance through the old frame once rather
         * than searching it separately for every persistent contact. */
        while (previous < acount(physics->previous) &&
               compare_pairs(&physics->previous[previous], &pair) < 0) {
            previous++;
        }
        if (previous < acount(physics->previous) &&
            !compare_pairs(&physics->previous[previous], &pair)) {
            continue;
        }
        apush(physics->begun, pair);
    }
    /* Deliver swept impacts in travel order so a later blocking hit cannot
     * retire the projectile before earlier debris contacts carve their hulls. */
    if (acount(physics->begun) > 1) {
        qsort(physics->begun, (size_t)acount(physics->begun), sizeof *physics->begun, compare_hit_times);
    }
    for (int i = 0; i < acount(physics->begun); i++) {
        contact_pair pair = physics->begun[i];
        int first = queue_contact(physics, pair.first, pair.second, pair.point),
            second = queue_contact(physics, pair.second, pair.first, pair.point);
        if (first < 0 || second < 0) {
            result = first < 0 ? first : second;
        }
    }
    asetlen(physics->current, count);
    contact_pair *old = physics->previous;
    physics->previous = physics->current;
    physics->current = old;
    aclear(physics->current);
    CHIBI_TRACE_ZONE_END(diff_zone);
    return result;
}

static void update_poses(game_physics *physics)
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

static void solve_fluid(game_physics *physics, float delta)
{
    game_materials *materials = game_entities_materials(physics->entities);
    if (!game_materials_enabled(materials) || delta <= 0) {
        return;
    }
    aclear(physics->fluid_bodies);
    aclear(physics->fluid_obstacles);
    aclear(physics->fluid_ids);
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        if (!body->body.index1 ||
            (body->collider.sensor &&
             !GAME_GameProjectile_get_const(physics->world, body->entity))) {
            continue;
        }
        b2Vec2 p = b2Body_GetPosition(body->body),
               v = b2Body_GetLinearVelocity(body->body);
        float mass = b2Body_GetMass(body->body);
        const GameIgniter *igniter = GAME_GameIgniter_get_const(physics->world, body->entity);
        int index = acount(physics->fluid_bodies);
        apush(physics->fluid_ids, body->body);
        apush(
            physics->fluid_bodies,
            ((fluid_body){ .position = { p.x, p.y },
                           .velocity = { v.x, v.y },
                           .angle = body->angle,
                           .heat = igniter ? igniter->heat : 0,
                           .owner = ecs_entity_to_u64(body->entity),
                           .inverse_mass = body->settings.type == GAME_BODY_DYNAMIC && mass > 0
                                             ? 1 / mass
                                             : 0 })
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
    CHIBI_TRACE_ZONE_BEGIN(fluid_zone, "Fluid simulation");
    game_materials_tick(
        materials,
        delta,
        physics->fluid_bodies,
        acount(physics->fluid_bodies),
        physics->fluid_obstacles,
        acount(physics->fluid_obstacles)
    );
    CHIBI_TRACE_ZONE_END(fluid_zone);
    for (int i = 0; i < acount(physics->fluid_bodies); i++) {
        fluid_body body = physics->fluid_bodies[i];
        if (body.inverse_mass > 0 && (body.impulse.x || body.impulse.y)) {
            b2Body_ApplyLinearImpulseToCenter(
                physics->fluid_ids[i],
                (b2Vec2){ body.impulse.x, body.impulse.y },
                true
            );
        }
    }
}

int game_physics_tick(game_physics *physics, float delta_seconds)
{
    physics->error[0] = 0;
    double start = 0;
    if (physics->profiling) {
        physics->profile = (game_physics_profile){ 0 };
        start = clock_ms();
    }
    if (!isfinite(delta_seconds) || delta_seconds < 0) {
        snprintf(physics->error, sizeof physics->error, "Physics delta must be finite and nonnegative.");
        return FLOW_ERR_ARGUMENT;
    }
    game_materials *materials = game_entities_materials(physics->entities);
    CHIBI_TRACE_ZONE_BEGIN(material_zone, "Material edits");
    const char *material_error = materials ? game_materials_prepare(materials) : NULL;
    CHIBI_TRACE_ZONE_END(material_zone);
    if (material_error) {
        snprintf(physics->error, sizeof physics->error, "%s", material_error);
        return FLOW_ERR_ARGUMENT;
    }
    CHIBI_TRACE_ZONE_BEGIN(sync_zone, "Physics synchronize");
    int result = synchronize(physics, delta_seconds);
    CHIBI_TRACE_ZONE_END(sync_zone);
    profile_phase(physics, &start, &physics->profile.synchronize_ms);
    if (result < 0) {
        return result;
    }
    bool parallel = physics->body_count >= PARALLEL_BODY_MINIMUM;
    if (physics->pool) {
        game_task_pool_begin(physics->pool, parallel);
    }
    physics->active_workers = physics->tasks.workers > 1 && (!physics->pool || parallel)
                                ? physics->tasks.workers
                                : 1;
    CHIBI_TRACE_ZONE_BEGIN(solve_zone, "Physics solve");
    b2World_Step(physics->simulation, delta_seconds, physics->configuration.substeps);
    CHIBI_TRACE_ZONE_END(solve_zone);
    profile_phase(physics, &start, &physics->profile.solve_ms);
    CHIBI_TRACE_ZONE_BEGIN(sweeps_zone, "Physics swept targets");
    update_poses(physics);
    CHIBI_TRACE_ZONE_END(sweeps_zone);
    profile_phase(physics, &start, &physics->profile.sweeps_ms);
    CHIBI_TRACE_ZONE_BEGIN(pairs_zone, "Physics collect pairs");
    collect_pairs(physics, delta_seconds > 0);
    if (physics->pool) {
        game_task_pool_end(physics->pool);
    }
    CHIBI_TRACE_ZONE_END(pairs_zone);
    /* Fluid interaction must see the swept contact pose. Using the proposed
     * end pose lets a fast igniter heat fuel on the far side of a solid wall. */
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        if (body->body.index1 && body->stop_fraction < 1) {
            body->end.p = b2Lerp(body->start.p, body->end.p, body->stop_fraction);
            b2Body_SetTransform(body->body, body->end.p, body->end.q);
        }
    }
    solve_fluid(physics, delta_seconds);
    profile_phase(physics, &start, &physics->profile.pairs_ms);
    if (physics->profiling) {
        physics->profile.pairs = acount(physics->current);
    }
    CHIBI_TRACE_ZONE_BEGIN(writeback_zone, "Physics writeback");
    for (int i = 0; i < acount(physics->bodies); i++) {
        physics_body *body = &physics->bodies[i];
        if (!body->body.index1) {
            continue;
        }
        *GAME_GameTransform_get(physics->world, body->entity) = (GameTransform){
            body->end.p.x,
            body->end.p.y
        };
        GameOrientation *orientation = GAME_GameOrientation_get(physics->world, body->entity);
        if (orientation && body->settings.angular_velocity != 0) {
            orientation->radians = body->angle +
                                   remainderf(b2Rot_GetAngle(body->end.q) - body->angle, 6.28318530718f);
        }
        GameVelocity *velocity = GAME_GameVelocity_get(physics->world, body->entity);
        if (velocity) {
            b2Vec2 solved = b2Body_GetLinearVelocity(body->body);
            *velocity = (GameVelocity){ solved.x, solved.y };
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
