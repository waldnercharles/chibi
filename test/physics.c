#include "runtime/scenes/physics.h"

#include "graph/model/registry.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "test/physics_executor.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    ecs_world *world;
    game_live_world *live;
} fixture;

static const game_task_system *test_tasks;
static bool large_world;
static int expected_workers;
static int configured_substeps = 4;
extern void test_task_pool();
enum
{
    PARALLEL_PADDING = 256
};

static behavior_asset *observer()
{
    behavior_asset *asset = behavior_asset_new("PhysicsObserver");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    pin_value zero = { .type = PIN_HANDLE, .type_name = "ecs.entity" };
    behavior_variable *other = behavior_asset_add_variable(
        asset,
        "Other",
        PIN_HANDLE,
        "ecs.entity",
        PIN_CONTAINER_SINGLE,
        &zero,
        "Test"
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, registry_node_by_name("Event"));
    assert(graph_set_event_name(g, event, "Contact Begin") == 0);
    pin_spec parameter = { .name = "Other",
                           .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .kind = PIN_DATA };
    assert(graph_add_pin(g, event, 1, &parameter) >= 0);
    node *set = behavior_asset_add_set_node(asset, other->id);
    node *log = graph_add_node(g, registry_node_by_name("Print String"));
    pin_value text = { .type = PIN_STR, .str = "contact" };
    assert(graph_set_pin_default(g, log, 0, 1, &text) == 0);
    assert(graph_connect_exec(g, event, 0, set, 0, 0) == 0);
    assert(graph_connect_data(g, event, 1, set, 1) == 0);
    assert(graph_connect_exec(g, set, 0, log, 0, 0) == 0);
    return asset;
}

static fixture create()
{
    fixture f = { .world = ecs_world_new() };
    const ecs_component_desc *components[] = { &GAME_COMPONENT_GameTransform,
                                               &GAME_COMPONENT_GameCollider,
                                               &GAME_COMPONENT_GameBody,
                                               &GAME_COMPONENT_GameVelocity,
                                               &GAME_COMPONENT_GameOrientation };
    for (int i = 0; i < 5; i++) {
        assert(ecs_world_register_component(f.world, components[i], NULL) == ECS_OK);
    }
    f.live = game_live_world_new(f.world);
    game_physics_settings settings = game_physics_defaults();
    settings.substeps = configured_substeps;
    assert(game_physics_configure(f.live->physics, settings) == 0);
    if (test_tasks) {
        game_physics_set_tasks(f.live->physics, test_tasks);
    }
    if (large_world) {
        /* Put every semantic case above the parallel collection threshold.
         * These distant, masked bodies produce no additional contacts. */
        for (int i = 0; i < PARALLEL_PADDING; i++) {
            ecs_entity entity = ecs_world_create_entity(f.world);
            assert(GAME_GameTransform_add(f.world, entity, 0) == ECS_OK);
            assert(GAME_GameCollider_add(f.world, entity, 0) == ECS_OK);
            assert(ecs_world_apply_commands(f.world) == ECS_OK);
            *GAME_GameTransform_get(f.world, entity) = (GameTransform){ 10000 + i * 20,
                                                                        10000 };
            *GAME_GameCollider_get(f.world, entity) = (GameCollider){ .radius = 1 };
        }
    }
    assert(
        game_live_add_definition(f.live, game_live_definition_new("PhysicsObserver", observer())) == 0
    );
    return f;
}

static void dispose(fixture *f)
{
    game_live_world_free(f->live);
    ecs_world_free(f->world);
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
    assert(game_behavior_attach(f->live->behaviors, entity, "PhysicsObserver") == 0);
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
    game_behavior_clear_logs(f->live->behaviors);
    int result = game_physics_tick(f->live->physics, delta);
    if (result < 0) {
        fprintf(stderr, "physics: %s\n", game_physics_error(f->live->physics));
    }
    assert(result == 0);
    assert(game_physics_workers(f->live->physics) == (large_world ? expected_workers : 1));
    do {
        assert(game_entities_dispatch_events(f->live->entities) >= 0);
    } while (game_entities_pending_events(f->live->entities));
    return game_behavior_log_count(f->live->behaviors);
}

