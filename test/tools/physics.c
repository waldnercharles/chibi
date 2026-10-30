#include "runtime/scenes/physics.h"

#include "execution/status.h"
#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/entities_native.h"
#include "runtime/component_catalog.h"
#include "runtime/events.h"
#include "runtime/material_fluid.h"
#include "runtime/materials.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#ifdef CHIBI_TEST_PREPARATION
#include "authoring/scene_prepare.h"
#include "runtime/scenes/prepared.h"
#endif

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 2D physics through its real seams: ECS components in, Contact Begin and
 * Contact End events out through entities. The host records each event in
 * dispatch order. */
typedef struct
{
    ecs_entity owner;
    uint64_t other;
    int order; /* Among this step's events. */
} contact;

typedef struct
{
    ecs_world *world;
    contact *contacts; /* This step's Contact Begin events. */
    contact *ended;    /* This step's Contact End events. */
    contact *latest;   /* The last Other each owner began touching. */
} recorder;

typedef struct
{
    ecs_world *world;
    entities *entities;
    materials *materials;
    physics *physics;
    recorder *events;
} fixture;

enum
{
    JOBS = 96,
    ITEMS = 4099
};
typedef struct
{
    int *values;
    int seed, workers;
} task_fixture;

static void fill_range(int start, int end, uint32_t worker, void *user)
{
    task_fixture *fixture = user;
    assert(start >= 0 && start < end && end <= ITEMS);
    assert(worker < (uint32_t)fixture->workers);
    for (int i = start; i < end; i++) {
        fixture->values[i] = (i + 1) * fixture->seed;
    }
}

static void test_task_pool()
{
    task_pool *pool = task_pool_acquire();
    task_pool *other = task_pool_acquire();
    assert(pool == other);
    task_system tasks = *task_pool_tasks(pool);
    bool started = task_pool_started(pool);
    int *values = xcalloc((size_t)JOBS * ITEMS, sizeof *values);
    task_fixture fixtures[JOBS];
    void *handles[JOBS];

    task_pool_begin(pool, false);
    task_fixture serial = { values, 3, 1 };
    assert(!tasks.enqueue(fill_range, ITEMS, 64, &serial, tasks.user));
    task_pool_end(pool);
    assert(task_pool_started(pool) == started);
    for (int i = 0; i < ITEMS; i++) {
        assert(values[i] == (i + 1) * 3);
    }
    task_pool_release(other);

    /* Exceed retained task slots, join in reverse order, and verify every
     * range is complete before its argument storage is reused or freed. */
    task_pool_begin(pool, true);
    for (int i = 0; i < JOBS; i++) {
        fixtures[i] = (task_fixture){ values + i * ITEMS, i + 5, tasks.workers };
        handles[i] = tasks.enqueue(fill_range, ITEMS, 64, &fixtures[i], tasks.user);
    }
    for (int i = JOBS - 1; i >= 0; i--) {
        tasks.finish(handles[i], tasks.user);
        for (int j = 0; j < ITEMS; j++) {
            assert(fixtures[i].values[j] == (j + 1) * fixtures[i].seed);
        }
    }
    task_pool_end(pool);
    assert(task_pool_started(pool) == (tasks.workers > 1));

    task_pool_begin(pool, false);
    assert(!tasks.enqueue(fill_range, ITEMS, 64, &serial, tasks.user));
    task_pool_end(pool);
    free(values);
    task_pool *serial_pool = task_pool_acquire_workers(1);
    task_pool *wide = task_pool_acquire_workers(8);
    task_pool *wide_shared = task_pool_acquire_workers(8);
    assert(wide == wide_shared);
    assert(task_pool_tasks(serial_pool)->workers == 1);
    assert(task_pool_tasks(wide)->workers >= 1 && task_pool_tasks(wide)->workers <= 8);
    task_pool_release(wide_shared);
    task_pool_release(serial_pool);
    task_pool_release(wide);
    task_pool_release(pool);
}

static bool large_world;
static int expected_workers;
static int configured_substeps = 4;
enum
{
    PARALLEL_PADDING = 256
};

static int dispatch(void *context, ecs_entity owner, const char *event, execution_arguments arguments)
{
    recorder *f = context;
    contact hit = { owner,
                    arguments.values[0].u64,
                    acount(f->contacts) + acount(f->ended) };
    if (host_event_is(event, HOST_EVENT_CONTACT_END)) {
        assert(arguments.count == 1);
        apush(f->ended, hit);
        return 0;
    }
    assert(host_event_is(event, HOST_EVENT_CONTACT_BEGIN) && arguments.count == 2);
    apush(f->contacts, hit);
    for (int i = 0; i < acount(f->latest); i++) {
        if (ecs_entity_to_u64(f->latest[i].owner) == ecs_entity_to_u64(owner)) {
            f->latest[i] = hit;
            return 0;
        }
    }
    apush(f->latest, hit);
    return 0;
}

static int destroy(void *context, const ecs_entity *entities, int count)
{
    ecs_world *world = ((recorder *)context)->world;
    for (int i = 0; i < count; i++) {
        assert(ecs_world_record_destroy(world, entities[i], (uint64_t)i) == ECS_OK);
    }
    return ecs_world_apply_commands(world);
}

static int attach(void *, ecs_entity, const char *)
{
    return -1; /* Nothing spawns prototypes here. */
}

static fixture create()
{
    fixture made = { .events = xcalloc(1, sizeof(recorder)) }, *f = &made;
    f->world = f->events->world = ecs_world_new();
    const ecs_component_desc *components[] = { &COMPONENT_Transform,
                                               &COMPONENT_Collider,
                                               &COMPONENT_Body,
                                               &COMPONENT_Velocity,
                                               &COMPONENT_Orientation };
    for (int i = 0; i < 5; i++) {
        assert(ecs_world_register_component(f->world, components[i], NULL) == ECS_OK);
    }
    const entity_host host = { .context = f->events,
                               .destroy = destroy,
                               .attach = attach,
                               .dispatch = dispatch };
    f->entities = entities_new(f->world, &host);
    f->materials = materials_new(f->world, f->entities, NULL);
    entities_bind_materials(f->entities, f->materials);
    f->physics = physics_new(f->world, f->entities);
    physics_settings settings = physics_defaults();
    settings.substeps = configured_substeps;
    assert(physics_configure(f->physics, settings) == 0);
    if (large_world) {
        /* Put every semantic case above the parallel collection threshold.
         * These distant, masked bodies produce no additional contacts. */
        for (int i = 0; i < PARALLEL_PADDING; i++) {
            ecs_entity entity = ecs_world_create_entity(f->world);
            assert(Transform_add(f->world, entity, 0) == ECS_OK);
            assert(Collider_add(f->world, entity, 0) == ECS_OK);
            assert(ecs_world_apply_commands(f->world) == ECS_OK);
            *Transform_get(f->world, entity) = (Transform){ 10000 + i * 20, 10000 };
            *Collider_get(f->world, entity) = (Collider){ .radius = 1 };
        }
    }
    return made;
}

static void dispose(fixture *f)
{
    physics_free(f->physics);
    materials_free(f->materials);
    entities_free(f->entities);
    ecs_world_free(f->world);
    afree(f->events->contacts);
    afree(f->events->ended);
    afree(f->events->latest);
    free(f->events);
}

