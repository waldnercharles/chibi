#include "runtime/scenes/physics.h"

#include "execution/status.h"
#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/entities_native.h"
#include "runtime/component_catalog.h"
#include "runtime/events.h"
#include "runtime/materials.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 2D physics through its real seams: ECS components in, Contact Begin events
 * out through game_entities. The host records each event in dispatch order. */
typedef struct
{
    ecs_entity owner;
    uint64_t other;
} contact;

typedef struct
{
    ecs_world *world;
    contact *contacts; /* This step's events. */
    contact *latest;   /* The last Other each owner saw. */
} recorder;

typedef struct
{
    ecs_world *world;
    game_entities *entities;
    game_materials *materials;
    game_physics *physics;
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
    game_task_pool *pool = game_task_pool_acquire();
    game_task_pool *other = game_task_pool_acquire();
    assert(pool == other);
    game_task_system tasks = *game_task_pool_tasks(pool);
    bool started = game_task_pool_started(pool);
    int *values = xcalloc((size_t)JOBS * ITEMS, sizeof *values);
    task_fixture fixtures[JOBS];
    void *handles[JOBS];

    game_task_pool_begin(pool, false);
    task_fixture serial = { values, 3, 1 };
    assert(!tasks.enqueue(fill_range, ITEMS, 64, &serial, tasks.user));
    game_task_pool_end(pool);
    assert(game_task_pool_started(pool) == started);
    for (int i = 0; i < ITEMS; i++) {
        assert(values[i] == (i + 1) * 3);
    }
    game_task_pool_release(other);

    /* Exceed retained task slots, join in reverse order, and verify every
     * range is complete before its argument storage is reused or freed. */
    game_task_pool_begin(pool, true);
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
    game_task_pool_end(pool);
    assert(game_task_pool_started(pool) == (tasks.workers > 1));

    game_task_pool_begin(pool, false);
    assert(!tasks.enqueue(fill_range, ITEMS, 64, &serial, tasks.user));
    game_task_pool_end(pool);
    free(values);
    game_task_pool *serial_pool = game_task_pool_acquire_workers(1);
    game_task_pool *wide = game_task_pool_acquire_workers(8);
    game_task_pool *wide_shared = game_task_pool_acquire_workers(8);
    assert(wide == wide_shared);
    assert(game_task_pool_tasks(serial_pool)->workers == 1);
    assert(
        game_task_pool_tasks(wide)->workers >= 1 && game_task_pool_tasks(wide)->workers <= 8
    );
    game_task_pool_release(wide_shared);
    game_task_pool_release(serial_pool);
    game_task_pool_release(wide);
    game_task_pool_release(pool);
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
    assert(game_host_event_is(event, GAME_HOST_EVENT_CONTACT_BEGIN) && arguments.count == 2);
    contact hit = { owner, arguments.values[0].u64 };
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
    const ecs_component_desc *components[] = { &GAME_COMPONENT_GameTransform,
                                               &GAME_COMPONENT_GameCollider,
                                               &GAME_COMPONENT_GameBody,
                                               &GAME_COMPONENT_GameVelocity,
                                               &GAME_COMPONENT_GameOrientation };
    for (int i = 0; i < 5; i++) {
        assert(ecs_world_register_component(f->world, components[i], NULL) == ECS_OK);
    }
    const game_entity_host host = { .context = f->events,
                                    .destroy = destroy,
                                    .attach = attach,
                                    .dispatch = dispatch };
    f->entities = game_entities_new(f->world, &host);
    f->materials = game_materials_new(f->world, f->entities, NULL);
    game_entities_bind_materials(f->entities, f->materials);
    f->physics = game_physics_new(f->world, f->entities);
    game_physics_settings settings = game_physics_defaults();
    settings.substeps = configured_substeps;
    assert(game_physics_configure(f->physics, settings) == 0);
    if (large_world) {
        /* Put every semantic case above the parallel collection threshold.
         * These distant, masked bodies produce no additional contacts. */
        for (int i = 0; i < PARALLEL_PADDING; i++) {
            ecs_entity entity = ecs_world_create_entity(f->world);
            assert(GAME_GameTransform_add(f->world, entity, 0) == ECS_OK);
            assert(GAME_GameCollider_add(f->world, entity, 0) == ECS_OK);
            assert(ecs_world_apply_commands(f->world) == ECS_OK);
            *GAME_GameTransform_get(f->world, entity) = (GameTransform){ 10000 + i * 20,
                                                                         10000 };
            *GAME_GameCollider_get(f->world, entity) = (GameCollider){ .radius = 1 };
        }
    }
    return made;
}

static void dispose(fixture *f)
{
    game_physics_free(f->physics);
    game_materials_free(f->materials);
    game_entities_free(f->entities);
    ecs_world_free(f->world);
    afree(f->events->contacts);
    afree(f->events->latest);
    free(f->events);
}