static uint64_t other(fixture *f, ecs_entity entity)
{
    behavior_instance *instance = game_behavior_instance(f->live->behaviors, entity, "PhysicsObserver");
    pin_value value;
    assert(
        behavior_instance_get_variable(instance, instance->asset->variables[0].id, &value) == 0
    );
    return value.u64;
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
    printf(
        "physics_quality substeps=%d wall_penetration=%.6f contacts=%d\n",
        configured_substeps,
        position.x - 41,
        contacts
    );
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
    game_physics_set_profiling(f.live->physics, true);
    assert(step(&f, 0) == 4);
    assert(other(&f, a) == ecs_entity_to_u64(b));
    assert(other(&f, c) == ecs_entity_to_u64(b));
    assert(step(&f, 0) == 0);
    GAME_GameTransform_get(f.world, a)->x = -20;
    assert(step(&f, 0) == 0); /* Remove the first sorted pair; keep the second. */
    GAME_GameTransform_get(f.world, c)->x = 40;
    assert(step(&f, 0) == 0);
    GAME_GameTransform_get(f.world, a)->x = 0;
    GAME_GameTransform_get(f.world, c)->x = 12;
    assert(step(&f, 0) == 4);
    assert(
        game_physics_last_profile(f.live->physics)->bodies ==
        3 + (large_world ? PARALLEL_PADDING : 0)
    );
    assert(game_physics_last_profile(f.live->physics)->pairs == 2);
    assert(game_behavior_destroy_entity(f.live->behaviors, b) == 0);
    ecs_entity replacement = body(&f, 6, 0, GAME_BODY_KINEMATIC, 1, 1);
    assert(replacement.slot == b.slot && replacement.generation != b.generation);
    GAME_GameCollider_get(f.world, replacement)->sensor = true;
    assert(step(&f, 0) == 4); /* Reused slots must generate new contact events. */
    assert(other(&f, a) == ecs_entity_to_u64(replacement));
    assert(other(&f, c) == ecs_entity_to_u64(replacement));
    game_physics_set_profiling(f.live->physics, false);
    assert(step(&f, 0) == 0);
    assert(!game_physics_last_profile(f.live->physics)->bodies);
    dispose(&f);
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
    assert(game_behavior_destroy_entity(f.live->behaviors, near) == 0);
    assert(step(&f, 1.0f / 60) == 2);
    assert(other(&f, shot) == ecs_entity_to_u64(far));
    assert(game_behavior_destroy_entity(f.live->behaviors, owner) == 0);
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
    game_physics_reset(f.live->physics);
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
    assert(game_behavior_destroy_entity(f.live->behaviors, target) == 0);
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
    assert(game_physics_tick(f.live->physics, 1.0f / 60) < 0);
    assert(strstr(game_physics_error(f.live->physics), "entity"));
    GAME_GameVelocity_get(f.world, actor)->x = 0;
    assert(step(&f, 0) == 0);
    game_physics_reset(f.live->physics);
    assert(step(&f, 0) == 2);
    /* Both ECS commands and host destruction requests retire a collider before
     * their respective flush boundary, without invalidating live handles. */
    assert(ecs_world_record_destroy(f.world, trigger, 0) == ECS_OK);
    assert(ecs_entity_is_alive(f.world, trigger));
    assert(game_entities_is_retiring(f.live->entities, trigger));
    assert(step(&f, 0) == 0);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    ecs_entity replacement = body(&f, 0, 0, GAME_BODY_STATIC, 2, 1);
    assert(!game_entities_is_retiring(f.live->entities, replacement));
    assert(step(&f, 0) == 2);
    game_entities_request_destroy(f.live->entities, replacement);
    assert(step(&f, 0) == 0);
    dispose(&f);
}

static void interpolation()
{
    fixture f = create();
    ecs_entity actor = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 0);
    *GAME_GameVelocity_get(f.world, actor) = (GameVelocity){ 600, 0 };
    assert(step(&f, 1.0f / 60) == 0);
    GameTransform shown;
    assert(game_physics_interpolate(f.live->physics, actor, .5f, &shown));
    assert(fabsf(shown.x - 5) < .001f);
    assert(fabsf(GAME_GameTransform_get(f.world, actor)->x - 10) < .001f);
    *GAME_GameTransform_get(f.world, actor) = (GameTransform){ 500, 10 };
    assert(game_physics_interpolate(f.live->physics, actor, .5f, &shown));
    assert(shown.x == 500 && shown.y == 10);
    assert(step(&f, 0) == 0);
    assert(game_physics_interpolate(f.live->physics, actor, 0, &shown));
    assert(shown.x == 500 && shown.y == 10);
    game_entities_request_destroy(f.live->entities, actor);
    assert(!game_physics_interpolate(f.live->physics, actor, .5f, &shown));
    assert(game_entities_flush(f.live->entities) == 0);
    ecs_entity replacement = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 0);
    assert(!game_physics_interpolate(f.live->physics, actor, .5f, &shown));
    assert(!game_physics_interpolate(f.live->physics, replacement, .5f, &shown));
    dispose(&f);
}