static ecs_entity body(fixture *f, float x, float y, int type, unsigned category, unsigned mask)
{
    ecs_entity entity = ecs_world_create_entity(f->world);
    assert(Transform_add(f->world, entity, 0) == ECS_OK);
    assert(Collider_add(f->world, entity, 0) == ECS_OK);
    assert(Body_add(f->world, entity, 0) == ECS_OK);
    assert(Velocity_add(f->world, entity, 0) == ECS_OK);
    assert(Orientation_add(f->world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    *Transform_get(f->world, entity) = (Transform){ x, y };
    *Collider_get(f->world, entity) = (Collider){ .radius = 4,
                                                  .category = category,
                                                  .mask = mask };
    *Body_get(f->world, entity) = (Body){ .type = type };
    return entity;
}

static ecs_entity sensor(fixture *f, float x, float y)
{
    ecs_entity entity = body(f, x, y, BODY_KINEMATIC, 4, 2 | 8);
    Collider_get(f->world, entity)->sensor = true;
    Collider_get(f->world, entity)->radius = 2;
    Body_get(f->world, entity)->continuous = true;
    return entity;
}

static int step(fixture *f, float delta)
{
    aclear(f->events->contacts);
    aclear(f->events->ended);
    int result = physics_tick(f->physics, delta);
    if (result < 0) {
        fprintf(stderr, "physics: %s\n", physics_error(f->physics));
    }
    assert(result == 0);
    assert(physics_workers(f->physics) == (large_world ? expected_workers : 1));
    do {
        assert(entities_dispatch_events(f->entities) >= 0);
    } while (entities_pending_events(f->entities));
    return acount(f->events->contacts);
}

static uint64_t other(fixture *f, ecs_entity entity)
{
    const recorder *events = f->events;
    for (int i = 0; i < acount(events->latest); i++) {
        if (ecs_entity_to_u64(events->latest[i].owner) == ecs_entity_to_u64(entity)) {
            return events->latest[i].other;
        }
    }
    return 0;
}

/* The Other of owner's Contact End this step, or 0. */
static uint64_t ended(fixture *f, ecs_entity owner)
{
    const contact *events = f->events->ended;
    for (int i = 0; i < acount(events); i++) {
        if (ecs_entity_to_u64(events[i].owner) == ecs_entity_to_u64(owner)) {
            return events[i].other;
        }
    }
    return 0;
}

static ecs_entity contact_owner(fixture *f, int index)
{
    assert(index < acount(f->events->contacts));
    return f->events->contacts[index].owner;
}

static void solid_walls()
{
    fixture f = create();
    ecs_entity wall = body(&f, 50, 0, BODY_STATIC, 8, 1);
    Collider *shape = Collider_get(f.world, wall);
    shape->shape = COLLIDER_BOX;
    shape->width = 2;
    shape->height = 200;
    ecs_entity actor = body(&f, 0, 0, BODY_DYNAMIC, 1, 8);
    Collider_get(f.world, actor)->radius = 8;
    Body_get(f.world, actor)->continuous = true;
    int contacts = 0;
    for (int i = 0; i < 20; i++) {
        *Velocity_get(f.world, actor) = (Velocity){ 600, 40 };
        contacts += step(&f, 1.0f / 60);
    }
    Transform position = *Transform_get(f.world, actor);
    /* Lower substeps deliberately trade overlap recovery accuracy for cost.
     * Retain the original four-step tolerance; all modes must slide, stay on
     * this side of the wall and preserve one Contact Begin per receiver. */
    float max_x = configured_substeps == 4 ? 41.5f : 49;
    assert(position.x > 40 && position.x < max_x && position.y > 12);
    assert(contacts == 2);
    assert(other(&f, actor) == ecs_entity_to_u64(wall));
    assert(Velocity_get(f.world, actor)->x < 1);
    /* Moving away and back must start a new contact, while a held push above
     * must retain the first one across solver separation tolerances. */
    Transform_get(f.world, actor)->x = 0;
    *Velocity_get(f.world, actor) = (Velocity){ 0 };
    assert(step(&f, 0) == 0);
    *Velocity_get(f.world, actor) = (Velocity){ 6000, 0 };
    assert(step(&f, 1.0f / 60) == 2);
    assert(Transform_get(f.world, actor)->x < 41.5f);
    dispose(&f);
}

static void sustained_dynamic_contact()
{
    fixture f = create();
    ecs_entity left = body(&f, -60, 0, BODY_DYNAMIC, 2, 15);
    ecs_entity right = body(&f, 60, 0, BODY_DYNAMIC, 2, 15);
    Collider_get(f.world, left)->radius = 10;
    Collider_get(f.world, right)->radius = 10;
    int contacts = 0;
    for (int i = 0; i < 360; i++) {
        *Velocity_get(f.world, left) = (Velocity){ 55, 0 };
        *Velocity_get(f.world, right) = (Velocity){ -55, 0 };
        contacts += step(&f, 1.0f / 60);
        float separation = Transform_get_const(f.world, right)->x -
                           Transform_get_const(f.world, left)->x;
        assert(separation >= 19);
    }
    assert(contacts == 2);
    assert(other(&f, left) == ecs_entity_to_u64(right));
    assert(fabsf(Velocity_get_const(f.world, left)->x) < 1);
    dispose(&f);
}

static void contact_set_changes()
{
    fixture f = create();
    ecs_entity a = body(&f, 0, 0, BODY_KINEMATIC, 1, 1);
    ecs_entity b = body(&f, 6, 0, BODY_KINEMATIC, 1, 1);
    ecs_entity c = body(&f, 12, 0, BODY_KINEMATIC, 1, 1);
    Collider_get(f.world, a)->sensor = true;
    Collider_get(f.world, b)->sensor = true;
    Collider_get(f.world, c)->sensor = true;
    physics_set_profiling(f.physics, true);
    assert(step(&f, 0) == 4);
    assert(other(&f, a) == ecs_entity_to_u64(b));
    assert(other(&f, c) == ecs_entity_to_u64(b));
    physics_hashes contacts = physics_hash(f.physics);
    assert(step(&f, 0) == 0 && !acount(f.events->ended));
    physics_hashes persistent = physics_hash(f.physics);
    assert(contacts.contact_pairs == persistent.contact_pairs);
    assert(contacts.contact_events != persistent.contact_events);
    assert(step(&f, 1.0f / 60) == 0);
    const physics_profile *profile = physics_last_profile(f.physics);
    assert(isfinite(profile->box2d.step_ms) && profile->box2d.step_ms > 0);
    assert(isfinite(profile->box2d.collide_ms) && profile->box2d.collide_ms >= 0);
    Transform_get(f.world, a)->x = -20;
    assert(step(&f, 0) == 0); /* Remove the first sorted pair; keep the second. */
    assert(acount(f.events->ended) == 2);
    assert(ended(&f, a) == ecs_entity_to_u64(b) && ended(&f, b) == ecs_entity_to_u64(a));
    physics_hashes separated = physics_hash(f.physics);
    assert(contacts.body_state != separated.body_state);
    assert(contacts.contact_pairs != separated.contact_pairs);
    Transform_get(f.world, c)->x = 40;
    assert(step(&f, 0) == 0);
    Transform_get(f.world, a)->x = 0;
    Transform_get(f.world, c)->x = 12;
    assert(step(&f, 0) == 4);
    assert(physics_last_profile(f.physics)->bodies == 3 + (large_world ? PARALLEL_PADDING : 0));
    assert(physics_last_profile(f.physics)->pairs == 2);
    assert(ecs_world_destroy_entity(f.world, b) == ECS_OK);
    ecs_entity replacement = body(&f, 6, 0, BODY_KINEMATIC, 1, 1);
    assert(replacement.slot == b.slot && replacement.generation != b.generation);
    Collider_get(f.world, replacement)->sensor = true;
    assert(step(&f, 0) == 4); /* Reused slots must generate new contact events. */
    /* The destroyed entity hears nothing; its partners hear it end first. */
    assert(acount(f.events->ended) == 2);
    assert(ended(&f, a) == ecs_entity_to_u64(b) && ended(&f, c) == ecs_entity_to_u64(b));
    assert(f.events->ended[1].order < f.events->contacts[0].order);
    assert(other(&f, a) == ecs_entity_to_u64(replacement));
    assert(other(&f, c) == ecs_entity_to_u64(replacement));
    physics_set_profiling(f.physics, false);
    assert(step(&f, 0) == 0);
    assert(!physics_last_profile(f.physics)->bodies);
    assert(physics_last_profile(f.physics)->box2d.step_ms == 0);
    dispose(&f);
}

static void compound_contacts()
{
    /* A noncombat fixture: a notched slab, a round probe and a wide bar. */
    fixture f = create();
    assert(ecs_world_register_component(f.world, &COMPONENT_Destructible, NULL) == ECS_OK);
    ecs_entity slab = body(&f, 0, 0, BODY_STATIC, 8, 1);
    *Collider_get(f.world, slab) = (Collider){ .shape = COLLIDER_BOX,
                                               .width = 32,
                                               .height = 16,
                                               .category = 8,
                                               .mask = 1 };
    Destructible material = { .cell_size = 1, .min_fragment_cells = 1 };
    assert(ecs_world_record_add(f.world, slab, COMPONENT_Destructible.id, &material, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(!materials_prepare(f.materials));
    /* This cut leaves the bottom bridge intact; both shoulders belong to the
     * same entity but occupy different Box2D polygons. */
    materials_damage(f.materials, slab, (fluid_vec){ 0, 7 }, 7, 1, 0, (material_impact){ 0 });
    assert(!materials_prepare(f.materials));
    const material_mask *mask = materials_mask(f.materials, slab);
    assert(mask && material_mask_collision(mask).count >= 3);
    assert(!material_mask_contains(mask, 0, 5));
    assert(material_mask_contains(mask, 10, 5));
    assert(materials_inspect(f.materials).solids == 1);

    ecs_entity probe = body(&f, 0, 5, BODY_KINEMATIC, 1, 8);
    Collider_get(f.world, probe)->radius = 1;
    Collider_get(f.world, probe)->sensor = true;
    ecs_entity bar = body(&f, 0, 5, BODY_KINEMATIC, 1, 8);
    *Collider_get(f.world, bar) = (Collider){ .shape = COLLIDER_BOX,
                                              .width = 28,
                                              .height = 2,
                                              .category = 1,
                                              .mask = 8,
                                              .sensor = true };
    physics_set_profiling(f.physics, true);
    assert(step(&f, 0) == 2); /* Two shoulders generate one entity pair. */
    assert(physics_last_profile(f.physics)->pairs >= 1);
    assert(other(&f, probe) == 0);
    assert(other(&f, bar) == ecs_entity_to_u64(slab));

    Transform_get(f.world, probe)->x = 10;
    assert(step(&f, 1.0f / 60) == 2); /* Earlier polygon misses must not hide this hit. */
    assert(physics_last_profile(f.physics)->pairs >= 2);
    assert(other(&f, probe) == ecs_entity_to_u64(slab));
    assert(step(&f, 1.0f / 60) == 0);
    Transform_get(f.world, probe)->x = 0;
    Transform_get(f.world, bar)->y = 30;
    assert(step(&f, 0) == 0);
    assert(physics_last_profile(f.physics)->pairs == 0);
    Transform_get(f.world, probe)->x = 10;
    Transform_get(f.world, bar)->y = 5;
    assert(step(&f, 0) == 4);
    assert(physics_last_profile(f.physics)->pairs >= 2);
    /* Polygon traversal and worker collection must not change event order. */
    ecs_entity order[] = { slab, probe, slab, bar };
    for (int i = 0; i < 4; i++) {
        assert(ecs_entity_to_u64(contact_owner(&f, i)) == ecs_entity_to_u64(order[i]));
    }
    /* Reuse solved contacts for a solid dynamic bar spanning both shoulders.
     * Repeated drive into the slab must preserve one begin event per entity,
     * even when several polygon manifolds report the same body pair. */
    physics_reset(f.physics);
    Transform_get(f.world, probe)->x = 0;
    Collider_get(f.world, bar)->sensor = false;
    Body_get(f.world, bar)->type = BODY_DYNAMIC;
    int contacts = 0;
    for (int i = 0; i < 30; i++) {
        *Velocity_get(f.world, bar) = (Velocity){ 0, -20 };
        contacts += step(&f, 1.0f / 60);
        assert(physics_last_profile(f.physics)->pairs >= 1);
    }
    assert(contacts == 2);
    assert(other(&f, bar) == ecs_entity_to_u64(slab));
    assert(fabsf(Transform_get_const(f.world, bar)->x) < 1);
    assert(Transform_get_const(f.world, bar)->y < 10);
    dispose(&f);
}

static ecs_entity mask_body(fixture *f, int type, bool sensor_shape)
{
    assert(ecs_world_register_component(f->world, &COMPONENT_Destructible, NULL) == ECS_OK);
    ecs_entity slab = body(f, 0, 0, type, 8, 1 | 4);
    *Collider_get(f->world, slab) = (Collider){ .shape = COLLIDER_BOX,
                                                .width = 32,
                                                .height = 16,
                                                .category = 8,
                                                .mask = 1 | 4,
                                                .sensor = sensor_shape };
    Destructible material = { .cell_size = 1, .min_fragment_cells = 1 };
    assert(ecs_world_record_add(f->world, slab, COMPONENT_Destructible.id, &material, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    return slab;
}

static void retained_mask_contacts()
{
    fixture f = create();
    ecs_entity slab = mask_body(&f, BODY_STATIC, false);
    assert(!materials_prepare(f.materials));
    materials_damage(f.materials, slab, (fluid_vec){ 0, 7 }, 7, 1, 0, (material_impact){ 0 });
    assert(!materials_prepare(f.materials));
    ecs_entity probe = body(&f, 10, 5, BODY_KINEMATIC, 1, 8);
    Collider_get(f.world, probe)->radius = 1;
    Collider_get(f.world, probe)->sensor = true;
    physics_set_profiling(f.physics, true);
    assert(step(&f, 0) == 2);
    material_collision_view before = material_mask_collision(
        materials_mask(f.materials, slab)
    );
    material_rect *old = NULL;
    for (int i = 0; i < before.count; i++) {
        apush(old, before.rects[i]);
    }
    assert(acount(old) >= 3);
    /* Change one shoulder while the other shoulder's contact stays live. */
    materials_damage(f.materials, slab, (fluid_vec){ -10, 7 }, 3, 1, 0, (material_impact){ 0 });
    assert(!materials_prepare(f.materials));
    material_collision_view after = material_mask_collision(
        materials_mask(f.materials, slab)
    );
    int matched = 0, updated = 0;
    for (int i = 0; i < acount(old); i++) {
        for (int j = 0; j < after.count; j++) {
            material_rect a = old[i], b = after.rects[j];
            if (a.x == b.x && a.y == b.y) {
                matched++;
                updated += a.width != b.width || a.height != b.height;
            }
        }
    }
    assert(matched > 0 && matched < acount(old));
    assert(step(&f, 0) == 0);
    const physics_profile *work = physics_last_profile(f.physics);
    assert(!work->body_creations);
    assert(work->shape_creations == (uint64_t)(after.count - matched));
    assert(work->shape_removals == (uint64_t)(acount(old) - matched));
    assert(work->shape_updates == (uint64_t)updated);
    assert(work->mass_updates == 1);
    afree(old);
    Transform_get(f.world, probe)->x = -10;
    Transform_get(f.world, probe)->y = 7;
    assert(step(&f, 0) == 0);
    Transform_get(f.world, probe)->x = 10;
    Transform_get(f.world, probe)->y = 5;
    assert(step(&f, 0) == 2);
    /* A continuous sensor enters the carved opening. Stale shape geometry or
     * old swept-target bounds must not stop it at the slab's original top. */
    ecs_entity shot = sensor(&f, 0, 20);
    Collider_get(f.world, shot)->radius = .25f;
    *Velocity_get(f.world, shot) = (Velocity){ 0, -960 };
    assert(step(&f, 1.0f / 60) == 0);
    assert(fabsf(Transform_get_const(f.world, shot)->y - 4) < .01f);
    dispose(&f);
}

static void empty_mask_shapes()
{
    for (int initially_empty = 0; initially_empty < 2; initially_empty++) {
        fixture f = create();
        ecs_entity slab = mask_body(&f, BODY_STATIC, false);
        assert(!materials_prepare(f.materials));
        /* Seed the adapter edge case directly through the borrowed mask: normal
         * gameplay retires empty sources, but an empty mask must never regain
         * analytic geometry. */
        material_mask *mask = (material_mask *)materials_mask(f.materials, slab);
        if (initially_empty) {
            assert(material_mask_carve(mask, 0, 0, 100) == 512);
        }
        ecs_entity probe = body(&f, 0, 0, BODY_KINEMATIC, 1, 8);
        Collider_get(f.world, probe)->sensor = true;
        physics_set_profiling(f.physics, true);
        assert(step(&f, 0) == (initially_empty ? 0 : 2));
        if (!initially_empty) {
            assert(material_mask_carve(mask, 0, 0, 100) == 512);
            assert(step(&f, 0) == 0);
            const physics_profile *work = physics_last_profile(f.physics);
            assert(!work->body_creations && !work->shape_creations && !work->shape_updates);
            assert(work->shape_removals == 1 && work->mass_updates == 1);
        }
        assert(!mask->count && !material_mask_collision(mask).count);
        ecs_entity shot = sensor(&f, -100, 0);
        *Velocity_get(f.world, shot) = (Velocity){ 12000, 0 };
        assert(step(&f, 1.0f / 60) == 0);
        assert(fabsf(Transform_get_const(f.world, shot)->x - 100) < .01f);
        assert(!physics_last_profile(f.physics)->pairs);
        dispose(&f);
    }
}

static void retained_mask_motion()
{
    for (int type = BODY_STATIC; type <= BODY_KINEMATIC; type++) {
        fixture f = create();
        ecs_entity slab = mask_body(&f, type, type == BODY_KINEMATIC);
        Body *settings = Body_get(f.world, slab);
        settings->continuous = true;
        settings->angular_velocity = type == BODY_KINEMATIC ? 1.2f : 0;
        *Velocity_get(f.world, slab) = (Velocity){ 10, -4 };
        physics_set_profiling(f.physics, true);
        assert(step(&f, 0) == 0);
        const physics_profile *work = physics_last_profile(f.physics);
        assert(work->body_creations == (uint64_t)(1 + (large_world ? PARALLEL_PADDING : 0)));
        /* Symmetric edge removal keeps the actual polygon centroid at zero,
         * permitting an in-place width change without stale CCD centroid data. */
        materials_damage(f.materials, slab, (fluid_vec){ -100, 0 }, 85, 1, 0, (material_impact){ 0 });
        materials_damage(f.materials, slab, (fluid_vec){ 100, 0 }, 85, 1, 0, (material_impact){ 0 });
        assert(!materials_prepare(f.materials));
        material_collision_view collision = material_mask_collision(
            materials_mask(f.materials, slab)
        );
        assert(collision.count == 1);
        assert(collision.rects[0].x == 0 && collision.rects[0].y == 0);
        assert(collision.rects[0].width == 30 && collision.rects[0].height == 16);
        Orientation_get(f.world, slab)->radians = type == BODY_KINEMATIC ? 0 : .3f;
        assert(step(&f, 0) == 0);
        work = physics_last_profile(f.physics);
        assert(!work->body_creations && !work->shape_creations && !work->shape_removals);
        assert(work->shape_updates == 1 && work->mass_updates == 1);
        assert(Velocity_get_const(f.world, slab)->x == (type == BODY_STATIC ? 0 : 10));
        assert(Velocity_get_const(f.world, slab)->y == (type == BODY_STATIC ? 0 : -4));
        assert(step(&f, 0) == 0);
        work = physics_last_profile(f.physics);
        assert(
            !work->body_creations && !work->shape_creations &&
            !work->shape_updates && !work->shape_removals && !work->mass_updates
        );
        assert(step(&f, 1.0f / 60) == 0);
        if (type != BODY_STATIC) {
            assert(Transform_get_const(f.world, slab)->x > .1f);
            assert(Transform_get_const(f.world, slab)->y < -.04f);
        }
        if (type == BODY_KINEMATIC) {
            assert(fabsf(Orientation_get_const(f.world, slab)->radians - .02f) < .001f);
        }
        /* Authoring changes retain their old rebuild contract, unlike cuts. */
        Collider_get(f.world, slab)->friction = .5f;
        assert(step(&f, 0) == 0);
        work = physics_last_profile(f.physics);
        assert(
            work->body_creations == 1 && work->shape_creations == 1 &&
            work->shape_removals == 1
        );
        assert(work->mass_updates == 1);
        unsigned slot = slab.slot;
        assert(ecs_world_record_destroy(f.world, slab, 0) == ECS_OK);
        assert(ecs_world_apply_commands(f.world) == ECS_OK);
        ecs_entity replacement = body(&f, 0, 0, BODY_STATIC, 8, 1);
        assert(
            replacement.slot == slot &&
            ecs_entity_to_u64(replacement) != ecs_entity_to_u64(slab)
        );
        assert(step(&f, 0) == 0);
        work = physics_last_profile(f.physics);
        assert(
            work->body_creations == 1 && work->shape_creations == 1 &&
            work->shape_removals == 1
        );
        physics_reset(f.physics);
        assert(step(&f, 0) == 0);
        assert(
            physics_last_profile(f.physics)->body_creations ==
            (uint64_t)(1 + (large_world ? PARALLEL_PADDING : 0))
        );
        dispose(&f);
    }
}

static physics_body_state inspect_body(fixture *f, ecs_entity entity)
{
    physics_body_state state;
    assert(physics_inspect_body(f->physics, entity, &state));
    return state;
}

static void reset_mask_incarnation()
{
    fixture f = create();
    ecs_entity slab = mask_body(&f, BODY_STATIC, false);
    materials_damage(f.materials, slab, (fluid_vec){ -100, 0 }, 85, 1, 0, (material_impact){ 0 });
    assert(step(&f, 0) == 0);
    unsigned revision = materials_mask(f.materials, slab)->revision;
    uint64_t generation = materials_mask_generation(f.materials, slab);
    materials_reset(f.materials);
    materials_damage(f.materials, slab, (fluid_vec){ 100, 0 }, 85, 1, 0, (material_impact){ 0 });
    assert(!materials_prepare(f.materials));
    assert(materials_mask(f.materials, slab)->revision == revision);
    assert(materials_mask_generation(f.materials, slab) != generation);
    physics_set_profiling(f.physics, true);
    assert(step(&f, 0) == 0);
    const physics_profile *work = physics_last_profile(f.physics);
    assert(!work->body_creations && work->shape_creations == 1 && work->shape_removals == 1);
    assert(work->mass_updates == 1);
    ecs_entity probe = sensor(&f, 15.6f, 0);
    Collider_get(f.world, probe)->radius = .1f;
    assert(step(&f, 0) == 0);
    Transform_get(f.world, probe)->x = -15.6f;
    assert(step(&f, 0) == 2);
    dispose(&f);
}

typedef struct
{
    Transform target, projectile;
    Velocity target_velocity, projectile_velocity;
    physics_body_state mass;
} impact_result;

static impact_result mask_impact(bool cut, bool fresh)
{
    fixture f = create();
    ecs_entity slab = mask_body(&f, BODY_DYNAMIC, false);
    Collider_get(f.world, slab)->restitution = 1;
    ecs_entity projectile = body(&f, 60, 0, BODY_DYNAMIC, 1, 8);
    Collider_get(f.world, projectile)->restitution = 1;
    assert(step(&f, 0) == 0);
    physics_body_state original = inspect_body(&f, slab);
    if (cut) {
        materials_damage(f.materials, slab, (fluid_vec){ -100, 0 }, 100, 1, 0, (material_impact){ 0 });
        assert(!materials_prepare(f.materials));
        if (fresh) {
            physics_reset(f.physics);
        }
        assert(step(&f, 0) == 0);
    }
    physics_body_state mass = inspect_body(&f, slab);
    assert(mass.inertia == 0); /* Dynamic bodies deliberately have fixed rotation. */
    if (cut) {
        assert(mass.mass < original.mass * .6f);
        assert(mass.local_center.x > 7 && fabsf(mass.local_center.y) < .001f);
    }
    *Velocity_get(f.world, projectile) = (Velocity){ -240, 0 };
    for (int i = 0; i < 24; i++) {
        step(&f, 1.0f / 60);
    }
    impact_result result = { *Transform_get(f.world, slab),
                             *Transform_get(f.world, projectile),
                             *Velocity_get(f.world, slab),
                             *Velocity_get(f.world, projectile),
                             mass };
    assert(result.target_velocity.x < -10);
    dispose(&f);
    return result;
}

static void retained_mask_mass()
{
    impact_result retained = mask_impact(true, false);
    impact_result fresh = mask_impact(true, true);
    impact_result uncut = mask_impact(false, false);
    assert(fabsf(retained.mass.mass - fresh.mass.mass) < .000001f);
    assert(fabsf(retained.mass.local_center.x - fresh.mass.local_center.x) < .00001f);
    assert(fabsf(retained.target.x - fresh.target.x) < .001f);
    assert(fabsf(retained.projectile.x - fresh.projectile.x) < .001f);
    assert(fabsf(retained.target_velocity.x - fresh.target_velocity.x) < .001f);
    assert(fabsf(retained.projectile_velocity.x - fresh.projectile_velocity.x) < .001f);
    assert(retained.target_velocity.x < uncut.target_velocity.x * 1.2f);
}

static void retained_mask_solid_ccd()
{
    fixture f = create();
    ecs_entity slab = mask_body(&f, BODY_STATIC, false);
    assert(step(&f, 0) == 0);
    materials_damage(f.materials, slab, (fluid_vec){ -100, 0 }, 100, 1, 0, (material_impact){ 0 });
    assert(step(&f, 0) == 0);
    ecs_entity projectile = body(&f, 60, 0, BODY_DYNAMIC, 1, 8);
    Collider_get(f.world, projectile)->radius = 1;
    Body_get(f.world, projectile)->continuous = true;
    *Velocity_get(f.world, projectile) = (Velocity){ -6000, 0 };
    assert(step(&f, 1.0f / 60) == 2);
    float x = Transform_get(f.world, projectile)->x;
    assert(x > 16 && x < 20);
    dispose(&f);
}

static void retained_mask_wakes_support()
{
    fixture f = create();
    ecs_entity slab = mask_body(&f, BODY_STATIC, false);
    ecs_entity actor = body(&f, 0, -10, BODY_DYNAMIC, 1, 8);
    Collider_get(f.world, actor)->radius = 2;
    for (int i = 0; i < 120; i++) {
        step(&f, 1.0f / 60);
    }
    assert(!inspect_body(&f, actor).awake);
    materials_damage(f.materials, slab, (fluid_vec){ 0, 0 }, 12, 1, 0, (material_impact){ 0 });
    step(&f, 0);
    assert(inspect_body(&f, actor).awake);
    /* Gravity is authored motion in this runtime. Check wake before supplying
     * downward velocity so the velocity setter cannot hide a failed wake. */
    *Velocity_get(f.world, actor) = (Velocity){ 0, 120 };
    for (int i = 0; i < 12; i++) {
        step(&f, 1.0f / 60);
    }
    assert(Transform_get(f.world, actor)->y > 10);
    dispose(&f);
}

static void separated_solved_contacts()
{
    fixture f = create();
    ecs_entity actor = body(&f, 0, 0, BODY_DYNAMIC, 1, 2);
    ecs_entity target = body(&f, 8.1f, 0, BODY_STATIC, 2, 1);
    physics_set_profiling(f.physics, true);
    assert(step(&f, 0) == 0); /* Zero delta requires actual overlap. */
    assert(physics_last_profile(f.physics)->pairs == 0);
    assert(step(&f, 1.0f / 60) == 2);
    assert(physics_last_profile(f.physics)->pairs >= 1);
    float separation = Transform_get_const(f.world, target)->x -
                       Transform_get_const(f.world, actor)->x;
    assert(separation > 8); /* The event comes from a solved positive-gap manifold. */
    assert(other(&f, actor) == ecs_entity_to_u64(target));
    for (int i = 0; i < 10; i++) {
        assert(step(&f, 1.0f / 60) == 0);
        assert(physics_last_profile(f.physics)->pairs >= 1);
    }
    assert(step(&f, 0) == 0); /* Do not reuse the solver manifold on a resync. */
    assert(physics_last_profile(f.physics)->pairs == 0);
    Transform_get(f.world, actor)->x = .2f;
    assert(step(&f, 0) == 2);
    assert(physics_last_profile(f.physics)->pairs >= 1);
    dispose(&f);
}

/* Nearest positions are world placements, which a parent's Orientation turns,
 * so a behavior that writes Orientation cannot run beside the query. */
static void nearest_reads_world_placement()
{
    const ecs_component_id components[] = { COMPONENT_Transform.id,
                                            COMPONENT_Orientation.id,
                                            COMPONENT_Collider.id };
    for (int i = 0; i < 3; i++) {
        bool read = false;
        for (const uint64_t *const *ref = ENTITY_NEAREST_ACCESS.read_refs; *ref; ref++) {
            read |= **ref == components[i];
        }
        assert(read);
    }
}

static void nearest_and_filters()
{
    fixture f = create();
    ecs_entity owner = body(&f, 0, 0, BODY_STATIC, 2, 4);
    ecs_entity far = body(&f, 80, 0, BODY_STATIC, 2, 4);
    body(&f, 20, 0, BODY_STATIC, 2, 0);
    ecs_entity near = body(&f, 40, 0, BODY_STATIC, 2, 4);
    ecs_entity shot = sensor(&f, 0, 0);
    Collider_get(f.world, shot)->ignore_entity = ecs_entity_to_u64(owner);
    *Velocity_get(f.world, shot) = (Velocity){ 6000, 0 };
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(near));
    float impact = Transform_get(f.world, shot)->x;
    assert(impact > 33 && impact < 35);
    assert(other(&f, far) == 0);
    assert(step(&f, 1.0f / 60) == 0);
    assert(ecs_world_destroy_entity(f.world, near) == ECS_OK);
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(far));
    assert(ecs_world_destroy_entity(f.world, owner) == ECS_OK);
    ecs_entity replacement = body(&f, 0, 0, BODY_STATIC, 2, 4);
    assert(ecs_entity_to_u64(replacement) != ecs_entity_to_u64(owner));
    Transform_get(f.world, shot)->x = 0;
    assert(step(&f, 0) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(replacement));
    dispose(&f);
}

static void moving_target_and_box()
{
    fixture f = create();
    ecs_entity target = body(&f, 50, -50, BODY_KINEMATIC, 2, 4);
    *Velocity_get(f.world, target) = (Velocity){ 0, 6000 };
    ecs_entity shot = sensor(&f, 0, 0);
    *Velocity_get(f.world, shot) = (Velocity){ 6000, 0 };
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(target));
    float impact = Transform_get(f.world, shot)->x;
    assert(impact > 44 && impact < 48);
    assert(Transform_get(f.world, target)->y > 49);
    /* The same continuous contract applies to box sensors and rotated boxes. */
    physics_reset(f.physics);
    *Transform_get(f.world, target) = (Transform){ 50, 0 };
    *Velocity_get(f.world, target) = (Velocity){ 0 };
    Collider *collider = Collider_get(f.world, target);
    collider->shape = COLLIDER_BOX;
    collider->width = 4;
    collider->height = 40;
    Orientation_get(f.world, target)->radians = .5f;
    *Transform_get(f.world, shot) = (Transform){ 0 };
    collider = Collider_get(f.world, shot);
    collider->shape = COLLIDER_BOX;
    collider->width = 4;
    collider->height = 3;
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(target));
    assert(Transform_get(f.world, shot)->x < 50);
    dispose(&f);
}

static void sweep_tree_lifetime()
{
    fixture f = create();
    ecs_entity target = body(&f, 50, 0, BODY_STATIC, 2, 4);
    ecs_entity shot = sensor(&f, 0, 0);
    Velocity_get(f.world, shot)->x = 6000;
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(target));

    /* Ordinary queries remain current while the swept index is dormant. */
    Body_get(f.world, shot)->continuous = false;
    Transform_get(f.world, shot)->x = 0;
    Transform_get(f.world, target)->x = 80;
    Velocity_get(f.world, shot)->x = 0;
    assert(step(&f, 0) == 0);
    Transform_get(f.world, shot)->x = 80;
    assert(step(&f, 0) == 2);
    Transform_get(f.world, shot)->x = 0;
    assert(step(&f, 0) == 0);
    assert(ecs_world_destroy_entity(f.world, target) == ECS_OK);
    ecs_entity replacement = body(&f, 60, 0, BODY_STATIC, 2, 4);
    assert(replacement.slot == target.slot && replacement.generation != target.generation);
    assert(step(&f, 0) == 0);

    Body_get(f.world, shot)->continuous = true;
    Velocity_get(f.world, shot)->x = 6000;
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(replacement));
    float impact = Transform_get(f.world, shot)->x;
    assert(impact > 53 && impact < 55);
    dispose(&f);
}