static ecs_entity body(fixture *f, float x, float y, int type, unsigned category, unsigned mask)
{
    ecs_entity entity = ecs_world_create_entity(f->world);
    assert(GAME_GameTransform_add(f->world, entity, 0) == ECS_OK);
    assert(GAME_GameCollider_add(f->world, entity, 0) == ECS_OK);
    assert(GAME_GameBody_add(f->world, entity, 0) == ECS_OK);
    assert(GAME_GameVelocity_add(f->world, entity, 0) == ECS_OK);
    assert(GAME_GameOrientation_add(f->world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    *GAME_GameTransform_get(f->world, entity) = (GameTransform){ x, y };
    *GAME_GameCollider_get(f->world, entity) = (GameCollider){ .radius = 4,
                                                               .category = category,
                                                               .mask = mask };
    *GAME_GameBody_get(f->world, entity) = (GameBody){ .type = type };
    return entity;
}

static ecs_entity sensor(fixture *f, float x, float y)
{
    ecs_entity entity = body(f, x, y, GAME_BODY_KINEMATIC, 4, 2 | 8);
    GAME_GameCollider_get(f->world, entity)->sensor = true;
    GAME_GameCollider_get(f->world, entity)->radius = 2;
    GAME_GameBody_get(f->world, entity)->continuous = true;
    return entity;
}

static int step(fixture *f, float delta)
{
    aclear(f->events->contacts);
    int result = game_physics_tick(f->physics, delta);
    if (result < 0) {
        fprintf(stderr, "physics: %s\n", game_physics_error(f->physics));
    }
    assert(result == 0);
    assert(game_physics_workers(f->physics) == (large_world ? expected_workers : 1));
    do {
        assert(game_entities_dispatch_events(f->entities) >= 0);
    } while (game_entities_pending_events(f->entities));
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

static ecs_entity contact_owner(fixture *f, int index)
{
    assert(index < acount(f->events->contacts));
    return f->events->contacts[index].owner;
}

static void solid_walls()
{
    fixture f = create();
    ecs_entity wall = body(&f, 50, 0, GAME_BODY_STATIC, 8, 1);
    GameCollider *shape = GAME_GameCollider_get(f.world, wall);
    shape->shape = GAME_COLLIDER_BOX;
    shape->width = 2;
    shape->height = 200;
    ecs_entity actor = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 8);
    GAME_GameCollider_get(f.world, actor)->radius = 8;
    GAME_GameBody_get(f.world, actor)->continuous = true;
    int contacts = 0;
    for (int i = 0; i < 20; i++) {
        *GAME_GameVelocity_get(f.world, actor) = (GameVelocity){ 600, 40 };
        contacts += step(&f, 1.0f / 60);
    }
    GameTransform position = *GAME_GameTransform_get(f.world, actor);
    /* Lower substeps deliberately trade overlap recovery accuracy for cost.
     * Retain the original four-step tolerance; all modes must slide, stay on
     * this side of the wall and preserve one Contact Begin per receiver. */
    float max_x = configured_substeps == 4 ? 41.5f : 49;
    assert(position.x > 40 && position.x < max_x && position.y > 12);
    assert(contacts == 2);
    assert(other(&f, actor) == ecs_entity_to_u64(wall));
    assert(GAME_GameVelocity_get(f.world, actor)->x < 1);
    /* Moving away and back must start a new contact, while a held push above
     * must retain the first one across solver separation tolerances. */
    GAME_GameTransform_get(f.world, actor)->x = 0;
    *GAME_GameVelocity_get(f.world, actor) = (GameVelocity){ 0 };
    assert(step(&f, 0) == 0);
    *GAME_GameVelocity_get(f.world, actor) = (GameVelocity){ 6000, 0 };
    assert(step(&f, 1.0f / 60) == 2);
    assert(GAME_GameTransform_get(f.world, actor)->x < 41.5f);
    dispose(&f);
}

static void sustained_dynamic_contact()
{
    fixture f = create();
    ecs_entity left = body(&f, -60, 0, GAME_BODY_DYNAMIC, 2, 15);
    ecs_entity right = body(&f, 60, 0, GAME_BODY_DYNAMIC, 2, 15);
    GAME_GameCollider_get(f.world, left)->radius = 10;
    GAME_GameCollider_get(f.world, right)->radius = 10;
    int contacts = 0;
    for (int i = 0; i < 360; i++) {
        *GAME_GameVelocity_get(f.world, left) = (GameVelocity){ 55, 0 };
        *GAME_GameVelocity_get(f.world, right) = (GameVelocity){ -55, 0 };
        contacts += step(&f, 1.0f / 60);
        float separation = GAME_GameTransform_get_const(f.world, right)->x -
                           GAME_GameTransform_get_const(f.world, left)->x;
        assert(separation >= 19);
    }
    assert(contacts == 2);
    assert(other(&f, left) == ecs_entity_to_u64(right));
    assert(fabsf(GAME_GameVelocity_get_const(f.world, left)->x) < 1);
    dispose(&f);
}

static void contact_set_changes()
{
    fixture f = create();
    ecs_entity a = body(&f, 0, 0, GAME_BODY_KINEMATIC, 1, 1);
    ecs_entity b = body(&f, 6, 0, GAME_BODY_KINEMATIC, 1, 1);
    ecs_entity c = body(&f, 12, 0, GAME_BODY_KINEMATIC, 1, 1);
    GAME_GameCollider_get(f.world, a)->sensor = true;
    GAME_GameCollider_get(f.world, b)->sensor = true;
    GAME_GameCollider_get(f.world, c)->sensor = true;
    game_physics_set_profiling(f.physics, true);
    assert(step(&f, 0) == 4);
    assert(other(&f, a) == ecs_entity_to_u64(b));
    assert(other(&f, c) == ecs_entity_to_u64(b));
    game_physics_hashes contacts = game_physics_hash(f.physics);
    assert(step(&f, 0) == 0);
    game_physics_hashes persistent = game_physics_hash(f.physics);
    assert(contacts.contact_pairs == persistent.contact_pairs);
    assert(contacts.contact_events != persistent.contact_events);
    assert(step(&f, 1.0f / 60) == 0);
    const game_physics_profile *profile = game_physics_last_profile(f.physics);
    assert(isfinite(profile->box2d.step_ms) && profile->box2d.step_ms > 0);
    assert(isfinite(profile->box2d.collide_ms) && profile->box2d.collide_ms >= 0);
    GAME_GameTransform_get(f.world, a)->x = -20;
    assert(step(&f, 0) == 0); /* Remove the first sorted pair; keep the second. */
    game_physics_hashes separated = game_physics_hash(f.physics);
    assert(contacts.body_state != separated.body_state);
    assert(contacts.contact_pairs != separated.contact_pairs);
    GAME_GameTransform_get(f.world, c)->x = 40;
    assert(step(&f, 0) == 0);
    GAME_GameTransform_get(f.world, a)->x = 0;
    GAME_GameTransform_get(f.world, c)->x = 12;
    assert(step(&f, 0) == 4);
    assert(
        game_physics_last_profile(f.physics)->bodies ==
        3 + (large_world ? PARALLEL_PADDING : 0)
    );
    assert(game_physics_last_profile(f.physics)->pairs == 2);
    assert(ecs_world_destroy_entity(f.world, b) == ECS_OK);
    ecs_entity replacement = body(&f, 6, 0, GAME_BODY_KINEMATIC, 1, 1);
    assert(replacement.slot == b.slot && replacement.generation != b.generation);
    GAME_GameCollider_get(f.world, replacement)->sensor = true;
    assert(step(&f, 0) == 4); /* Reused slots must generate new contact events. */
    assert(other(&f, a) == ecs_entity_to_u64(replacement));
    assert(other(&f, c) == ecs_entity_to_u64(replacement));
    game_physics_set_profiling(f.physics, false);
    assert(step(&f, 0) == 0);
    assert(!game_physics_last_profile(f.physics)->bodies);
    assert(game_physics_last_profile(f.physics)->box2d.step_ms == 0);
    dispose(&f);
}

static void compound_contacts()
{
    /* A noncombat fixture: a notched slab, a round probe and a wide bar. */
    fixture f = create();
    assert(ecs_world_register_component(f.world, &GAME_COMPONENT_GameDestructible, NULL) == ECS_OK);
    ecs_entity slab = body(&f, 0, 0, GAME_BODY_STATIC, 8, 1);
    *GAME_GameCollider_get(f.world, slab) = (GameCollider){ .shape = GAME_COLLIDER_BOX,
                                                            .width = 32,
                                                            .height = 16,
                                                            .category = 8,
                                                            .mask = 1 };
    GameDestructible material = { .cell_size = 1, .min_fragment_cells = 1 };
    assert(
        ecs_world_record_add(f.world, slab, GAME_COMPONENT_GameDestructible.id, &material, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(!game_materials_prepare(f.materials));
    /* This cut leaves the bottom bridge intact; both shoulders belong to the
     * same entity but occupy different Box2D polygons. */
    game_materials_damage(f.materials, slab, (fluid_vec){ 0, 7 }, 7, 1, 0, (material_impact){ 0 });
    assert(!game_materials_prepare(f.materials));
    const material_mask *mask = game_materials_mask(f.materials, slab);
    assert(mask && material_mask_collision(mask).count >= 3);
    assert(!material_mask_contains(mask, 0, 5));
    assert(material_mask_contains(mask, 10, 5));
    assert(game_materials_inspect(f.materials).solids == 1);

    ecs_entity probe = body(&f, 0, 5, GAME_BODY_KINEMATIC, 1, 8);
    GAME_GameCollider_get(f.world, probe)->radius = 1;
    GAME_GameCollider_get(f.world, probe)->sensor = true;
    ecs_entity bar = body(&f, 0, 5, GAME_BODY_KINEMATIC, 1, 8);
    *GAME_GameCollider_get(f.world, bar) = (GameCollider){ .shape = GAME_COLLIDER_BOX,
                                                           .width = 28,
                                                           .height = 2,
                                                           .category = 1,
                                                           .mask = 8,
                                                           .sensor = true };
    game_physics_set_profiling(f.physics, true);
    assert(step(&f, 0) == 2); /* Two shoulders generate one entity pair. */
    assert(game_physics_last_profile(f.physics)->pairs >= 1);
    assert(other(&f, probe) == 0);
    assert(other(&f, bar) == ecs_entity_to_u64(slab));

    GAME_GameTransform_get(f.world, probe)->x = 10;
    assert(step(&f, 1.0f / 60) == 2); /* Earlier polygon misses must not hide this hit. */
    assert(game_physics_last_profile(f.physics)->pairs >= 2);
    assert(other(&f, probe) == ecs_entity_to_u64(slab));
    assert(step(&f, 1.0f / 60) == 0);
    GAME_GameTransform_get(f.world, probe)->x = 0;
    GAME_GameTransform_get(f.world, bar)->y = 30;
    assert(step(&f, 0) == 0);
    assert(game_physics_last_profile(f.physics)->pairs == 0);
    GAME_GameTransform_get(f.world, probe)->x = 10;
    GAME_GameTransform_get(f.world, bar)->y = 5;
    assert(step(&f, 0) == 4);
    assert(game_physics_last_profile(f.physics)->pairs >= 2);
    /* Polygon traversal and worker collection must not change event order. */
    ecs_entity order[] = { slab, probe, slab, bar };
    for (int i = 0; i < 4; i++) {
        assert(ecs_entity_to_u64(contact_owner(&f, i)) == ecs_entity_to_u64(order[i]));
    }
    /* Reuse solved contacts for a solid dynamic bar spanning both shoulders.
     * Repeated drive into the slab must preserve one begin event per entity,
     * even when several polygon manifolds report the same body pair. */
    game_physics_reset(f.physics);
    GAME_GameTransform_get(f.world, probe)->x = 0;
    GAME_GameCollider_get(f.world, bar)->sensor = false;
    GAME_GameBody_get(f.world, bar)->type = GAME_BODY_DYNAMIC;
    int contacts = 0;
    for (int i = 0; i < 30; i++) {
        *GAME_GameVelocity_get(f.world, bar) = (GameVelocity){ 0, -20 };
        contacts += step(&f, 1.0f / 60);
        assert(game_physics_last_profile(f.physics)->pairs >= 1);
    }
    assert(contacts == 2);
    assert(other(&f, bar) == ecs_entity_to_u64(slab));
    assert(fabsf(GAME_GameTransform_get_const(f.world, bar)->x) < 1);
    assert(GAME_GameTransform_get_const(f.world, bar)->y < 10);
    dispose(&f);
}

static ecs_entity mask_body(fixture *f, int type, bool sensor_shape)
{
    assert(ecs_world_register_component(f->world, &GAME_COMPONENT_GameDestructible, NULL) == ECS_OK);
    ecs_entity slab = body(f, 0, 0, type, 8, 1 | 4);
    *GAME_GameCollider_get(f->world, slab) = (GameCollider){ .shape = GAME_COLLIDER_BOX,
                                                             .width = 32,
                                                             .height = 16,
                                                             .category = 8,
                                                             .mask = 1 | 4,
                                                             .sensor = sensor_shape };
    GameDestructible material = { .cell_size = 1, .min_fragment_cells = 1 };
    assert(
        ecs_world_record_add(f->world, slab, GAME_COMPONENT_GameDestructible.id, &material, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    return slab;
}

static void retained_mask_contacts()
{
    fixture f = create();
    ecs_entity slab = mask_body(&f, GAME_BODY_STATIC, false);
    assert(!game_materials_prepare(f.materials));
    game_materials_damage(f.materials, slab, (fluid_vec){ 0, 7 }, 7, 1, 0, (material_impact){ 0 });
    assert(!game_materials_prepare(f.materials));
    ecs_entity probe = body(&f, 10, 5, GAME_BODY_KINEMATIC, 1, 8);
    GAME_GameCollider_get(f.world, probe)->radius = 1;
    GAME_GameCollider_get(f.world, probe)->sensor = true;
    game_physics_set_profiling(f.physics, true);
    assert(step(&f, 0) == 2);
    material_collision_view before = material_mask_collision(
        game_materials_mask(f.materials, slab)
    );
    material_rect *old = NULL;
    for (int i = 0; i < before.count; i++) {
        apush(old, before.rects[i]);
    }
    assert(acount(old) >= 3);
    /* Change one shoulder while the other shoulder's contact stays live. */
    game_materials_damage(f.materials, slab, (fluid_vec){ -10, 7 }, 3, 1, 0, (material_impact){ 0 });
    assert(!game_materials_prepare(f.materials));
    material_collision_view after = material_mask_collision(
        game_materials_mask(f.materials, slab)
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
    const game_physics_profile *work = game_physics_last_profile(f.physics);
    assert(!work->body_creations);
    assert(work->shape_creations == (uint64_t)(after.count - matched));
    assert(work->shape_removals == (uint64_t)(acount(old) - matched));
    assert(work->shape_updates == (uint64_t)updated);
    assert(work->mass_updates == 1);
    afree(old);
    GAME_GameTransform_get(f.world, probe)->x = -10;
    GAME_GameTransform_get(f.world, probe)->y = 7;
    assert(step(&f, 0) == 0);
    GAME_GameTransform_get(f.world, probe)->x = 10;
    GAME_GameTransform_get(f.world, probe)->y = 5;
    assert(step(&f, 0) == 2);
    /* A continuous sensor enters the carved opening. Stale shape geometry or
     * old swept-target bounds must not stop it at the slab's original top. */
    ecs_entity shot = sensor(&f, 0, 20);
    GAME_GameCollider_get(f.world, shot)->radius = .25f;
    *GAME_GameVelocity_get(f.world, shot) = (GameVelocity){ 0, -960 };
    assert(step(&f, 1.0f / 60) == 0);
    assert(fabsf(GAME_GameTransform_get_const(f.world, shot)->y - 4) < .01f);
    dispose(&f);
}

static void empty_mask_shapes()
{
    for (int initially_empty = 0; initially_empty < 2; initially_empty++) {
        fixture f = create();
        ecs_entity slab = mask_body(&f, GAME_BODY_STATIC, false);
        assert(!game_materials_prepare(f.materials));
        /* Seed the adapter edge case directly through the borrowed mask: normal
         * gameplay retires empty sources, but an empty mask must never regain
         * analytic geometry. */
        material_mask *mask = (material_mask *)game_materials_mask(f.materials, slab);
        if (initially_empty) {
            assert(material_mask_carve(mask, 0, 0, 100) == 512);
        }
        ecs_entity probe = body(&f, 0, 0, GAME_BODY_KINEMATIC, 1, 8);
        GAME_GameCollider_get(f.world, probe)->sensor = true;
        game_physics_set_profiling(f.physics, true);
        assert(step(&f, 0) == (initially_empty ? 0 : 2));
        if (!initially_empty) {
            assert(material_mask_carve(mask, 0, 0, 100) == 512);
            assert(step(&f, 0) == 0);
            const game_physics_profile *work = game_physics_last_profile(f.physics);
            assert(!work->body_creations && !work->shape_creations && !work->shape_updates);
            assert(work->shape_removals == 1 && work->mass_updates == 1);
        }
        assert(!mask->count && !material_mask_collision(mask).count);
        ecs_entity shot = sensor(&f, -100, 0);
        *GAME_GameVelocity_get(f.world, shot) = (GameVelocity){ 12000, 0 };
        assert(step(&f, 1.0f / 60) == 0);
        assert(fabsf(GAME_GameTransform_get_const(f.world, shot)->x - 100) < .01f);
        assert(!game_physics_last_profile(f.physics)->pairs);
        dispose(&f);
    }
}

static void retained_mask_motion()
{
    for (int type = GAME_BODY_STATIC; type <= GAME_BODY_KINEMATIC; type++) {
        fixture f = create();
        ecs_entity slab = mask_body(&f, type, type == GAME_BODY_KINEMATIC);
        GameBody *settings = GAME_GameBody_get(f.world, slab);
        settings->continuous = true;
        settings->angular_velocity = type == GAME_BODY_KINEMATIC ? 1.2f : 0;
        *GAME_GameVelocity_get(f.world, slab) = (GameVelocity){ 10, -4 };
        game_physics_set_profiling(f.physics, true);
        assert(step(&f, 0) == 0);
        const game_physics_profile *work = game_physics_last_profile(f.physics);
        assert(work->body_creations == (uint64_t)(1 + (large_world ? PARALLEL_PADDING : 0)));
        /* Symmetric edge removal keeps the actual polygon centroid at zero,
         * permitting an in-place width change without stale CCD centroid data. */
        game_materials_damage(f.materials, slab, (fluid_vec){ -100, 0 }, 85, 1, 0, (material_impact){ 0 });
        game_materials_damage(f.materials, slab, (fluid_vec){ 100, 0 }, 85, 1, 0, (material_impact){ 0 });
        assert(!game_materials_prepare(f.materials));
        material_collision_view collision = material_mask_collision(
            game_materials_mask(f.materials, slab)
        );
        assert(collision.count == 1);
        assert(collision.rects[0].x == 0 && collision.rects[0].y == 0);
        assert(collision.rects[0].width == 30 && collision.rects[0].height == 16);
        GAME_GameOrientation_get(f.world, slab)->radians = type == GAME_BODY_KINEMATIC
                                                             ? 0
                                                             : .3f;
        assert(step(&f, 0) == 0);
        work = game_physics_last_profile(f.physics);
        assert(!work->body_creations && !work->shape_creations && !work->shape_removals);
        assert(work->shape_updates == 1 && work->mass_updates == 1);
        assert(
            GAME_GameVelocity_get_const(f.world, slab)->x ==
            (type == GAME_BODY_STATIC ? 0 : 10)
        );
        assert(
            GAME_GameVelocity_get_const(f.world, slab)->y ==
            (type == GAME_BODY_STATIC ? 0 : -4)
        );
        assert(step(&f, 0) == 0);
        work = game_physics_last_profile(f.physics);
        assert(
            !work->body_creations && !work->shape_creations &&
            !work->shape_updates && !work->shape_removals && !work->mass_updates
        );
        assert(step(&f, 1.0f / 60) == 0);
        if (type != GAME_BODY_STATIC) {
            assert(GAME_GameTransform_get_const(f.world, slab)->x > .1f);
            assert(GAME_GameTransform_get_const(f.world, slab)->y < -.04f);
        }
        if (type == GAME_BODY_KINEMATIC) {
            assert(fabsf(GAME_GameOrientation_get_const(f.world, slab)->radians - .02f) < .001f);
        }
        /* Authoring changes retain their old rebuild contract, unlike cuts. */
        GAME_GameCollider_get(f.world, slab)->friction = .5f;
        assert(step(&f, 0) == 0);
        work = game_physics_last_profile(f.physics);
        assert(
            work->body_creations == 1 && work->shape_creations == 1 &&
            work->shape_removals == 1
        );
        assert(work->mass_updates == 1);
        unsigned slot = slab.slot;
        assert(ecs_world_record_destroy(f.world, slab, 0) == ECS_OK);
        assert(ecs_world_apply_commands(f.world) == ECS_OK);
        ecs_entity replacement = body(&f, 0, 0, GAME_BODY_STATIC, 8, 1);
        assert(
            replacement.slot == slot &&
            ecs_entity_to_u64(replacement) != ecs_entity_to_u64(slab)
        );
        assert(step(&f, 0) == 0);
        work = game_physics_last_profile(f.physics);
        assert(
            work->body_creations == 1 && work->shape_creations == 1 &&
            work->shape_removals == 1
        );
        game_physics_reset(f.physics);
        assert(step(&f, 0) == 0);
        assert(
            game_physics_last_profile(f.physics)->body_creations ==
            (uint64_t)(1 + (large_world ? PARALLEL_PADDING : 0))
        );
        dispose(&f);
    }
}

static game_physics_body_state inspect_body(fixture *f, ecs_entity entity)
{
    game_physics_body_state state;
    assert(game_physics_inspect_body(f->physics, entity, &state));
    return state;
}

static void reset_mask_incarnation()
{
    fixture f = create();
    ecs_entity slab = mask_body(&f, GAME_BODY_STATIC, false);
    game_materials_damage(f.materials, slab, (fluid_vec){ -100, 0 }, 85, 1, 0, (material_impact){ 0 });
    assert(step(&f, 0) == 0);
    unsigned revision = game_materials_mask(f.materials, slab)->revision;
    uint64_t generation = game_materials_mask_generation(f.materials, slab);
    game_materials_reset(f.materials);
    game_materials_damage(f.materials, slab, (fluid_vec){ 100, 0 }, 85, 1, 0, (material_impact){ 0 });
    assert(!game_materials_prepare(f.materials));
    assert(game_materials_mask(f.materials, slab)->revision == revision);
    assert(game_materials_mask_generation(f.materials, slab) != generation);
    game_physics_set_profiling(f.physics, true);
    assert(step(&f, 0) == 0);
    const game_physics_profile *work = game_physics_last_profile(f.physics);
    assert(!work->body_creations && work->shape_creations == 1 && work->shape_removals == 1);
    assert(work->mass_updates == 1);
    ecs_entity probe = sensor(&f, 15.6f, 0);
    GAME_GameCollider_get(f.world, probe)->radius = .1f;
    assert(step(&f, 0) == 0);
    GAME_GameTransform_get(f.world, probe)->x = -15.6f;
    assert(step(&f, 0) == 2);
    dispose(&f);
}

typedef struct
{
    GameTransform target, projectile;
    GameVelocity target_velocity, projectile_velocity;
    game_physics_body_state mass;
} impact_result;

static impact_result mask_impact(bool cut, bool fresh)
{
    fixture f = create();
    ecs_entity slab = mask_body(&f, GAME_BODY_DYNAMIC, false);
    GAME_GameCollider_get(f.world, slab)->restitution = 1;
    ecs_entity projectile = body(&f, 60, 0, GAME_BODY_DYNAMIC, 1, 8);
    GAME_GameCollider_get(f.world, projectile)->restitution = 1;
    assert(step(&f, 0) == 0);
    game_physics_body_state original = inspect_body(&f, slab);
    if (cut) {
        game_materials_damage(f.materials, slab, (fluid_vec){ -100, 0 }, 100, 1, 0, (material_impact){ 0 });
        assert(!game_materials_prepare(f.materials));
        if (fresh) {
            game_physics_reset(f.physics);
        }
        assert(step(&f, 0) == 0);
    }
    game_physics_body_state mass = inspect_body(&f, slab);
    assert(mass.inertia == 0); /* Dynamic bodies deliberately have fixed rotation. */
    if (cut) {
        assert(mass.mass < original.mass * .6f);
        assert(mass.local_center.x > 7 && fabsf(mass.local_center.y) < .001f);
    }
    *GAME_GameVelocity_get(f.world, projectile) = (GameVelocity){ -240, 0 };
    for (int i = 0; i < 24; i++) {
        step(&f, 1.0f / 60);
    }
    impact_result result = { *GAME_GameTransform_get(f.world, slab),
                             *GAME_GameTransform_get(f.world, projectile),
                             *GAME_GameVelocity_get(f.world, slab),
                             *GAME_GameVelocity_get(f.world, projectile),
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
    ecs_entity slab = mask_body(&f, GAME_BODY_STATIC, false);
    assert(step(&f, 0) == 0);
    game_materials_damage(f.materials, slab, (fluid_vec){ -100, 0 }, 100, 1, 0, (material_impact){ 0 });
    assert(step(&f, 0) == 0);
    ecs_entity projectile = body(&f, 60, 0, GAME_BODY_DYNAMIC, 1, 8);
    GAME_GameCollider_get(f.world, projectile)->radius = 1;
    GAME_GameBody_get(f.world, projectile)->continuous = true;
    *GAME_GameVelocity_get(f.world, projectile) = (GameVelocity){ -6000, 0 };
    assert(step(&f, 1.0f / 60) == 2);
    float x = GAME_GameTransform_get(f.world, projectile)->x;
    assert(x > 16 && x < 20);
    dispose(&f);
}

static void retained_mask_wakes_support()
{
    fixture f = create();
    ecs_entity slab = mask_body(&f, GAME_BODY_STATIC, false);
    ecs_entity actor = body(&f, 0, -10, GAME_BODY_DYNAMIC, 1, 8);
    GAME_GameCollider_get(f.world, actor)->radius = 2;
    for (int i = 0; i < 120; i++) {
        step(&f, 1.0f / 60);
    }
    assert(!inspect_body(&f, actor).awake);
    game_materials_damage(f.materials, slab, (fluid_vec){ 0, 0 }, 12, 1, 0, (material_impact){ 0 });
    step(&f, 0);
    assert(inspect_body(&f, actor).awake);
    /* Gravity is authored motion in this runtime. Check wake before supplying
     * downward velocity so the velocity setter cannot hide a failed wake. */
    *GAME_GameVelocity_get(f.world, actor) = (GameVelocity){ 0, 120 };
    for (int i = 0; i < 12; i++) {
        step(&f, 1.0f / 60);
    }
    assert(GAME_GameTransform_get(f.world, actor)->y > 10);
    dispose(&f);
}

static void separated_solved_contacts()
{
    fixture f = create();
    ecs_entity actor = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 2);
    ecs_entity target = body(&f, 8.1f, 0, GAME_BODY_STATIC, 2, 1);
    game_physics_set_profiling(f.physics, true);
    assert(step(&f, 0) == 0); /* Zero delta requires actual overlap. */
    assert(game_physics_last_profile(f.physics)->pairs == 0);
    assert(step(&f, 1.0f / 60) == 2);
    assert(game_physics_last_profile(f.physics)->pairs >= 1);
    float separation = GAME_GameTransform_get_const(f.world, target)->x -
                       GAME_GameTransform_get_const(f.world, actor)->x;
    assert(separation > 8); /* The event comes from a solved positive-gap manifold. */
    assert(other(&f, actor) == ecs_entity_to_u64(target));
    for (int i = 0; i < 10; i++) {
        assert(step(&f, 1.0f / 60) == 0);
        assert(game_physics_last_profile(f.physics)->pairs >= 1);
    }
    assert(step(&f, 0) == 0); /* Do not reuse the solver manifold on a resync. */
    assert(game_physics_last_profile(f.physics)->pairs == 0);
    GAME_GameTransform_get(f.world, actor)->x = .2f;
    assert(step(&f, 0) == 2);
    assert(game_physics_last_profile(f.physics)->pairs >= 1);
    dispose(&f);
}

/* Nearest positions are world placements, which a parent's Orientation turns,
 * so a behavior that writes Orientation cannot run beside the query. */
static void nearest_reads_world_placement()
{
    const ecs_component_id components[] = { GAME_COMPONENT_GameTransform.id,
                                            GAME_COMPONENT_GameOrientation.id,
                                            GAME_COMPONENT_GameCollider.id };
    for (int i = 0; i < 3; i++) {
        bool read = false;
        for (const uint64_t *const *ref = GAME_ENTITY_NEAREST_ACCESS.read_refs; *ref;
             ref++) {
            read |= **ref == components[i];
        }
        assert(read);
    }
}

static void nearest_and_filters()
{
    fixture f = create();
    ecs_entity owner = body(&f, 0, 0, GAME_BODY_STATIC, 2, 4);
    ecs_entity far = body(&f, 80, 0, GAME_BODY_STATIC, 2, 4);
    body(&f, 20, 0, GAME_BODY_STATIC, 2, 0);
    ecs_entity near = body(&f, 40, 0, GAME_BODY_STATIC, 2, 4);
    ecs_entity shot = sensor(&f, 0, 0);
    GAME_GameCollider_get(f.world, shot)->ignore_entity = ecs_entity_to_u64(owner);
    *GAME_GameVelocity_get(f.world, shot) = (GameVelocity){ 6000, 0 };
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(near));
    float impact = GAME_GameTransform_get(f.world, shot)->x;
    assert(impact > 33 && impact < 35);
    assert(other(&f, far) == 0);
    assert(step(&f, 1.0f / 60) == 0);
    assert(ecs_world_destroy_entity(f.world, near) == ECS_OK);
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(far));
    assert(ecs_world_destroy_entity(f.world, owner) == ECS_OK);
    ecs_entity replacement = body(&f, 0, 0, GAME_BODY_STATIC, 2, 4);
    assert(ecs_entity_to_u64(replacement) != ecs_entity_to_u64(owner));
    GAME_GameTransform_get(f.world, shot)->x = 0;
    assert(step(&f, 0) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(replacement));
    dispose(&f);
}

static void moving_target_and_box()
{
    fixture f = create();
    ecs_entity target = body(&f, 50, -50, GAME_BODY_KINEMATIC, 2, 4);
    *GAME_GameVelocity_get(f.world, target) = (GameVelocity){ 0, 6000 };
    ecs_entity shot = sensor(&f, 0, 0);
    *GAME_GameVelocity_get(f.world, shot) = (GameVelocity){ 6000, 0 };
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(target));
    float impact = GAME_GameTransform_get(f.world, shot)->x;
    assert(impact > 44 && impact < 48);
    assert(GAME_GameTransform_get(f.world, target)->y > 49);
    /* The same continuous contract applies to box sensors and rotated boxes. */
    game_physics_reset(f.physics);
    *GAME_GameTransform_get(f.world, target) = (GameTransform){ 50, 0 };
    *GAME_GameVelocity_get(f.world, target) = (GameVelocity){ 0 };
    GameCollider *collider = GAME_GameCollider_get(f.world, target);
    collider->shape = GAME_COLLIDER_BOX;
    collider->width = 4;
    collider->height = 40;
    GAME_GameOrientation_get(f.world, target)->radians = .5f;
    *GAME_GameTransform_get(f.world, shot) = (GameTransform){ 0 };
    collider = GAME_GameCollider_get(f.world, shot);
    collider->shape = GAME_COLLIDER_BOX;
    collider->width = 4;
    collider->height = 3;
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(target));
    assert(GAME_GameTransform_get(f.world, shot)->x < 50);
    dispose(&f);
}

static void sweep_tree_lifetime()
{
    fixture f = create();
    ecs_entity target = body(&f, 50, 0, GAME_BODY_STATIC, 2, 4);
    ecs_entity shot = sensor(&f, 0, 0);
    GAME_GameVelocity_get(f.world, shot)->x = 6000;
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(target));

    /* Ordinary queries remain current while the swept index is dormant. */
    GAME_GameBody_get(f.world, shot)->continuous = false;
    GAME_GameTransform_get(f.world, shot)->x = 0;
    GAME_GameTransform_get(f.world, target)->x = 80;
    GAME_GameVelocity_get(f.world, shot)->x = 0;
    assert(step(&f, 0) == 0);
    GAME_GameTransform_get(f.world, shot)->x = 80;
    assert(step(&f, 0) == 2);
    GAME_GameTransform_get(f.world, shot)->x = 0;
    assert(step(&f, 0) == 0);
    assert(ecs_world_destroy_entity(f.world, target) == ECS_OK);
    ecs_entity replacement = body(&f, 60, 0, GAME_BODY_STATIC, 2, 4);
    assert(replacement.slot == target.slot && replacement.generation != target.generation);
    assert(step(&f, 0) == 0);

    GAME_GameBody_get(f.world, shot)->continuous = true;
    GAME_GameVelocity_get(f.world, shot)->x = 6000;
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(replacement));
    float impact = GAME_GameTransform_get(f.world, shot)->x;
    assert(impact > 53 && impact < 55);
    dispose(&f);
}

static void repeated_moving_sweeps()
{
    fixture f = create();
    ecs_entity targets[16];
    for (int i = 0; i < 16; i++) {
        targets[i] = body(&f, 200 + i * 20, 200, GAME_BODY_KINEMATIC, 2, 4);
    }
    ecs_entity shot = sensor(&f, 0, 0);
    for (int round = 0; round < 16; round++) {
        /* Clear contact history and move the whole target population outside
         * its previous bounds, then cross two targets through the shot. */
        *GAME_GameTransform_get(f.world, shot) = (GameTransform){ 0 };
        *GAME_GameVelocity_get(f.world, shot) = (GameVelocity){ 0 };
        for (int i = 0; i < 16; i++) {
            *GAME_GameTransform_get(f.world, targets[i]) = (GameTransform){
                200 + i * 20,
                200 + round * 100
            };
            *GAME_GameVelocity_get(f.world, targets[i]) = (GameVelocity){ 0 };
        }
        assert(step(&f, 0) == 0);
        int a = round, b = (round + 7) % 16;
        *GAME_GameTransform_get(f.world, targets[a]) = (GameTransform){ 50, -50 };
        *GAME_GameTransform_get(f.world, targets[b]) = (GameTransform){ 80, -80 };
        GAME_GameVelocity_get(f.world, targets[a])->y = 6000;
        GAME_GameVelocity_get(f.world, targets[b])->y = 6000;
        GAME_GameVelocity_get(f.world, shot)->x = 6000;
        assert(step(&f, 1.0f / 60) == 2);
        assert(other(&f, shot) == ecs_entity_to_u64(targets[a]));
        float impact = GAME_GameTransform_get(f.world, shot)->x;
        assert(impact > 44 && impact < 48);
    }
    dispose(&f);
}

static void lifecycle_and_validation()
{
    fixture f = create();
    ecs_entity trigger = body(&f, 0, 0, GAME_BODY_STATIC, 2, 1);
    GAME_GameCollider_get(f.world, trigger)->sensor = true;
    ecs_entity actor = body(&f, 3, 0, GAME_BODY_DYNAMIC, 1, 2);
    assert(step(&f, 0) == 2);
    assert(step(&f, 0) == 0);
    GAME_GameCollider_get(f.world, trigger)->ignore_entity = ecs_entity_to_u64(actor);
    assert(step(&f, 0) == 0);
    GAME_GameCollider_get(f.world, trigger)->ignore_entity = 0;
    assert(step(&f, 0) == 2);
    assert(GAME_GameCollider_remove(f.world, trigger, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(step(&f, 0) == 0);
    assert(GAME_GameCollider_add(f.world, trigger, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    *GAME_GameCollider_get(f.world, trigger) = (GameCollider){ .radius = 4,
                                                               .category = 2,
                                                               .mask = 1,
                                                               .sensor = true };
    assert(step(&f, 0) == 2);
    GAME_GameVelocity_get(f.world, actor)->x = NAN;
    assert(game_physics_tick(f.physics, 1.0f / 60) < 0);
    assert(strstr(game_physics_error(f.physics), "entity"));
    GAME_GameVelocity_get(f.world, actor)->x = 0;
    assert(step(&f, 0) == 0);
    game_physics_reset(f.physics);
    assert(step(&f, 0) == 2);
    /* Both ECS commands and host destruction requests retire a collider before
     * their respective flush boundary, without invalidating live handles. */
    assert(ecs_world_record_destroy(f.world, trigger, 0) == ECS_OK);
    assert(ecs_entity_is_alive(f.world, trigger));
    assert(game_entities_is_retiring(f.entities, trigger));
    assert(step(&f, 0) == 0);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    ecs_entity replacement = body(&f, 0, 0, GAME_BODY_STATIC, 2, 1);
    assert(!game_entities_is_retiring(f.entities, replacement));
    assert(step(&f, 0) == 2);
    game_entities_request_destroy(f.entities, replacement);
    assert(step(&f, 0) == 0);
    dispose(&f);
}

static void interpolation()
{
    fixture f = create();
    ecs_entity actor = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 0);
    *GAME_GameVelocity_get(f.world, actor) = (GameVelocity){ 600, 0 };
    assert(step(&f, 1.0f / 60) == 0);
    CF_V2 offset;
    assert(game_physics_interpolation_offset(f.physics, actor, .5f, &offset));
    assert(fabsf(offset.x + 5) < .001f && offset.y == 0);
    assert(fabsf(GAME_GameTransform_get(f.world, actor)->x - 10) < .001f);
    *GAME_GameTransform_get(f.world, actor) = (GameTransform){ 500, 10 };
    assert(game_physics_interpolation_offset(f.physics, actor, .5f, &offset));
    assert(offset.x == 0 && offset.y == 0);
    assert(step(&f, 0) == 0);
    assert(game_physics_interpolation_offset(f.physics, actor, 0, &offset));
    assert(offset.x == 0 && offset.y == 0);
    game_entities_request_destroy(f.entities, actor);
    assert(!game_physics_interpolation_offset(f.physics, actor, .5f, &offset));
    assert(game_entities_flush(f.entities) == 0);
    ecs_entity replacement = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 0);
    assert(!game_physics_interpolation_offset(f.physics, actor, .5f, &offset));
    assert(!game_physics_interpolation_offset(f.physics, replacement, .5f, &offset));
    dispose(&f);
}

static void followers()
{
    fixture f = create();
    ecs_entity parent = body(&f, 100, 0, GAME_BODY_KINEMATIC, 1, 0);
    GAME_GameOrientation_get(f.world, parent)->radians = 1.57079633f;
    ecs_entity child = sensor(&f, 10, 0);
    assert(ecs_world_record_add_pair(f.world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    *GAME_GameVelocity_get(f.world, child) = (GameVelocity){ 600, 0 };
    ecs_entity there = body(&f, 100, 10, GAME_BODY_KINEMATIC, 2, 4);
    ecs_entity origin = body(&f, 10, 0, GAME_BODY_KINEMATIC, 2, 4);
    /* The child collides where its parent places it, ignores its own Velocity
     * and never writes its pose back. */
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, child) == ecs_entity_to_u64(there));
    assert(other(&f, there) == ecs_entity_to_u64(child));
    assert(!other(&f, origin));
    const GameTransform *local = GAME_GameTransform_get_const(f.world, child);
    assert(local->x == 10 && local->y == 0);
    CF_V2 offset;
    assert(!game_physics_interpolation_offset(f.physics, child, .5f, &offset));
    /* A parent without a Transform only groups; its children stay roots. */
    ecs_entity group = ecs_world_create_entity(f.world);
    ecs_entity grouped = body(&f, 0, 40, GAME_BODY_DYNAMIC, 1, 0);
    assert(ecs_world_record_add_pair(f.world, grouped, ECS_CHILD_OF, group, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    *GAME_GameVelocity_get(f.world, grouped) = (GameVelocity){ 60, 0 };
    step(&f, 1.0f / 60);
    assert(fabsf(GAME_GameTransform_get_const(f.world, grouped)->x - 1) < .001f);
    GAME_GameBody_get(f.world, child)->type = GAME_BODY_DYNAMIC;
    assert(game_physics_tick(f.physics, 1.0f / 60) < 0);
    assert(strstr(game_physics_error(f.physics), "root entity"));
    dispose(&f);
}

/* A child of a moving body moves with it within the same step, so it touches
 * what lies where its parent carries it, not only where it started. */
static void followers_move_with_their_body()
{
    fixture f = create();
    ecs_entity parent = body(&f, 0, 200, GAME_BODY_DYNAMIC, 1, 0);
    *GAME_GameVelocity_get(f.world, parent) = (GameVelocity){ 1200, 0 };
    ecs_entity child = sensor(&f, 10, 0);
    assert(ecs_world_record_add_pair(f.world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    /* The child starts over 8..12 and ends over 28..32; the target covers
     * 30..38. */
    ecs_entity target = body(&f, 34, 200, GAME_BODY_KINEMATIC, 2, 4);
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, child) == ecs_entity_to_u64(target));
    assert(fabsf(GAME_GameTransform_get_const(f.world, parent)->x - 20) < .001f);
    dispose(&f);
}

/* A body and the colliders it carries move as one, so they never touch. */
static void followers_ignore_their_anchor()
{
    fixture f = create();
    ecs_entity ship = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 1);
    ecs_entity hull = body(&f, 2, 0, GAME_BODY_KINEMATIC, 1, 1);
    ecs_entity mast = body(&f, -2, 0, GAME_BODY_KINEMATIC, 1, 1);
    assert(ecs_world_record_add_pair(f.world, hull, ECS_CHILD_OF, ship, 0) == ECS_OK);
    assert(ecs_world_record_add_pair(f.world, mast, ECS_CHILD_OF, ship, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    for (int i = 0; i < 4; i++) {
        assert(step(&f, 1.0f / 60) == 0);
    }
    const GameTransform *at = GAME_GameTransform_get_const(f.world, ship);
    assert(at->x == 0 && at->y == 0);
    ecs_entity *found = NULL;
    assert(game_physics_contact_entities(f.world, f.entities, ship, 1, &found) == 0);
    assert(acount(found) == 0);
    assert(game_physics_contact_entities(f.world, f.entities, hull, 1, &found) == 0);
    assert(acount(found) == 0);
    afree(found);
    dispose(&f);
}

/* A collider that joins the body it touches stops pushing it. */
static void followers_stop_touching_the_body_they_join()
{
    fixture f = create();
    ecs_entity ship = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 1);
    ecs_entity dock = ecs_world_create_entity(f.world);
    assert(GAME_GameTransform_add(f.world, dock, 0) == ECS_OK);
    ecs_entity hull = body(&f, 6, 0, GAME_BODY_KINEMATIC, 1, 1);
    assert(ecs_world_record_add_pair(f.world, hull, ECS_CHILD_OF, dock, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    step(&f, 1.0f / 60);
    assert(ecs_world_record_add_pair(f.world, hull, ECS_CHILD_OF, ship, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    float x = GAME_GameTransform_get_const(f.world, ship)->x;
    for (int i = 0; i < 4; i++) {
        *GAME_GameVelocity_get(f.world, ship) = (GameVelocity){ 0 };
        step(&f, 1.0f / 60);
    }
    assert(fabsf(GAME_GameTransform_get_const(f.world, ship)->x - x) < .001f);
    dispose(&f);
}

/* A continuous sensor stops partway through its sweep, after its followers
 * were tested at its end, so it cannot carry a collider. */
static void continuous_sensors_carry_no_colliders()
{
    fixture f = create();
    ecs_entity shot = sensor(&f, 0, 0);
    ecs_entity hitbox = body(&f, 4, 0, GAME_BODY_KINEMATIC, 1, 0);
    assert(ecs_world_record_add_pair(f.world, hitbox, ECS_CHILD_OF, shot, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(game_physics_tick(f.physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    assert(strstr(game_physics_error(f.physics), "cannot carry colliders"));
    dispose(&f);
}

/* A destructible under a placed parent breaks where its parent places it. */
static void followed_material()
{
    fixture f = create();
    assert(ecs_world_register_component(f.world, &GAME_COMPONENT_GameDestructible, NULL) == ECS_OK);
    ecs_entity parent = body(&f, 100, 0, GAME_BODY_KINEMATIC, 1, 0);
    GAME_GameOrientation_get(f.world, parent)->radians = 1.57079633f;
    ecs_entity slab = body(&f, 0, 0, GAME_BODY_STATIC, 8, 1);
    *GAME_GameCollider_get(f.world, slab) = (GameCollider){ .shape = GAME_COLLIDER_BOX,
                                                            .width = 32,
                                                            .height = 16,
                                                            .category = 8,
                                                            .mask = 1 };
    GameDestructible material = { .cell_size = 1, .min_fragment_cells = 1 };
    assert(
        ecs_world_record_add(f.world, slab, GAME_COMPONENT_GameDestructible.id, &material, 0) == ECS_OK
    );
    assert(ecs_world_record_add_pair(f.world, slab, ECS_CHILD_OF, parent, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(!game_materials_prepare(f.materials));
    /* The slab's local (0, 7) sits at world (93, 0) under the turned parent. */
    game_materials_damage(f.materials, slab, (fluid_vec){ 93, 0 }, 7, 1, 0, (material_impact){ 0 });
    assert(!game_materials_prepare(f.materials));
    const material_mask *mask = game_materials_mask(f.materials, slab);
    assert(!material_mask_contains(mask, 0, 5));
    assert(material_mask_contains(mask, 10, 5));
    dispose(&f);
}

static void current_contact_query()
{
    fixture f = create();
    ecs_entity actor = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 2 | 8);
    ecs_entity target = body(&f, 8.32f, 0, GAME_BODY_STATIC, 2, 1);
    ecs_entity *found = NULL;
    assert(game_physics_contact_entities(f.world, f.entities, actor, 2, &found) == 0);
    assert(acount(found) == 1 && ecs_entity_to_u64(found[0]) == ecs_entity_to_u64(target));
    /* Sensor queries require actual geometry overlap, not the solver's gap. */
    GAME_GameCollider_get(f.world, target)->sensor = true;
    assert(game_physics_contact_entities(f.world, f.entities, actor, 2, &found) == 0);
    assert(acount(found) == 0);
    GAME_GameTransform_get(f.world, target)->x = 7;
    assert(game_physics_contact_entities(f.world, f.entities, actor, 2, &found) == 0);
    assert(acount(found) == 1);
    assert(step(&f, 0) == 2);
    /* An authored teleport is visible before physics synchronizes its cache. */
    GAME_GameTransform_get(f.world, target)->x = 100;
    assert(game_physics_contact_entities(f.world, f.entities, actor, 2, &found) == 0);
    assert(acount(found) == 0);
    *GAME_GameTransform_get(f.world, target) = (GameTransform){ 0 };
    *GAME_GameCollider_get(f.world, target) = (GameCollider){ .shape = GAME_COLLIDER_BOX,
                                                              .width = 2,
                                                              .height = 100,
                                                              .category = 8,
                                                              .mask = 1 };
    GAME_GameOrientation_get(f.world, target)->radians = .7f;
    *GAME_GameTransform_get(f.world, actor) = (GameTransform){ 5.32f * cosf(.7f),
                                                               5.32f * sinf(.7f) };
    assert(game_physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 1);
    assert(game_physics_contact_entities(f.world, f.entities, actor, 2, &found) == 0);
    assert(acount(found) == 0);
    GAME_GameCollider_get(f.world, target)->mask = 0;
    assert(game_physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    GAME_GameCollider_get(f.world, target)->mask = 1;
    GAME_GameCollider_get(f.world, actor)->ignore_entity = ecs_entity_to_u64(target);
    assert(game_physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    GAME_GameCollider_get(f.world, actor)->ignore_entity = 0;
    GAME_GameCollider_get(f.world, target)->ignore_entity = ecs_entity_to_u64(actor);
    assert(game_physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    GAME_GameCollider_get(f.world, target)->ignore_entity = 0;
    game_entities_request_destroy(f.entities, target);
    assert(game_physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    assert(game_entities_flush(f.entities) == 0);
    ecs_entity replacement = body(&f, 0, 0, GAME_BODY_STATIC, 8, 1);
    assert(replacement.slot == target.slot && replacement.generation != target.generation);
    assert(game_physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 1 && ecs_entity_to_u64(found[0]) == ecs_entity_to_u64(replacement));
    assert(ecs_world_record_destroy(f.world, replacement, 0) == ECS_OK);
    assert(game_physics_contact_entities(f.world, f.entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    afree(found);
    dispose(&f);
}

static void raycasts()
{
    fixture f = create();
    ecs_entity circle = body(&f, 20, 0, GAME_BODY_STATIC, 8, 0);
    ecs_entity wall = body(&f, 50, 0, GAME_BODY_STATIC, 8, 0);
    GameCollider *box = GAME_GameCollider_get(f.world, wall);
    box->shape = GAME_COLLIDER_BOX;
    box->width = 4;
    box->height = 40;
    game_ray_hit hit;
    assert(
        game_physics_raycast(
            f.world,
            f.entities,
            (CF_V2){ 0 },
            (CF_V2){ 100, 0 },
            8,
            (ecs_entity){ 0 },
            false,
            &hit
        ) == 0
    );
    assert(hit.found && ecs_entity_to_u64(hit.entity) == ecs_entity_to_u64(circle));
    assert(fabsf(hit.distance - 16) < .001f && fabsf(hit.normal.x + 1) < .001f);
    assert(
        game_physics_raycast(f.world, f.entities, (CF_V2){ 0 }, (CF_V2){ 100, 0 }, 8, circle, false, &hit) == 0
    );
    assert(hit.found && fabsf(hit.point.x - 48) < .001f);
    /* A rotated wall and a teleport are visible before any physics step. */
    GAME_GameOrientation_get(f.world, wall)->radians = 3.14159265f / 2;
    assert(
        game_physics_raycast(f.world, f.entities, (CF_V2){ 0 }, (CF_V2){ 100, 0 }, 8, circle, false, &hit) == 0
    );
    assert(hit.found && fabsf(hit.point.x - 30) < .001f);
    GAME_GameTransform_get(f.world, wall)->y = 100;
    GAME_GameCollider_get(f.world, circle)->sensor = true;
    assert(
        game_physics_raycast(
            f.world,
            f.entities,
            (CF_V2){ 0 },
            (CF_V2){ 100, 0 },
            8,
            (ecs_entity){ 0 },
            false,
            &hit
        ) == 0
    );
    assert(!hit.found && hit.distance == 100 && hit.point.x == 100);
    assert(
        game_physics_raycast(
            f.world,
            f.entities,
            (CF_V2){ 20, 0 },
            (CF_V2){ 100, 0 },
            8,
            (ecs_entity){ 0 },
            true,
            &hit
        ) == 0
    );
    assert(hit.found && hit.distance == 0 && hit.normal.x == -1);
    assert(
        game_physics_raycast(
            f.world,
            f.entities,
            (CF_V2){ 20, 0 },
            (CF_V2){ 20, 0 },
            8,
            (ecs_entity){ 0 },
            true,
            &hit
        ) == 0
    );
    assert(hit.found && hit.distance == 0);
    game_entities_request_destroy(f.entities, circle);
    assert(
        game_physics_raycast(
            f.world,
            f.entities,
            (CF_V2){ 0 },
            (CF_V2){ 100, 0 },
            8,
            (ecs_entity){ 0 },
            true,
            &hit
        ) == 0
    );
    assert(!hit.found);
    assert(
        game_physics_raycast(
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
            actors[group][i] = body(&f, group * 40 + i * 6, 0, GAME_BODY_KINEMATIC, 1, 1);
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
        GAME_GameTransform_get(f.world, actors[group][2])->y = 40;
    }
    assert(step(&f, 0) == 0);
    for (int group = 0; group < GROUPS; group++) {
        GAME_GameTransform_get(f.world, actors[group][2])->y = 0;
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
    followers_stop_touching_the_body_they_join();
    continuous_sensors_carry_no_colliders();
    followed_material();
    current_contact_query();
    ordered_contacts();
}

static void configured_motion()
{
    for (int mode = 0; mode < 2; mode++) {
        fixture f = create();
        game_physics_settings settings = game_physics_defaults();
        settings.force_drive = mode;
        settings.sleep = false;
        settings.workers = 1;
        assert(game_physics_configure(f.physics, settings) == 0);
        ecs_entity dynamic = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 0);
        ecs_entity kinematic = body(&f, 0, 20, GAME_BODY_KINEMATIC, 1, 0);
        *GAME_GameVelocity_get(f.world, dynamic) = (GameVelocity){ 60, 0 };
        *GAME_GameVelocity_get(f.world, kinematic) = (GameVelocity){ 60, 0 };
        /* A later invalid body must not accumulate an earlier body's drive
         * force when the author fixes the component and retries the tick. */
        GAME_GameCollider_get(f.world, kinematic)->radius = -1;
        assert(game_physics_tick(f.physics, 1.0f / 60) < 0);
        GAME_GameCollider_get(f.world, kinematic)->radius = 4;
        assert(step(&f, 1.0f / 60) == 0);
        float distance = GAME_GameTransform_get(f.world, dynamic)->x;
        assert(fabsf(distance - (mode ? .625f : 1)) < .001f);
        assert(fabsf(GAME_GameVelocity_get(f.world, dynamic)->x - 60) < .001f);
        assert(fabsf(GAME_GameTransform_get(f.world, kinematic)->x - 1) < .001f);
        game_physics_settings invalid = settings;
        invalid.substeps = 0;
        assert(game_physics_configure(f.physics, invalid) < 0);
        invalid = settings;
        invalid.contact_damping = NAN;
        assert(game_physics_configure(f.physics, invalid) < 0);
        invalid = settings;
        invalid.workers = 8;
        assert(game_physics_configure(f.physics, invalid) < 0);
        assert(game_physics_settings_equal(settings, game_physics_configuration(f.physics)));
        game_physics_reset(f.physics);
        assert(game_physics_settings_equal(settings, game_physics_configuration(f.physics)));
        assert(game_physics_configure(f.physics, invalid) == 0);
        dispose(&f);
    }
    for (int continuous = 0; continuous < 2; continuous++) {
        fixture f = create();
        game_physics_settings settings = game_physics_defaults();
        settings.substeps = 1;
        settings.continuous = continuous;
        assert(game_physics_configure(f.physics, settings) == 0);
        ecs_entity wall = body(&f, 50, 0, GAME_BODY_STATIC, 8, 1);
        *GAME_GameCollider_get(f.world, wall) = (GameCollider){ .shape = GAME_COLLIDER_BOX,
                                                                .width = 2,
                                                                .height = 200,
                                                                .category = 8,
                                                                .mask = 1 };
        ecs_entity actor = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 8);
        GAME_GameBody_get(f.world, actor)->continuous = true;
        *GAME_GameVelocity_get(f.world, actor) = (GameVelocity){ 6000, 0 };
        step(&f, 1.0f / 60);
        float x = GAME_GameTransform_get(f.world, actor)->x;
        assert(continuous ? x < 46 : x > 90);
        dispose(&f);
    }
}

static void authored_bounce_and_drive()
{
    fixture f = create();
    ecs_entity wall = body(&f, 50, 0, GAME_BODY_STATIC, 2, 1);
    *GAME_GameCollider_get(f.world, wall) = (GameCollider){ .shape = GAME_COLLIDER_BOX,
                                                            .width = 2,
                                                            .height = 100,
                                                            .category = 2,
                                                            .mask = 1,
                                                            .restitution = 1 };
    ecs_entity ball = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 2);
    *GAME_GameVelocity_get(f.world, ball) = (GameVelocity){ 240, 0 };
    for (int i = 0; i < 20; i++) {
        assert(step(&f, 1.0f / 60) >= 0);
    }
    assert(GAME_GameVelocity_get(f.world, ball)->x < -200);
    dispose(&f);

    f = create();
    ecs_entity arm = body(&f, 20, 0, GAME_BODY_KINEMATIC, 2, 1);
    *GAME_GameCollider_get(f.world, arm) = (GameCollider){ .shape = GAME_COLLIDER_BOX,
                                                           .width = 48,
                                                           .height = 8,
                                                           .category = 2,
                                                           .mask = 1 };
    GameBody *rotating = GAME_GameBody_get(f.world, arm);
    rotating->angular_velocity = 6;
    ball = body(&f, 34, 10, GAME_BODY_DYNAMIC, 1, 2);
    for (int i = 0; i < 10; i++) {
        assert(step(&f, 1.0f / 60) >= 0);
    }
    assert(fabsf(GAME_GameOrientation_get(f.world, arm)->radians - 1) < .02f);
    assert(GAME_GameVelocity_get(f.world, ball)->y > 30);
    assert(fabsf(GAME_GameTransform_get(f.world, arm)->x - 20) < .01f);
    rotating = GAME_GameBody_get(f.world, arm);
    rotating->angular_velocity = 0;
    float stopped = GAME_GameOrientation_get(f.world, arm)->radians;
    for (int i = 0; i < 10; i++) {
        assert(step(&f, 1.0f / 60) >= 0);
    }
    assert(GAME_GameOrientation_get(f.world, arm)->radians == stopped);
    /* Continuous angles survive either direction through the pi boundary. */
    for (int direction = -1; direction <= 1; direction += 2) {
        GAME_GameOrientation_get(f.world, arm)->radians = direction * 3;
        rotating->angular_velocity = direction * 3.6f;
        for (int i = 0; i < 10; i++) {
            assert(step(&f, 1.0f / 60) >= 0);
        }
        assert(fabsf(GAME_GameOrientation_get(f.world, arm)->radians - direction * 3.6f) < .02f);
    }
    rotating->angular_velocity = NAN;
    assert(game_physics_tick(f.physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    rotating->angular_velocity = 1;
    rotating->type = GAME_BODY_DYNAMIC;
    assert(game_physics_tick(f.physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    rotating->type = GAME_BODY_KINEMATIC;
    assert(GAME_GameOrientation_remove(f.world, arm, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(game_physics_tick(f.physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    dispose(&f);
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
    game_task_pool *pool = game_task_pool_acquire();
    expected_workers = game_task_pool_tasks(pool)->workers;
    large_world = true;
    physics_cases();
    large_world = false;
    game_task_pool_release(pool);
    return true;
}

void test_native_catalog_enable();
void test_tool_physics()
{
    test_native_catalog_enable(); /* Contact payloads copy a CF_V2. */
    RUN_TEST_CASE(physics_task_pool_runs_every_range);
    RUN_TEST_CASE(physics_authored_motion);
    RUN_TEST_CASE(physics_contacts_and_queries_at_each_substep_count);
    RUN_TEST_CASE(physics_parallel_collection_matches_serial);
}