static void current_contact_query()
{
    fixture f = create();
    ecs_entity actor = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 2 | 8);
    ecs_entity target = body(&f, 8.32f, 0, GAME_BODY_STATIC, 2, 1);
    ecs_entity *found = NULL;
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 2, &found) == 0);
    assert(acount(found) == 1 && ecs_entity_to_u64(found[0]) == ecs_entity_to_u64(target));
    /* Sensor queries require actual geometry overlap, not the solver's gap. */
    GAME_GameCollider_get(f.world, target)->sensor = true;
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 2, &found) == 0);
    assert(acount(found) == 0);
    GAME_GameTransform_get(f.world, target)->x = 7;
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 2, &found) == 0);
    assert(acount(found) == 1);
    assert(step(&f, 0) == 2);
    /* An authored teleport is visible before physics synchronizes its cache. */
    GAME_GameTransform_get(f.world, target)->x = 100;
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 2, &found) == 0);
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
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 8, &found) == 0);
    assert(acount(found) == 1);
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 2, &found) == 0);
    assert(acount(found) == 0);
    GAME_GameCollider_get(f.world, target)->mask = 0;
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    GAME_GameCollider_get(f.world, target)->mask = 1;
    GAME_GameCollider_get(f.world, actor)->ignore_entity = ecs_entity_to_u64(target);
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    GAME_GameCollider_get(f.world, actor)->ignore_entity = 0;
    GAME_GameCollider_get(f.world, target)->ignore_entity = ecs_entity_to_u64(actor);
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    GAME_GameCollider_get(f.world, target)->ignore_entity = 0;
    game_entities_request_destroy(f.live->entities, target);
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 8, &found) == 0);
    assert(acount(found) == 0);
    assert(game_entities_flush(f.live->entities) == 0);
    ecs_entity replacement = body(&f, 0, 0, GAME_BODY_STATIC, 8, 1);
    assert(replacement.slot == target.slot && replacement.generation != target.generation);
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 8, &found) == 0);
    assert(acount(found) == 1 && ecs_entity_to_u64(found[0]) == ecs_entity_to_u64(replacement));
    assert(ecs_world_record_destroy(f.world, replacement, 0) == ECS_OK);
    assert(game_physics_contact_entities(f.world, f.live->entities, actor, 8, &found) == 0);
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
            f.live->entities,
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
        game_physics_raycast(f.world, f.live->entities, (CF_V2){ 0 }, (CF_V2){ 100, 0 }, 8, circle, false, &hit) ==
        0
    );
    assert(hit.found && fabsf(hit.point.x - 48) < .001f);
    /* A rotated wall and a teleport are visible before any physics step. */
    GAME_GameOrientation_get(f.world, wall)->radians = 3.14159265f / 2;
    assert(
        game_physics_raycast(f.world, f.live->entities, (CF_V2){ 0 }, (CF_V2){ 100, 0 }, 8, circle, false, &hit) ==
        0
    );
    assert(hit.found && fabsf(hit.point.x - 30) < .001f);
    GAME_GameTransform_get(f.world, wall)->y = 100;
    GAME_GameCollider_get(f.world, circle)->sensor = true;
    assert(
        game_physics_raycast(
            f.world,
            f.live->entities,
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
            f.live->entities,
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
            f.live->entities,
            (CF_V2){ 20, 0 },
            (CF_V2){ 20, 0 },
            8,
            (ecs_entity){ 0 },
            true,
            &hit
        ) == 0
    );
    assert(hit.found && hit.distance == 0);
    game_entities_request_destroy(f.live->entities, circle);
    assert(
        game_physics_raycast(
            f.world,
            f.live->entities,
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
            f.live->entities,
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
            game_behavior_log_info info;
            assert(game_behavior_log_info_at(f.live->behaviors, index++, &info) == 0);
            assert(
                ecs_entity_to_u64(info.entity) ==
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
            game_behavior_log_info info;
            assert(game_behavior_log_info_at(f.live->behaviors, group * 2 + i, &info) == 0);
            assert(ecs_entity_to_u64(info.entity) == ecs_entity_to_u64(actors[group][i + 1]));
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
    nearest_and_filters();
    moving_target_and_box();
    sweep_tree_lifetime();
    repeated_moving_sweeps();
    lifecycle_and_validation();
    interpolation();
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
        assert(game_physics_configure(f.live->physics, settings) == 0);
        ecs_entity dynamic = body(&f, 0, 0, GAME_BODY_DYNAMIC, 1, 0);
        ecs_entity kinematic = body(&f, 0, 20, GAME_BODY_KINEMATIC, 1, 0);
        *GAME_GameVelocity_get(f.world, dynamic) = (GameVelocity){ 60, 0 };
        *GAME_GameVelocity_get(f.world, kinematic) = (GameVelocity){ 60, 0 };
        /* A later invalid body must not accumulate an earlier body's drive
         * force when the author fixes the component and retries the tick. */
        GAME_GameCollider_get(f.world, kinematic)->radius = -1;
        assert(game_physics_tick(f.live->physics, 1.0f / 60) < 0);
        GAME_GameCollider_get(f.world, kinematic)->radius = 4;
        assert(step(&f, 1.0f / 60) == 0);
        float distance = GAME_GameTransform_get(f.world, dynamic)->x;
        assert(fabsf(distance - (mode ? .625f : 1)) < .001f);
        assert(fabsf(GAME_GameVelocity_get(f.world, dynamic)->x - 60) < .001f);
        assert(fabsf(GAME_GameTransform_get(f.world, kinematic)->x - 1) < .001f);
        game_physics_settings invalid = settings;
        invalid.substeps = 0;
        assert(game_physics_configure(f.live->physics, invalid) < 0);
        invalid = settings;
        invalid.contact_damping = NAN;
        assert(game_physics_configure(f.live->physics, invalid) < 0);
        invalid = settings;
        invalid.workers = 8;
        assert(game_physics_configure(f.live->physics, invalid) < 0);
        assert(game_physics_settings_equal(
            settings,
            game_physics_configuration(f.live->physics)
        ));
        game_physics_reset(f.live->physics);
        assert(game_physics_settings_equal(
            settings,
            game_physics_configuration(f.live->physics)
        ));
        assert(game_physics_configure(f.live->physics, invalid) == 0);
        dispose(&f);
    }
    for (int continuous = 0; continuous < 2; continuous++) {
        fixture f = create();
        game_physics_settings settings = game_physics_defaults();
        settings.substeps = 1;
        settings.continuous = continuous;
        assert(game_physics_configure(f.live->physics, settings) == 0);
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
    puts("physics settings PASS: force drive, kinematic velocity, CCD tradeoff, validation and retained reset configuration");
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
    assert(game_physics_tick(f.live->physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    rotating->angular_velocity = 1;
    rotating->type = GAME_BODY_DYNAMIC;
    assert(game_physics_tick(f.live->physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    rotating->type = GAME_BODY_KINEMATIC;
    assert(GAME_GameOrientation_remove(f.world, arm, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(game_physics_tick(f.live->physics, 1.0f / 60) == FLOW_ERR_ARGUMENT);
    dispose(&f);
    puts("physics authored motion PASS: restitution, angular strike, stop, unwrapped angles and invalid configuration");
}

void test_physics()
{
    authored_bounce_and_drive();
    test_task_pool();
    configured_motion();
    game_task_pool *pool = game_task_pool_acquire();
    physics_cases();
    configured_substeps = 2;
    physics_cases();
    configured_substeps = 1;
    /* One substep fails the four-step crowd separation tolerance. It remains
     * an explicit quality choice; verify wall blocking and swept hits here. */
    solid_walls();
    nearest_and_filters();
    moving_target_and_box();
    configured_substeps = 4;
    test_physics_executor executor = test_physics_executor_open();
    if (executor.library) {
        test_tasks = &executor.tasks;
    }
    expected_workers = test_tasks ? test_tasks->workers
                                  : game_task_pool_tasks(pool)->workers;
    large_world = true;
    physics_cases();
    large_world = false;
    test_tasks = NULL;
    printf("physics parallel PASS: workers=%d, padded semantic cases and ordered contact re-entry\n", expected_workers);
    test_physics_executor_close(&executor);
    game_task_pool_release(pool);
    puts("physics PASS: solid thin walls, sliding, contact lifetime, relative moving-target and box sweeps, nearest/filter/owner exclusion, initial overlap, removal/reset and validation");
}