static void repeated_moving_sweeps()
{
    fixture f = create();
    ecs_entity targets[16];
    for (int i = 0; i < 16; i++) {
        targets[i] = body(&f, 200 + i * 20, 200, BODY_KINEMATIC, 2, 4);
    }
    ecs_entity shot = sensor(&f, 0, 0);
    for (int round = 0; round < 16; round++) {
        /* Clear contact history and move the whole target population outside
         * its previous bounds, then cross two targets through the shot. */
        *Transform_get(f.world, shot) = (Transform){ 0 };
        *Velocity_get(f.world, shot) = (Velocity){ 0 };
        for (int i = 0; i < 16; i++) {
            *Transform_get(f.world, targets[i]) = (Transform){ 200 + i * 20,
                                                               200 + round * 100 };
            *Velocity_get(f.world, targets[i]) = (Velocity){ 0 };
        }
        assert(step(&f, 0) == 0);
        int a = round, b = (round + 7) % 16;
        *Transform_get(f.world, targets[a]) = (Transform){ 50, -50 };
        *Transform_get(f.world, targets[b]) = (Transform){ 80, -80 };
        Velocity_get(f.world, targets[a])->y = 6000;
        Velocity_get(f.world, targets[b])->y = 6000;
        Velocity_get(f.world, shot)->x = 6000;
        assert(step(&f, 1.0f / 60) == 2);
        assert(other(&f, shot) == ecs_entity_to_u64(targets[a]));
        float impact = Transform_get(f.world, shot)->x;
        assert(impact > 44 && impact < 48);
    }
    dispose(&f);
}

static void lifecycle_and_validation()
{
    fixture f = create();
    ecs_entity trigger = body(&f, 0, 0, BODY_STATIC, 2, 1);
    Collider_get(f.world, trigger)->sensor = true;
    ecs_entity actor = body(&f, 3, 0, BODY_DYNAMIC, 1, 2);
    assert(step(&f, 0) == 2);
    assert(step(&f, 0) == 0);
    Collider_get(f.world, trigger)->ignore_entity = ecs_entity_to_u64(actor);
    assert(step(&f, 0) == 0);
    Collider_get(f.world, trigger)->ignore_entity = 0;
    assert(step(&f, 0) == 2);
    assert(Collider_remove(f.world, trigger, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(step(&f, 0) == 0);
    assert(Collider_add(f.world, trigger, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    *Collider_get(f.world, trigger) = (Collider){ .radius = 4,
                                                  .category = 2,
                                                  .mask = 1,
                                                  .sensor = true };
    assert(step(&f, 0) == 2);
    Velocity_get(f.world, actor)->x = NAN;
    assert(physics_tick(f.physics, 1.0f / 60) < 0);
    assert(strstr(physics_error(f.physics), "entity"));
    Velocity_get(f.world, actor)->x = 0;
    assert(step(&f, 0) == 0);
    physics_reset(f.physics);
    assert(step(&f, 0) == 2);
    /* Both ECS commands and host destruction requests retire a collider before
     * their respective flush boundary, without invalidating live handles. */
    assert(ecs_world_record_destroy(f.world, trigger, 0) == ECS_OK);
    assert(ecs_entity_is_alive(f.world, trigger));
    assert(entities_is_retiring(f.entities, trigger));
    assert(step(&f, 0) == 0);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    ecs_entity replacement = body(&f, 0, 0, BODY_STATIC, 2, 1);
    assert(!entities_is_retiring(f.entities, replacement));
    assert(step(&f, 0) == 2);
    entities_request_destroy(f.entities, replacement);
    assert(step(&f, 0) == 0);
    dispose(&f);
}

static void interpolation()
{
    fixture f = create();
    ecs_entity actor = body(&f, 0, 0, BODY_DYNAMIC, 1, 0);
    *Velocity_get(f.world, actor) = (Velocity){ 600, 0 };
    assert(step(&f, 1.0f / 60) == 0);
    CF_V2 offset;
    assert(physics_interpolation_offset(f.physics, actor, .5f, &offset));
    assert(fabsf(offset.x + 5) < .001f && offset.y == 0);
    assert(fabsf(Transform_get(f.world, actor)->x - 10) < .001f);
    *Transform_get(f.world, actor) = (Transform){ 500, 10 };
    assert(physics_interpolation_offset(f.physics, actor, .5f, &offset));
    assert(offset.x == 0 && offset.y == 0);
    assert(step(&f, 0) == 0);
    assert(physics_interpolation_offset(f.physics, actor, 0, &offset));
    assert(offset.x == 0 && offset.y == 0);
    entities_request_destroy(f.entities, actor);
    assert(!physics_interpolation_offset(f.physics, actor, .5f, &offset));
    assert(entities_flush(f.entities) == 0);
    ecs_entity replacement = body(&f, 0, 0, BODY_DYNAMIC, 1, 0);
    assert(!physics_interpolation_offset(f.physics, actor, .5f, &offset));
    assert(!physics_interpolation_offset(f.physics, replacement, .5f, &offset));
    dispose(&f);
}

static void followers()
{
    fixture f = create();
    ecs_entity parent = body(&f, 100, 0, BODY_KINEMATIC, 1, 0);
    Orientation_get(f.world, parent)->radians = 1.57079633f;
    ecs_entity child = sensor(&f, 10, 0);
    assert(ecs_world_record_add_pair(f.world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    *Velocity_get(f.world, child) = (Velocity){ 600, 0 };
    ecs_entity there = body(&f, 100, 10, BODY_KINEMATIC, 2, 4);
    ecs_entity origin = body(&f, 10, 0, BODY_KINEMATIC, 2, 4);
    /* The child collides where its parent places it, ignores its own Velocity
     * and never writes its pose back. */
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, child) == ecs_entity_to_u64(there));
    assert(other(&f, there) == ecs_entity_to_u64(child));
    assert(!other(&f, origin));
    const Transform *local = Transform_get_const(f.world, child);
    assert(local->x == 10 && local->y == 0);
    CF_V2 offset;
    assert(!physics_interpolation_offset(f.physics, child, .5f, &offset));
    /* A parent without a Transform only groups; its children stay roots. */
    ecs_entity group = ecs_world_create_entity(f.world);
    ecs_entity grouped = body(&f, 0, 40, BODY_DYNAMIC, 1, 0);
    assert(ecs_world_record_add_pair(f.world, grouped, ECS_CHILD_OF, group, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    *Velocity_get(f.world, grouped) = (Velocity){ 60, 0 };
    step(&f, 1.0f / 60);
    assert(fabsf(Transform_get_const(f.world, grouped)->x - 1) < .001f);
    Body_get(f.world, child)->type = BODY_DYNAMIC;
    assert(physics_tick(f.physics, 1.0f / 60) < 0);
    assert(strstr(physics_error(f.physics), "root entity"));
    dispose(&f);
}

/* A child of a moving body moves with it within the same step, so it touches
 * what lies where its parent carries it, not only where it started. */
static void followers_move_with_their_body()
{
    fixture f = create();
    ecs_entity parent = body(&f, 0, 200, BODY_DYNAMIC, 1, 0);
    *Velocity_get(f.world, parent) = (Velocity){ 1200, 0 };
    ecs_entity child = sensor(&f, 10, 0);
    assert(ecs_world_record_add_pair(f.world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    /* The child starts over 8..12 and ends over 28..32; the target covers
     * 30..38. */
    ecs_entity target = body(&f, 34, 200, BODY_KINEMATIC, 2, 4);
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, child) == ecs_entity_to_u64(target));
    assert(fabsf(Transform_get_const(f.world, parent)->x - 20) < .001f);
    dispose(&f);
}

/* A body and the colliders it carries move as one, so they never touch. */
static void followers_ignore_their_anchor()
{
    fixture f = create();
    ecs_entity ship = body(&f, 0, 0, BODY_DYNAMIC, 1, 4);
    ecs_entity hull = body(&f, 2, 0, BODY_KINEMATIC, 4, 1 | 4);
    ecs_entity mast = sensor(&f, -2, 0);
    Collider_get(f.world, mast)->mask |= 1 | 4;
    assert(ecs_world_record_add_pair(f.world, hull, ECS_CHILD_OF, ship, 0) == ECS_OK);
    assert(ecs_world_record_add_pair(f.world, mast, ECS_CHILD_OF, ship, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    for (int i = 0; i < 4; i++) {
        assert(step(&f, 1.0f / 60) == 0);
    }
    const Transform *at = Transform_get_const(f.world, ship);
    assert(at->x == 0 && at->y == 0);
    ecs_entity *found = NULL;
    assert(physics_contact_entities(f.world, f.entities, ship, 4, &found) == 0);
    assert(acount(found) == 0);
    assert(physics_contact_entities(f.world, f.entities, hull, 1 | 4, &found) == 0);
    assert(acount(found) == 0);
    afree(found);
    dispose(&f);
}

/* A carried collider is part of its root's body: it adds its mass (a sensor
 * adds none), and what it hits stops the body while contacts name it. */
static void carried_colliders_join_their_body()
{
    fixture f = create();
    ecs_entity ship = body(&f, 0, 0, BODY_DYNAMIC, 1, 2);
    assert(step(&f, 0) == 0);
    float alone = inspect_body(&f, ship).mass;
    ecs_entity ram = body(&f, 10, 0, BODY_KINEMATIC, 1, 2);
    ecs_entity mast = sensor(&f, -10, 0);
    assert(ecs_world_record_add_pair(f.world, ram, ECS_CHILD_OF, ship, 0) == ECS_OK);
    assert(ecs_world_record_add_pair(f.world, mast, ECS_CHILD_OF, ship, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    ecs_entity wall = body(&f, 40, 0, BODY_STATIC, 2, 1);
    *Velocity_get(f.world, ship) = (Velocity){ 600, 0 };
    assert(step(&f, 0) == 0);
    physics_body_state state = inspect_body(&f, ship);
    assert(state.shape_count == 3 && fabsf(state.mass - 2 * alone) < alone * 1e-4f);
    assert(Body_get(f.world, ship)->mass == state.mass);
    for (int i = 0; i < 30; i++) {
        step(&f, 1.0f / 60);
    }
    /* The ram, 10 ahead of the ship, stops against the wall. */
    float x = Transform_get_const(f.world, ship)->x;
    assert(x > 21 && x < 22.5f);
    assert(other(&f, ram) == ecs_entity_to_u64(wall));
    assert(other(&f, wall) == ecs_entity_to_u64(ram));
    assert(!other(&f, ship));
    /* A rebuilt body takes its carried colliders back. */
    Collider_get(f.world, ship)->radius = 3;
    assert(step(&f, 0) == 0);
    assert(inspect_body(&f, ship).shape_count == 3);
    assert(inspect_body(&f, ram).shape_count == 3);
    /* A removed one takes its mass with it. */
    assert(Collider_remove(f.world, ram, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(step(&f, 0) == 0);
    state = inspect_body(&f, ship);
    assert(state.shape_count == 2 && fabsf(state.mass - 9.0f / 16 * alone) < alone * 1e-4f);
    dispose(&f);
}

/* A carried collider whose mask has no rectangles left still leaves with the
 * body that carries it, so a rebuilt body takes it back. */
static void empty_carried_colliders_follow_their_body()
{
    fixture f = create();
    ecs_entity ship = body(&f, 0, 40, BODY_KINEMATIC, 1, 0);
    ecs_entity slab = mask_body(&f, BODY_STATIC, false);
    assert(!materials_prepare(f.materials));
    material_mask *mask = (material_mask *)materials_mask(f.materials, slab);
    assert(material_mask_carve(mask, 0, 0, 100) == 512);
    assert(ecs_world_record_add_pair(f.world, slab, ECS_CHILD_OF, ship, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(step(&f, 0) == 0);
    assert(!material_mask_collision(materials_mask(f.materials, slab)).count);
    assert(inspect_body(&f, slab).shape_count == 1);
    Collider_get(f.world, ship)->radius = 3;
    assert(step(&f, 0) == 0);
    assert(inspect_body(&f, slab).shape_count == 1);
    assert(Collider_remove(f.world, ship, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(step(&f, 0) == 0);
    assert(inspect_body(&f, slab).shape_count == 0);
    dispose(&f);
}

/* Material fluid over a 64 by 64 camera at the origin, with gas flow on a
 * grid of `gas_cell_size` cells when it is not 0, and liquid moved by
 * `solver`. */
static void enable_fluid(fixture *f, float gas_cell_size, MaterialSolver solver)
{
    assert(ecs_world_register_component(f->world, &COMPONENT_Camera, NULL) == ECS_OK);
    assert(ecs_world_register_component(f->world, &COMPONENT_MaterialFluidWorld, NULL) == ECS_OK);
    ecs_entity config = ecs_world_create_entity(f->world);
    assert(Transform_add(f->world, config, 0) == ECS_OK);
    Camera camera = { .width = 64, .height = 64 };
    assert(ecs_world_record_add(f->world, config, COMPONENT_Camera.id, &camera, 0) == ECS_OK);
    MaterialFluidWorld fluid = { .chunk_size = 64,
                                 .buffer = 32,
                                 .kernel = 10,
                                 .particle_limit = 16,
                                 .gas_cell_size = gas_cell_size,
                                 .solver = solver,
                                 .mpm_cell_size = 2,
                                 .mpm_max_substeps = 8 };
    assert(
        ecs_world_record_add(f->world, config, COMPONENT_MaterialFluidWorld.id, &fluid, 0) == ECS_OK
    );
}

/* A 12 by 12 hull carried 20 above `ship`, so it spans -6..6 at the origin. */
static ecs_entity carried_hull(fixture *f, ecs_entity ship)
{
    ecs_entity hull = body(f, 0, 20, BODY_KINEMATIC, 1, 0);
    *Collider_get(f->world, hull) = (Collider){ .shape = COLLIDER_BOX,
                                                .width = 12,
                                                .height = 12 };
    assert(ecs_world_record_add_pair(f->world, hull, ECS_CHILD_OF, ship, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    return hull;
}

static void emit_gas(fixture *f, float x, float y, float speed)
{
    assert(materials_emit(
        f->materials,
        (fluid_particle){ .position = { x, y },
                          .velocity = { speed, 0 },
                          .mass = .01f,
                          .radius = 1,
                          .lifetime = .025f,
                          .kind = FLUID_GAS }
    ));
}

/* Fluid striking a carried collider pushes the body that carries it. */
static void carried_colliders_take_fluid_impulses()
{
    fixture f = create();
    enable_fluid(&f, 0, MATERIAL_SOLVER_DDR);
    ecs_entity ship = body(&f, 0, -20, BODY_DYNAMIC, 1, 0);
    carried_hull(&f, ship);
    assert(step(&f, 0) == 0);
    emit_gas(&f, -8, 4, 120);
    step(&f, 1.0f / 60);
    step(&f, 1.0f / 60);
    assert(Velocity_get_const(f.world, ship)->x > 0);
    dispose(&f);
}

/* Fluid striking two colliders on one body pushes it once: the second
 * contact sees what the first did. Heavy gas, or liquid moved by MPM, carries
 * the ship at the fluid's own speed, never twice it. */
static void carried_colliders_share_fluid_response()
{
    for (int mpm = 0; mpm < 2; mpm++) {
        fixture f = create();
        enable_fluid(&f, 0, mpm ? MATERIAL_SOLVER_MPM : MATERIAL_SOLVER_DDR);
        ecs_entity ship = body(&f, 0, -20, BODY_DYNAMIC, 1, 0);
        for (int i = 0; i < 2; i++) {
            Transform_get(f.world, carried_hull(&f, ship))->y = 20 + 20 * i;
        }
        assert(step(&f, 0) == 0);
        for (int i = 0; i < 2; i++) {
            assert(materials_emit(
                f.materials,
                (fluid_particle){ .position = { -8, 4 + 20 * i },
                                  .velocity = { 120, 0 },
                                  .mass = 1000,
                                  .radius = 1,
                                  .lifetime = .025f,
                                  .kind = mpm ? FLUID_LIQUID : FLUID_GAS,
                                  .constitutive = { 1, 20, 0 } }
            ));
        }
        step(&f, 1.0f / 60);
        step(&f, 1.0f / 60);
        float speed = Velocity_get_const(f.world, ship)->x;
        assert(speed > 110 && speed < 121);
        dispose(&f);
    }
}

/* The impulse a blast of burning fuel at the origin gives two bodies 30 and 34
 * to its right, parts of one body when `parts`. */
/* The push a blast gives two bodies, or one body of two parts, the first at
 * first_x and the second at 34. */
static float blast_impulse(float first_x, bool parts)
{
    fixture f = create();
    const ecs_component_desc *components[] = { &COMPONENT_MaterialFluidWorld,
                                               &COMPONENT_Combustion,
                                               &COMPONENT_FluidVolume,
                                               &COMPONENT_Fuel };
    for (int i = 0; i < 4; i++) {
        assert(ecs_world_register_component(f.world, components[i], NULL) == ECS_OK);
    }
    ecs_entity config = ecs_world_create_entity(f.world);
    MaterialFluidWorld fluid = { .chunk_size = 64,
                                 .buffer = 128,
                                 .kernel = 3,
                                 .particle_limit = 64 };
    Combustion combustion = { .ignition = .9f,
                              .burn_rate = .1f,
                              .heat_release = 2,
                              .smoke_lifetime = 3,
                              .blast_radius = 80,
                              .blast_speed = 90 };
    assert(
        ecs_world_record_add(f.world, config, COMPONENT_MaterialFluidWorld.id, &fluid, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(f.world, config, COMPONENT_Combustion.id, &combustion, 0) == ECS_OK
    );
    ecs_entity fuel = ecs_world_create_entity(f.world);
    FluidVolume volume = { .kind = FLUID_GAS,
                           .width = 6,
                           .height = 6,
                           .spacing = 1,
                           .mass = .001f };
    assert(Transform_add(f.world, fuel, 0) == ECS_OK);
    assert(ecs_world_record_add(f.world, fuel, COMPONENT_FluidVolume.id, &volume, 0) == ECS_OK);
    assert(ecs_world_record_add(f.world, fuel, COMPONENT_Fuel.id, &(Fuel){ 1 }, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(!materials_prepare(f.materials));
    fluid_body bodies[] = {
        { .velocity = { 2, 0 }, .heat = 2 },
        { .position = { first_x, 0 }, .inverse_mass = 1 },
        { .position = { 34, 0 }, .inverse_mass = 1, .part_of = parts ? 2 : 0 }
    };
    materials_tick(f.materials, 1.0f / 60, bodies, 3, NULL, 0);
    int blasts;
    material_fluid_blasts(materials_fluid(f.materials), &blasts);
    assert(blasts > 0);
    dispose(&f);
    return bodies[1].impulse.x + bodies[2].impulse.x;
}

/* A blast pushes a body made of parts once, as its nearest part it reaches,
 * even when its first part is out of reach. */
static void blasts_push_parts_once()
{
    float apart = blast_impulse(30, false), parts = blast_impulse(30, true);
    assert(parts > 0 && parts < apart * .6f);
    assert(blast_impulse(200, true) > 0);
}

/* Gas flows with a carried collider's surface as its body turns, though the
 * body's center of mass is still. The hull's left side moves at about -20
 * when the ship, 20 below it, turns at 1 radian a second. */
static void carried_colliders_move_gas()
{
    fixture f = create();
    enable_fluid(&f, 4, MATERIAL_SOLVER_DDR);
    ecs_entity ship = body(&f, 0, -20, BODY_KINEMATIC, 1, 0);
    Body_get(f.world, ship)->angular_velocity = 1;
    carried_hull(&f, ship);
    assert(step(&f, 0) == 0);
    emit_gas(&f, -9, 0, 0);
    step(&f, 1.0f / 60);
    int count;
    const fluid_particle *gas = material_fluid_particles(materials_fluid(f.materials), &count);
    assert(count == 1 && gas[0].velocity.x < -15);
    dispose(&f);
}

/* A follower whose parents have no body teleports with them. */
static void followers_without_a_body()
{
    fixture f = create();
    ecs_entity parent = ecs_world_create_entity(f.world);
    assert(Transform_add(f.world, parent, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    *Transform_get(f.world, parent) = (Transform){ 50, 0 };
    ecs_entity child = sensor(&f, 10, 0);
    assert(ecs_world_record_add_pair(f.world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    ecs_entity far = body(&f, 60, 0, BODY_KINEMATIC, 2, 4);
    ecs_entity near = body(&f, 10, 0, BODY_KINEMATIC, 2, 4);
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, child) == ecs_entity_to_u64(far));
    assert(inspect_body(&f, child).shape_count == 1);
    *Transform_get(f.world, parent) = (Transform){ 0, 0 };
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, child) == ecs_entity_to_u64(near));
    dispose(&f);
}

/* A continuous sensor stops partway through its sweep, after its followers
 * were tested at its end, so it cannot carry a collider. */
static void continuous_sensors_carry_no_colliders()
{
    fixture f = create();
    ecs_entity shot = sensor(&f, 0, 0);
    ecs_entity hitbox = body(&f, 4, 0, BODY_KINEMATIC, 1, 0);
    assert(ecs_world_record_add_pair(f.world, hitbox, ECS_CHILD_OF, shot, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(physics_tick(f.physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    assert(strstr(physics_error(f.physics), "cannot carry colliders"));
    dispose(&f);
}

/* A destructible under a placed parent breaks where its parent places it. */
static void followed_material()
{
    fixture f = create();
    assert(ecs_world_register_component(f.world, &COMPONENT_Destructible, NULL) == ECS_OK);
    ecs_entity parent = body(&f, 100, 0, BODY_KINEMATIC, 1, 0);
    Orientation_get(f.world, parent)->radians = 1.57079633f;
    ecs_entity slab = body(&f, 0, 0, BODY_STATIC, 8, 1);
    *Collider_get(f.world, slab) = (Collider){ .shape = COLLIDER_BOX,
                                               .width = 32,
                                               .height = 16,
                                               .category = 8,
                                               .mask = 1 };
    Destructible material = { .cell_size = 1, .min_fragment_cells = 1 };
    assert(ecs_world_record_add(f.world, slab, COMPONENT_Destructible.id, &material, 0) == ECS_OK);
    assert(ecs_world_record_add_pair(f.world, slab, ECS_CHILD_OF, parent, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(!materials_prepare(f.materials));
    /* The slab's local (0, 7) sits at world (93, 0) under the turned parent. */
    materials_damage(f.materials, slab, (fluid_vec){ 93, 0 }, 7, 1, 0, (material_impact){ 0 });
    assert(!materials_prepare(f.materials));
    const material_mask *mask = materials_mask(f.materials, slab);
    assert(!material_mask_contains(mask, 0, 5));
    assert(material_mask_contains(mask, 10, 5));
    dispose(&f);
}

static void current_contact_query()
{
    fixture f = create();
    ecs_entity actor = body(&f, 0, 0, BODY_DYNAMIC, 1, 2 | 8);
    ecs_entity target = body(&f, 8.32f, 0, BODY_STATIC, 2, 1);
    ecs_entity *found = NULL;
    assert(physics_contact_entities(f.world, f.entities, actor, 2, &found) == 0);
    assert(acount(found) == 1 && ecs_entity_to_u64(found[0]) == ecs_entity_to_u64(target));
    /* Sensor queries require actual geometry overlap, not the solver's gap. */
    Collider_get(f.world, target)->sensor = true;
    assert(physics_contact_entities(f.world, f.entities, actor, 2, &found) == 0);
    assert(acount(found) == 0);
    Transform_get(f.world, target)->x = 7;
    assert(physics_contact_entities(f.world, f.entities, actor, 2, &found) == 0);
    assert(acount(found) == 1);
    assert(step(&f, 0) == 2);
    /* An authored teleport is visible before physics synchronizes its cache. */
    Transform_get(f.world, target)->x = 100;
    assert(physics_contact_entities(f.world, f.entities, actor, 2, &found) == 0);
    assert(acount(found) == 0);
    *Transform_get(f.world, target) = (Transform){ 0 };
    *Collider_get(f.world, target) = (Collider){ .shape = COLLIDER_BOX,
                                                 .width = 2,
                                                 .height = 100,
                                                 .category = 8,
                                                 .mask = 1 };
    Orientation_get(f.world, target)->radians = .7f;
    *Transform_get(f.world, actor) = (Transform){ 5.32f * cosf(.7f),
                                                  5.32f * sinf(.7f) };
    assert(physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 1);
    assert(physics_contact_entities(f.world, f.entities, actor, 2, &found) == 0);
    assert(acount(found) == 0);
    Collider_get(f.world, target)->mask = 0;
    assert(physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    Collider_get(f.world, target)->mask = 1;
    Collider_get(f.world, actor)->ignore_entity = ecs_entity_to_u64(target);
    assert(physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    Collider_get(f.world, actor)->ignore_entity = 0;
    Collider_get(f.world, target)->ignore_entity = ecs_entity_to_u64(actor);
    assert(physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    Collider_get(f.world, target)->ignore_entity = 0;
    entities_request_destroy(f.entities, target);
    assert(physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    assert(entities_flush(f.entities) == 0);
    ecs_entity replacement = body(&f, 0, 0, BODY_STATIC, 8, 1);
    assert(replacement.slot == target.slot && replacement.generation != target.generation);
    assert(physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 1 && ecs_entity_to_u64(found[0]) == ecs_entity_to_u64(replacement));
    assert(ecs_world_record_destroy(f.world, replacement, 0) == ECS_OK);
    assert(physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    afree(found);
    dispose(&f);
}

static void raycasts()
{
    fixture f = create();
    ecs_entity circle = body(&f, 20, 0, BODY_STATIC, 8, 0);
    ecs_entity wall = body(&f, 50, 0, BODY_STATIC, 8, 0);
    Collider *box = Collider_get(f.world, wall);
    box->shape = COLLIDER_BOX;
    box->width = 4;
    box->height = 40;
    ray_hit hit;
    assert(
        physics_raycast(f.world, f.entities, (CF_V2){ 0 }, (CF_V2){ 100, 0 }, 8, (ecs_entity){ 0 }, false, &hit) ==
        0
    );
    assert(hit.found && ecs_entity_to_u64(hit.entity) == ecs_entity_to_u64(circle));
    assert(fabsf(hit.distance - 16) < .001f && fabsf(hit.normal.x + 1) < .001f);
    assert(
        physics_raycast(f.world, f.entities, (CF_V2){ 0 }, (CF_V2){ 100, 0 }, 8, circle, false, &hit) == 0
    );
    assert(hit.found && fabsf(hit.point.x - 48) < .001f);
    /* A rotated wall and a teleport are visible before any physics step. */
    Orientation_get(f.world, wall)->radians = 3.14159265f / 2;
    assert(
        physics_raycast(f.world, f.entities, (CF_V2){ 0 }, (CF_V2){ 100, 0 }, 8, circle, false, &hit) == 0
    );
    assert(hit.found && fabsf(hit.point.x - 30) < .001f);
    Transform_get(f.world, wall)->y = 100;
    Collider_get(f.world, circle)->sensor = true;
    assert(
        physics_raycast(f.world, f.entities, (CF_V2){ 0 }, (CF_V2){ 100, 0 }, 8, (ecs_entity){ 0 }, false, &hit) ==
        0
    );
    assert(!hit.found && hit.distance == 100 && hit.point.x == 100);
    assert(
        physics_raycast(f.world, f.entities, (CF_V2){ 20, 0 }, (CF_V2){ 100, 0 }, 8, (ecs_entity){ 0 }, true, &hit) ==
        0
    );
    assert(hit.found && hit.distance == 0 && hit.normal.x == -1);
    assert(
        physics_raycast(f.world, f.entities, (CF_V2){ 20, 0 }, (CF_V2){ 20, 0 }, 8, (ecs_entity){ 0 }, true, &hit) ==
        0
    );
    assert(hit.found && hit.distance == 0);
    entities_request_destroy(f.entities, circle);
    assert(
        physics_raycast(f.world, f.entities, (CF_V2){ 0 }, (CF_V2){ 100, 0 }, 8, (ecs_entity){ 0 }, true, &hit) ==
        0
    );
    assert(!hit.found);
    assert(
        physics_raycast(
            f.world,
            f.entities,
            (CF_V2){ NAN, 0 },
            (CF_V2){ 100, 0 },
            8,
            (ecs_entity){ 0 },
            true,
            &hit
        ) < 0
    );
    dispose(&f);
}

static void ordered_contacts()
{
    fixture f = create();
    enum
    {
        GROUPS = 48
    };
    ecs_entity actors[GROUPS][3];
    for (int group = 0; group < GROUPS; group++) {
        for (int i = 0; i < 3; i++) {
            actors[group][i] = body(&f, group * 40 + i * 6, 0, BODY_KINEMATIC, 1, 1);
        }
    }
    assert(step(&f, 0) == GROUPS * 4);
    int index = 0;
    for (int group = 0; group < GROUPS; group++) {
        const int order[] = { 0, 1, 1, 2 };
        for (int i = 0; i < 4; i++) {
            assert(
                ecs_entity_to_u64(contact_owner(&f, index++)) ==
                ecs_entity_to_u64(actors[group][order[i]])
            );
        }
        assert(other(&f, actors[group][1]) == ecs_entity_to_u64(actors[group][2]));
    }
    assert(step(&f, 1.0f / 60) == 0);
    for (int group = 0; group < GROUPS; group++) {
        Transform_get(f.world, actors[group][2])->y = 40;
    }
    assert(step(&f, 0) == 0);
    for (int group = 0; group < GROUPS; group++) {
        Transform_get(f.world, actors[group][2])->y = 0;
    }
    assert(step(&f, 0) == GROUPS * 2);
    for (int group = 0; group < GROUPS; group++) {
        for (int i = 0; i < 2; i++) {
            assert(
                ecs_entity_to_u64(contact_owner(&f, group * 2 + i)) ==
                ecs_entity_to_u64(actors[group][i + 1])
            );
        }
    }
    dispose(&f);
}

static void physics_cases()
{
    raycasts();
    solid_walls();
    sustained_dynamic_contact();
    contact_set_changes();
    compound_contacts();
    retained_mask_contacts();
    retained_mask_motion();
    reset_mask_incarnation();
    retained_mask_mass();
    retained_mask_solid_ccd();
    retained_mask_wakes_support();
    empty_mask_shapes();
    separated_solved_contacts();
    nearest_and_filters();
    nearest_reads_world_placement();
    moving_target_and_box();
    sweep_tree_lifetime();
    repeated_moving_sweeps();
    lifecycle_and_validation();
    interpolation();
    followers();
    followers_move_with_their_body();
    followers_ignore_their_anchor();
    carried_colliders_join_their_body();
    empty_carried_colliders_follow_their_body();
    carried_colliders_take_fluid_impulses();
    carried_colliders_move_gas();
    carried_colliders_share_fluid_response();
    blasts_push_parts_once();
    followers_without_a_body();
    continuous_sensors_carry_no_colliders();
    followed_material();
    current_contact_query();
    ordered_contacts();
}

static void configured_motion()
{
    for (int mode = 0; mode < 2; mode++) {
        fixture f = create();
        physics_settings settings = physics_defaults();
        settings.force_drive = mode;
        settings.sleep = false;
        settings.workers = 1;
        assert(physics_configure(f.physics, settings) == 0);
        ecs_entity dynamic = body(&f, 0, 0, BODY_DYNAMIC, 1, 0);
        ecs_entity kinematic = body(&f, 0, 20, BODY_KINEMATIC, 1, 0);
        *Velocity_get(f.world, dynamic) = (Velocity){ 60, 0 };
        *Velocity_get(f.world, kinematic) = (Velocity){ 60, 0 };
        /* A later invalid body must not accumulate an earlier body's drive
         * force when the author fixes the component and retries the tick. */
        Collider_get(f.world, kinematic)->radius = -1;
        assert(physics_tick(f.physics, 1.0f / 60) < 0);
        Collider_get(f.world, kinematic)->radius = 4;
        assert(step(&f, 1.0f / 60) == 0);
        float distance = Transform_get(f.world, dynamic)->x;
        assert(fabsf(distance - (mode ? .625f : 1)) < .001f);
        assert(fabsf(Velocity_get(f.world, dynamic)->x - 60) < .001f);
        assert(fabsf(Transform_get(f.world, kinematic)->x - 1) < .001f);
        physics_settings invalid = settings;
        invalid.substeps = 0;
        assert(physics_configure(f.physics, invalid) < 0);
        invalid = settings;
        invalid.contact_damping = NAN;
        assert(physics_configure(f.physics, invalid) < 0);
        invalid = settings;
        invalid.workers = 8;
        assert(physics_configure(f.physics, invalid) < 0);
        assert(physics_settings_equal(settings, physics_configuration(f.physics)));
        physics_reset(f.physics);
        assert(physics_settings_equal(settings, physics_configuration(f.physics)));
        assert(physics_configure(f.physics, invalid) == 0);
        dispose(&f);
    }
    for (int continuous = 0; continuous < 2; continuous++) {
        fixture f = create();
        physics_settings settings = physics_defaults();
        settings.substeps = 1;
        settings.continuous = continuous;
        assert(physics_configure(f.physics, settings) == 0);
        ecs_entity wall = body(&f, 50, 0, BODY_STATIC, 8, 1);
        *Collider_get(f.world, wall) = (Collider){ .shape = COLLIDER_BOX,
                                                   .width = 2,
                                                   .height = 200,
                                                   .category = 8,
                                                   .mask = 1 };
        ecs_entity actor = body(&f, 0, 0, BODY_DYNAMIC, 1, 8);
        Body_get(f.world, actor)->continuous = true;
        *Velocity_get(f.world, actor) = (Velocity){ 6000, 0 };
        step(&f, 1.0f / 60);
        float x = Transform_get(f.world, actor)->x;
        assert(continuous ? x < 46 : x > 90);
        dispose(&f);
    }
}

static void authored_bounce_and_drive()
{
    fixture f = create();
    ecs_entity wall = body(&f, 50, 0, BODY_STATIC, 2, 1);
    *Collider_get(f.world, wall) = (Collider){ .shape = COLLIDER_BOX,
                                               .width = 2,
                                               .height = 100,
                                               .category = 2,
                                               .mask = 1,
                                               .restitution = 1 };
    ecs_entity ball = body(&f, 0, 0, BODY_DYNAMIC, 1, 2);
    *Velocity_get(f.world, ball) = (Velocity){ 240, 0 };
    for (int i = 0; i < 20; i++) {
        assert(step(&f, 1.0f / 60) >= 0);
    }
    assert(Velocity_get(f.world, ball)->x < -200);
    dispose(&f);

    f = create();
    ecs_entity arm = body(&f, 20, 0, BODY_KINEMATIC, 2, 1);
    *Collider_get(f.world, arm) = (Collider){ .shape = COLLIDER_BOX,
                                              .width = 48,
                                              .height = 8,
                                              .category = 2,
                                              .mask = 1 };
    Body *rotating = Body_get(f.world, arm);
    rotating->angular_velocity = 6;
    ball = body(&f, 34, 10, BODY_DYNAMIC, 1, 2);
    for (int i = 0; i < 10; i++) {
        assert(step(&f, 1.0f / 60) >= 0);
    }
    assert(fabsf(Orientation_get(f.world, arm)->radians - 1) < .02f);
    assert(Velocity_get(f.world, ball)->y > 30);
    assert(fabsf(Transform_get(f.world, arm)->x - 20) < .01f);
    rotating = Body_get(f.world, arm);
    rotating->angular_velocity = 0;
    float stopped = Orientation_get(f.world, arm)->radians;
    for (int i = 0; i < 10; i++) {
        assert(step(&f, 1.0f / 60) >= 0);
    }
    assert(Orientation_get(f.world, arm)->radians == stopped);
    /* Continuous angles survive either direction through the pi boundary. */
    for (int direction = -1; direction <= 1; direction += 2) {
        Orientation_get(f.world, arm)->radians = direction * 3;
        rotating->angular_velocity = direction * 3.6f;
        for (int i = 0; i < 10; i++) {
            assert(step(&f, 1.0f / 60) >= 0);
        }
        assert(fabsf(Orientation_get(f.world, arm)->radians - direction * 3.6f) < .02f);
    }
    rotating->angular_velocity = NAN;
    assert(physics_tick(f.physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    rotating->angular_velocity = 1;
    rotating->type = BODY_DYNAMIC;
    assert(physics_tick(f.physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    rotating->type = BODY_KINEMATIC;
    assert(Orientation_remove(f.world, arm, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(physics_tick(f.physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    dispose(&f);
}

/* Terrain tiles are laid out along the world's axes, so terrain under a
 * rotated parent, which would turn them, is refused. */
static void rotated_terrain()
{
    fixture f = create();
    assert(ecs_world_register_component(f.world, &COMPONENT_MaterialTerrain, NULL) == ECS_OK);
    ecs_entity parent = ecs_world_create_entity(f.world);
    ecs_entity source = ecs_world_create_entity(f.world);
    assert(Transform_add(f.world, parent, 0) == ECS_OK);
    assert(Orientation_add(f.world, parent, 0) == ECS_OK);
    assert(Transform_add(f.world, source, 0) == ECS_OK);
    assert(ecs_world_record_add_pair(f.world, source, ECS_CHILD_OF, parent, 0) == ECS_OK);
    MaterialTerrain terrain = { .width = 64,
                                .height = 64,
                                .cell_size = 2,
                                .field = "Cave",
                                .min_fragment_cells = 1 };
    assert(
        ecs_world_record_add(f.world, source, COMPONENT_MaterialTerrain.id, &terrain, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    Orientation_get(f.world, parent)->radians = 0.5f;
    const char *error = materials_prepare(f.materials);
    assert(error && strstr(error, "unrotated"));
    dispose(&f);
}

/* A box Body with no Orientation, so a push cannot turn it. */
static ecs_entity crate(fixture *f, float x, float size, int type)
{
    ecs_entity entity = body(f, x, 0, type, 1, 1);
    *Collider_get(f->world, entity) = (Collider){ .shape = COLLIDER_BOX,
                                                  .width = size,
                                                  .height = size,
                                                  .category = 1,
                                                  .mask = 1 };
    return entity;
}

/* Pushes act through the center of mass at the next advancing step: bodies
 * weigh 1 kg per square meter, so an equal push moves a crate four times as
 * massive a quarter as fast, a force lasts one step, and only a live dynamic
 * Body takes one. */
static void pushes_divide_by_mass()
{
    fixture f = create();
    ecs_entity small = crate(&f, 0, 32, BODY_DYNAMIC);
    ecs_entity large = crate(&f, 200, 64, BODY_DYNAMIC);
    ecs_entity wall = crate(&f, -200, 32, BODY_STATIC);
    ecs_entity mover = crate(&f, 400, 32, BODY_KINEMATIC);
    assert(physics_apply_impulse(f.physics, small, (CF_V2){ 64, 0 }) == FLOW_OK);
    assert(physics_apply_impulse(f.physics, large, (CF_V2){ 64, 0 }) == FLOW_OK);
    assert(step(&f, 0) == 0 && Velocity_get(f.world, small)->x == 0);
    /* Each step writes a dynamic body's mass for scripts to size pushes. */
    assert(fabsf(Body_get(f.world, small)->mass - 1) < 1e-5f);
    assert(fabsf(Body_get(f.world, large)->mass - 4) < 1e-5f);
    Velocity_get(f.world, small)->y = 5; /* Pushes add to a set velocity. */
    step(&f, 1.0f / 60);
    Velocity v = *Velocity_get(f.world, small);
    assert(fabsf(v.x - 64) < .01f && fabsf(v.y - 5) < .01f);
    assert(fabsf(Velocity_get(f.world, large)->x - 16) < .01f);
    assert(physics_apply_force(f.physics, large, (CF_V2){ 0, 4 * 120 }) == FLOW_OK);
    assert(physics_apply_force(f.physics, large, (CF_V2){ 0, 4 * 120 }) == FLOW_OK);
    step(&f, 1.0f / 60);
    assert(fabsf(Velocity_get(f.world, large)->y - 4) < .01f);
    step(&f, 1.0f / 60);
    assert(fabsf(Velocity_get(f.world, large)->y - 4) < .01f);
    assert(physics_apply_impulse(f.physics, wall, (CF_V2){ 1, 0 }) == FLOW_ERR_ARGUMENT);
    assert(physics_apply_force(f.physics, mover, (CF_V2){ 1, 0 }) == FLOW_ERR_ARGUMENT);
    assert(physics_apply_impulse(f.physics, small, (CF_V2){ NAN, 0 }) == FLOW_ERR_ARGUMENT);
    assert(physics_apply_impulse(f.physics, small, (CF_V2){ 0, 64 }) == FLOW_OK);
    physics_reset(f.physics); /* A reset drops queued pushes. */
    step(&f, 1.0f / 60);
    assert(fabsf(Velocity_get(f.world, small)->y - 5) < .01f);
    assert(physics_apply_impulse(f.physics, small, (CF_V2){ 0, 64 }) == FLOW_OK);
    assert(ecs_world_destroy_entity(f.world, small) == ECS_OK);
    ecs_entity reused = crate(&f, 0, 32, BODY_DYNAMIC);
    assert(reused.slot == small.slot);
    step(&f, 1.0f / 60); /* The push named the destroyed crate. */
    assert(Velocity_get(f.world, reused)->y == 0);
    dispose(&f);
}

/* Ticks once and checks that the tick fails with `message` in its error. */
static void refuses(fixture *f, const char *message)
{
    assert(physics_tick(f->physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    assert(strstr(physics_error(f->physics), message));
}

/* One entity's Gravity pulls dynamic bodies and material liquids, and a
 * change wakes bodies asleep under the old one. Without one, nothing falls. */
static void gravity_pulls_bodies_and_fluids()
{
    fixture f = create();
    enable_fluid(&f, 0, MATERIAL_SOLVER_DDR);
    ecs_entity floor = crate(&f, 0, 32, BODY_STATIC);
    Transform_get(f.world, floor)->y = -40;
    Collider_get(f.world, floor)->width = 400;
    Collider_get(f.world, floor)->height = 8;
    ecs_entity box = crate(&f, 0, 16, BODY_DYNAMIC);
    assert(step(&f, 0) == 0);
    assert(materials_emit(
        f.materials,
        (fluid_particle){ .position = { -24, 24 }, .mass = 1, .radius = 1, .kind = FLUID_LIQUID }
    ));
    step(&f, 1.0f / 60);
    int count;
    const fluid_particle *drop = material_fluid_particles(materials_fluid(f.materials), &count);
    assert(count == 1 && drop->velocity.y == 0);
    assert(Velocity_get(f.world, box)->y == 0);
    assert(ecs_world_register_component(f.world, &COMPONENT_Gravity, NULL) == ECS_OK);
    ecs_entity scene = ecs_world_create_entity(f.world);
    assert(Gravity_add(f.world, scene, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    Gravity_get(f.world, scene)->acceleration = (CF_V2){ 0, -320 };
    step(&f, 1.0f / 60);
    drop = material_fluid_particles(materials_fluid(f.materials), &count);
    assert(count == 1 && fabsf(drop->velocity.y + 320.0f / 60) < .01f);
    assert(fabsf(Velocity_get(f.world, box)->y + 320.0f / 60) < .01f);
    for (int i = 0; i < 120; i++) {
        step(&f, 1.0f / 60);
    }
    assert(!inspect_body(&f, box).awake);
    Gravity_get(f.world, scene)->acceleration = (CF_V2){ 320, 0 };
    step(&f, 1.0f / 60);
    assert(Velocity_get(f.world, box)->x > 5);
    Gravity_get(f.world, scene)->acceleration = (CF_V2){ 0, -5000 };
    refuses(&f, "Gravity within +/-4096");
    Gravity_get(f.world, scene)->acceleration = (CF_V2){ NAN, 0 };
    refuses(&f, "Gravity acceleration must be finite");
    Gravity_get(f.world, scene)->acceleration = (CF_V2){ 0 };
    ecs_entity second = ecs_world_create_entity(f.world);
    assert(Gravity_add(f.world, second, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    refuses(&f, "Only one entity may have Gravity; 2 do");
    dispose(&f);
}

/* A body turning a radian a second for a second has turned a radian: reading
 * its written Orientation back does not move it. */
static void turning_bodies_keep_their_angle()
{
    fixture f = create();
    ecs_entity wheel = crate(&f, 0, 32, BODY_KINEMATIC);
    Body_get(f.world, wheel)->angular_velocity = 1;
    for (int i = 0; i < 60; i++) {
        step(&f, 1.0f / 60);
    }
    assert(fabsf(Orientation_get_const(f.world, wheel)->radians - 1) < 1e-4f);
    dispose(&f);
}

/* A fixture with Joint and a Gravity pulling down at 320 units/second^2. */
static fixture joined()
{
    fixture f = create();
    assert(ecs_world_register_component(f.world, &COMPONENT_Joint, NULL) == ECS_OK);
    assert(ecs_world_register_component(f.world, &COMPONENT_Gravity, NULL) == ECS_OK);
    ecs_entity scene = ecs_world_create_entity(f.world);
    assert(Gravity_add(f.world, scene, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    Gravity_get(f.world, scene)->acceleration = (CF_V2){ 0, -320 };
    return f;
}

/* A square crate at x, y that turns. */
static ecs_entity turning(fixture *f, float x, float y, float size, int type)
{
    ecs_entity entity = crate(f, x, size, type);
    Transform_get(f->world, entity)->y = y;
    Body_get(f->world, entity)->allow_rotation = true;
    return entity;
}

/* A Transform without a collider at x, y under parent. */
static ecs_entity point(fixture *f, ecs_entity parent, float x, float y)
{
    ecs_entity entity = ecs_world_create_entity(f->world);
    assert(Transform_add(f->world, entity, 0) == ECS_OK);
    if (parent.slot) {
        assert(ecs_world_record_add_pair(f->world, entity, ECS_CHILD_OF, parent, 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    *Transform_get(f->world, entity) = (Transform){ x, y };
    return entity;
}

/* Joins entity's body to other's, or to the world when other is zero. */
static Joint *join(fixture *f, ecs_entity entity, JointKind kind, ecs_entity other)
{
    assert(Joint_add(f->world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    Joint *joint = Joint_get(f->world, entity);
    joint->kind = kind;
    joint->other = other.slot ? ecs_entity_to_u64(other) : 0;
    return joint;
}

static void steps(fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        step(f, 1.0f / 60);
    }
}

static CF_V2 at(fixture *f, ecs_entity entity)
{
    return *Transform_get_const(f->world, entity);
}

static float turned(fixture *f, ecs_entity entity)
{
    return Orientation_get_const(f->world, entity)->radians;
}

static bool near(float value, float expected, float tolerance)
{
    return fabsf(value - expected) <= tolerance;
}

static bool near_v2(CF_V2 value, float x, float y, float tolerance)
{
    return near(value.x, x, tolerance) && near(value.y, y, tolerance);
}

static float apart(fixture *f, ecs_entity a, ecs_entity b)
{
    return cf_len(cf_sub(at(f, a), at(f, b)));
}

static const ecs_entity world_body = { 0 };

TEST_CASE(physics_hinges_and_sliders_keep_to_their_axis)
{
    fixture f = joined();
    ecs_entity door = turning(&f, 0, 64, 32, BODY_DYNAMIC);
    Joint *hinge = join(&f, door, JOINT_HINGE, world_body);
    hinge->motor = true;
    hinge->speed = 1;
    hinge->strength = 1e9f;
    ecs_entity gate = turning(&f, -200, 64, 32, BODY_DYNAMIC);
    hinge = join(&f, gate, JOINT_HINGE, world_body);
    hinge->motor = hinge->limit = true;
    hinge->speed = 1;
    hinge->strength = 1e4f;
    hinge->upper = .5f;
    ecs_entity lift = turning(&f, 200, 64, 32, BODY_DYNAMIC);
    Joint *slider = join(&f, lift, JOINT_SLIDER, world_body);
    slider->limit = true;
    slider->lower = -32;
    /* A sled's slider runs along its Y axis, tilted an eighth turn. */
    ecs_entity sled = turning(&f, 400, 64, 32, BODY_DYNAMIC);
    Orientation_get(f.world, sled)->radians = -.78539816f;
    slider = join(&f, sled, JOINT_SLIDER, world_body);
    slider->limit = true;
    slider->lower = -32;
    /* A carriage on a turned base slides along its own Y axis. */
    ecs_entity base = turning(&f, 600, 64, 32, BODY_STATIC);
    Orientation_get(f.world, base)->radians = 1.57079633f;
    ecs_entity carriage = turning(&f, 700, 64, 32, BODY_DYNAMIC);
    slider = join(&f, carriage, JOINT_SLIDER, base);
    slider->limit = true;
    slider->lower = -32;
    steps(&f, 60);
    /* The hinge holds the door up and its motor turns it. */
    REQUIRE(near_v2(at(&f, door), 0, 64, .5f) && near(turned(&f, door), 1, .03f));
    /* The gate's motor turns it only to its limit. */
    REQUIRE(near(turned(&f, gate), .5f, .02f));
    /* The lift fell to its lower limit and stays there, upright. */
    REQUIRE(near_v2(at(&f, lift), 200, 32, .5f));
    REQUIRE(physics_apply_impulse(f.physics, lift, (CF_V2){ 1e4f, 0 }) == FLOW_OK);
    steps(&f, 10);
    REQUIRE(near_v2(at(&f, lift), 200, 32, .5f) && near(turned(&f, lift), 0, 1e-3f));
    REQUIRE(near_v2(at(&f, sled), 400 - 22.627f, 64 - 22.627f, .5f));
    REQUIRE(near(turned(&f, sled), -.78539816f, 1e-3f));
    REQUIRE(near_v2(at(&f, carriage), 700, 32, .5f) && near(turned(&f, carriage), 0, 1e-3f));
    dispose(&f);
    return true;
}

TEST_CASE(physics_welds_and_distances_hold_bodies)
{
    fixture f = joined();
    /* A bob swings on a hinge 32 units from its center. */
    ecs_entity bob = turning(&f, 32, 0, 8, BODY_DYNAMIC);
    join(&f, point(&f, bob, -32, 0), JOINT_HINGE, world_body);
    /* Two crates welded side by side fall and take a push as one. */
    ecs_entity left = turning(&f, 200, 0, 16, BODY_DYNAMIC);
    ecs_entity right = turning(&f, 216, 0, 16, BODY_DYNAMIC);
    join(&f, right, JOINT_WELD, left);
    /* A beam welded to a post at an angle keeps that angle. */
    ecs_entity mast = turning(&f, 1000, 0, 16, BODY_DYNAMIC);
    Orientation_get(f.world, mast)->radians = .5f;
    ecs_entity boom = turning(&f, 1020, 0, 16, BODY_DYNAMIC);
    Orientation_get(f.world, boom)->radians = -.3f;
    join(&f, boom, JOINT_WELD, mast);
    /* A rod keeps a weight 64 units from a fixed post, and ropes let two
     * more fall until they are taut, one tied on at its hook's origin. */
    ecs_entity post = crate(&f, 400, 8, BODY_STATIC);
    ecs_entity weight = turning(&f, 400, -64, 8, BODY_DYNAMIC);
    join(&f, weight, JOINT_DISTANCE, post);
    Velocity_get(f.world, weight)->x = 64;
    ecs_entity hook = crate(&f, 600, 8, BODY_STATIC);
    ecs_entity load = turning(&f, 600, -16, 8, BODY_DYNAMIC);
    Joint *rope = join(&f, load, JOINT_DISTANCE, hook);
    rope->spring = rope->limit = true;
    rope->upper = 64;
    ecs_entity knot = crate(&f, 800, 8, BODY_STATIC);
    ecs_entity sack = turning(&f, 800, -16, 8, BODY_DYNAMIC);
    rope = join(&f, point(&f, sack, 0, 16), JOINT_DISTANCE, knot);
    rope->spring = rope->limit = true;
    rope->upper = 64;
    steps(&f, 2);
    REQUIRE(physics_apply_impulse(f.physics, left, (CF_V2){ 0, 64 }) == FLOW_OK);
    steps(&f, 58);
    /* A second in, the bob has swung through to the far side. */
    REQUIRE(near(cf_len(at(&f, bob)), 32, .5f) && at(&f, bob).x < -16);
    /* The push turned the pair; the right crate stays 16 along the left's X. */
    CF_V2 offset = cf_mul_T_sc_v2(cf_sincos(turned(&f, left)), cf_sub(at(&f, right), at(&f, left)));
    REQUIRE(near_v2(offset, 16, 0, .5f) && fabsf(turned(&f, left)) > .1f);
    REQUIRE(near(turned(&f, right), turned(&f, left), 1e-2f));
    REQUIRE(near(turned(&f, boom) - turned(&f, mast), -.8f, 1e-2f));
    REQUIRE(near(apart(&f, weight, post), 64, .5f) && at(&f, weight).x > 416);
    REQUIRE(near_v2(at(&f, load), 600, -64, 1));
    REQUIRE(near_v2(at(&f, sack), 800, -80, 1));
    dispose(&f);
    return true;
}

TEST_CASE(physics_joints_follow_their_components)
{
    fixture f = joined();
    ecs_entity lamp = turning(&f, 0, 160, 32, BODY_DYNAMIC);
    join(&f, lamp, JOINT_HINGE, world_body);
    steps(&f, 10);
    REQUIRE(near_v2(at(&f, lamp), 0, 160, .5f) && turned(&f, lamp) == 0);
    /* A field change makes the joint again: now its motor turns it. */
    Joint *hinge = Joint_get(f.world, lamp);
    hinge->motor = true;
    hinge->speed = -2;
    hinge->strength = 1e9f;
    steps(&f, 10);
    float before = turned(&f, lamp);
    steps(&f, 30);
    REQUIRE(near(turned(&f, lamp) - before, -1, .03f));
    /* Moving the joint to the wheel's rim makes it again there. */
    ecs_entity wheel = turning(&f, 300, 160, 32, BODY_DYNAMIC);
    ecs_entity axle = point(&f, wheel, 0, 0);
    hinge = join(&f, axle, JOINT_HINGE, world_body);
    hinge->motor = true;
    hinge->speed = 2;
    hinge->strength = 1e9f;
    steps(&f, 10);
    REQUIRE(near_v2(at(&f, wheel), 300, 160, .5f));
    *Transform_get(f.world, axle) = cf_mul_T_sc_v2(cf_sincos(turned(&f, wheel)), (CF_V2){ 32, 0 });
    steps(&f, 20);
    CF_V2 rim = { 332, 160 };
    REQUIRE(near(cf_len(cf_sub(at(&f, wheel), rim)), 32, .5f));
    REQUIRE(!near_v2(at(&f, wheel), 300, 160, 8));
    /* Limits that are off are ignored, however they are set. */
    ecs_entity drawer = turning(&f, 600, 160, 32, BODY_DYNAMIC);
    join(&f, drawer, JOINT_SLIDER, world_body)->lower = 1;
    ecs_entity vane = turning(&f, 800, 160, 32, BODY_DYNAMIC);
    join(&f, vane, JOINT_HINGE, world_body)->upper = -5;
    /* Without its Joint the lamp falls. */
    REQUIRE(Joint_remove(f.world, lamp, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(f.world) == ECS_OK);
    /* A weld to a body that is gone holds nothing. */
    ecs_entity shelf = turning(&f, 1000, 160, 32, BODY_KINEMATIC);
    ecs_entity vase = turning(&f, 1032, 160, 32, BODY_DYNAMIC);
    join(&f, vase, JOINT_WELD, shelf);
    steps(&f, 10);
    REQUIRE(at(&f, lamp).y < 160 - 2 && at(&f, drawer).y < 160 - 2);
    REQUIRE(near_v2(at(&f, vane), 800, 160, .5f));
    REQUIRE(near_v2(at(&f, vase), 1032, 160, .5f));
    REQUIRE(ecs_world_destroy_entity(f.world, shelf) == ECS_OK);
    steps(&f, 10);
    REQUIRE(at(&f, vase).y < 160 - 2);
    /* A body being destroyed releases a joint on an entity under it, and a
     * joint to one, in the step before its children are destroyed too. */
    ecs_entity bob = turning(&f, 1200, 160, 8, BODY_DYNAMIC);
    join(&f, point(&f, bob, -32, 0), JOINT_HINGE, world_body);
    ecs_entity weight = turning(&f, 1300, 160, 8, BODY_DYNAMIC);
    join(&f, weight, JOINT_DISTANCE, point(&f, bob, 0, 0));
    steps(&f, 2);
    entities_request_destroy(f.entities, bob);
    steps(&f, 1);
    REQUIRE(entities_flush(f.entities) == 0);
    steps(&f, 1);
    dispose(&f);
    return true;
}

/* Joined bodies that may not collide report no contact; collide lets them
 * touch and report it. */
TEST_CASE(physics_joined_bodies_touch_only_with_collide)
{
    fixture f = joined();
    ecs_entity chassis = crate(&f, 0, 64, BODY_STATIC);
    ecs_entity wheel = turning(&f, 16, 0, 16, BODY_DYNAMIC);
    Joint *axle = join(&f, point(&f, wheel, 0, 0), JOINT_HINGE, chassis);
    ecs_entity *found = NULL;
    REQUIRE(physics_contact_entities(f.world, f.entities, wheel, 1, &found) == FLOW_OK);
    REQUIRE(acount(found) == 0);
    REQUIRE(step(&f, 1.0f / 60) == 0 && step(&f, 1.0f / 60) == 0);
    REQUIRE(near_v2(at(&f, wheel), 16, 0, .5f));
    REQUIRE(physics_contact_entities(f.world, f.entities, chassis, 1, &found) == FLOW_OK);
    REQUIRE(acount(found) == 0);
    axle->collide = true;
    REQUIRE(physics_contact_entities(f.world, f.entities, chassis, 1, &found) == FLOW_OK);
    REQUIRE(acount(found) == 1 && ecs_entity_to_u64(found[0]) == ecs_entity_to_u64(wheel));
    REQUIRE(step(&f, 1.0f / 60) == 2);
    REQUIRE(other(&f, wheel) == ecs_entity_to_u64(chassis));
    afree(found);
    dispose(&f);
    return true;
}

static bool fails_with(fixture *f, const char *message)
{
    return physics_tick(f->physics, 1.0f / 60) == FLOW_ERR_ARGUMENT &&
           strstr(physics_error(f->physics), message);
}

/* Fails with message, then ticks once entity's Joint is a hinge to the world. */
static bool joint_fails(fixture *f, ecs_entity entity, const char *message)
{
    bool failed = fails_with(f, message);
    *Joint_get(f->world, entity) = (Joint){ 0 };
    return failed && physics_tick(f->physics, 1.0f / 60) == FLOW_OK;
}

TEST_CASE(physics_joints_need_two_bodies_and_sound_values)
{
    fixture f = joined();
    ecs_entity bare = ecs_world_create_entity(f.world);
    REQUIRE(Joint_add(f.world, bare, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(f.world) == ECS_OK);
    REQUIRE(fails_with(&f, "A Joint needs a Transform on entity"));
    REQUIRE(ecs_world_destroy_entity(f.world, bare) == ECS_OK);
    ecs_entity loose = point(&f, world_body, 0, 0);
    join(&f, loose, JOINT_WELD, world_body);
    REQUIRE(fails_with(&f, "A Joint must be on or under a body on entity"));
    REQUIRE(Joint_remove(f.world, loose, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(f.world) == ECS_OK);
    ecs_entity box = crate(&f, 0, 32, BODY_DYNAMIC);
    ecs_entity grip = point(&f, box, 16, 0);
    Joint *joint = join(&f, grip, JOINT_WELD, loose);
    REQUIRE(joint_fails(&f, grip, "A Joint's other must be on or under a body"));
    Joint_get(f.world, grip)->other = ecs_entity_to_u64(box);
    REQUIRE(joint_fails(&f, grip, "A Joint must join two different bodies"));
    Joint_get(f.world, grip)->kind = JOINT_DISTANCE;
    REQUIRE(joint_fails(&f, grip, "A distance Joint needs another body"));
    ecs_entity post = crate(&f, 200, 8, BODY_STATIC);
    joint = Joint_get(f.world, grip);
    joint->kind = JOINT_DISTANCE;
    joint->other = ecs_entity_to_u64(post);
    joint->limit = true;
    joint->lower = -2;
    joint->upper = -1;
    REQUIRE(joint_fails(&f, grip, "A distance Joint limit needs lower at least 0"));
    /* A transform-only joint entity or other must be finite too. */
    Joint_get(f.world, grip)->other = ecs_entity_to_u64(post);
    Transform_get(f.world, grip)->x = NAN;
    REQUIRE(fails_with(&f, "A Joint and its other need finite Transforms and Orientations"));
    Transform_get(f.world, grip)->x = 16;
    ecs_entity tip = point(&f, post, INFINITY, 0);
    Joint_get(f.world, grip)->other = ecs_entity_to_u64(tip);
    REQUIRE(joint_fails(&f, grip, "A Joint and its other need finite Transforms and Orientations"));
    joint = Joint_get(f.world, grip);
    joint->limit = true;
    joint->upper = 3.12f;
    REQUIRE(joint_fails(&f, grip, "A hinge Joint limit needs lower and upper within 0.99 pi"));
    joint->limit = true;
    joint->lower = 1;
    REQUIRE(joint_fails(&f, grip, "A Joint limit needs lower at most upper"));
    Joint_get(f.world, grip)->hertz = -1;
    REQUIRE(joint_fails(&f, grip, "Joint strength, hertz and damping must not be negative"));
    Joint_get(f.world, grip)->speed = NAN;
    REQUIRE(joint_fails(&f, grip, "Joint values must be finite"));
    Joint_get(f.world, grip)->kind = (JointKind)99;
    REQUIRE(joint_fails(&f, grip, "Unknown Joint kind"));
    dispose(&f);
    return true;
}

#ifdef CHIBI_TEST_PREPARATION

#endif

TEST_CASE(physics_gravity_pulls_bodies_and_fluids)
{
    gravity_pulls_bodies_and_fluids();
    return true;
}

TEST_CASE(physics_pushes_divide_by_mass)
{
    pushes_divide_by_mass();
    return true;
}

TEST_CASE(physics_rotated_terrain_is_refused)
{
    rotated_terrain();
    return true;
}

TEST_CASE(physics_task_pool_runs_every_range)
{
    test_task_pool();
    return true;
}

TEST_CASE(physics_authored_motion)
{
    authored_bounce_and_drive();
    configured_motion();
    turning_bodies_keep_their_angle();
    return true;
}

TEST_CASE(physics_contacts_and_queries_at_each_substep_count)
{
    for (int substeps = 4; substeps >= 2; substeps -= 2) {
        configured_substeps = substeps;
        physics_cases();
    }
    /* One substep fails the four-step crowd separation tolerance. It remains
     * an explicit quality choice; verify wall blocking and swept hits here. */
    configured_substeps = 1;
    solid_walls();
    nearest_and_filters();
    moving_target_and_box();
    configured_substeps = 4;
    return true;
}

/* Padding every case above the parallel collection threshold must not change
 * results or contact order. */
TEST_CASE(physics_parallel_collection_matches_serial)
{
    task_pool *pool = task_pool_acquire();
    expected_workers = task_pool_tasks(pool)->workers;
    large_world = true;
    physics_cases();
    large_world = false;
    task_pool_release(pool);
    return true;
}

void test_native_catalog_enable();
void test_tool_physics()
{
    test_native_catalog_enable(); /* Contact payloads copy a CF_V2. */
    RUN_TEST_CASE(physics_task_pool_runs_every_range);
    RUN_TEST_CASE(physics_rotated_terrain_is_refused);
    RUN_TEST_CASE(physics_authored_motion);
    RUN_TEST_CASE(physics_pushes_divide_by_mass);
    RUN_TEST_CASE(physics_gravity_pulls_bodies_and_fluids);
    RUN_TEST_CASE(physics_hinges_and_sliders_keep_to_their_axis);
    RUN_TEST_CASE(physics_welds_and_distances_hold_bodies);
    RUN_TEST_CASE(physics_joints_follow_their_components);
    RUN_TEST_CASE(physics_joined_bodies_touch_only_with_collide);
    RUN_TEST_CASE(physics_joints_need_two_bodies_and_sound_values);
#ifdef CHIBI_TEST_PREPARATION
#endif
    RUN_TEST_CASE(physics_contacts_and_queries_at_each_substep_count);
    RUN_TEST_CASE(physics_parallel_collection_matches_serial);
}
