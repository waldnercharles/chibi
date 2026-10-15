#include "authoring/engine_preview.h"
#include "authoring/preview.h"
#include "authoring/prototype_preview.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "support/array.h"
#include "support/event_fixture.h"
#include "support/xalloc.h"
#include "test/support/code_fixture.h"
#include "test/support/event_fixture.h"
#include "test/support/simulation_fixture.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static behavior_asset *contact_observer()
{
    behavior_asset *asset = behavior_asset_new("Contacts");
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
    assert(other);
    behavior_variable *point = behavior_asset_add_variable(
        asset,
        "Point",
        PIN_STRUCT,
        "CF_V2",
        PIN_CONTAINER_SINGLE,
        NULL,
        "Test"
    );
    assert(point);
    graph *g = asset->graph;
    node *event = graph_add_node(g, registry_node_by_name("Event"));
    assert(graph_set_event_name(g, event, "Contact Begin") == 0);
    pin_spec parameter = { .name = "Other",
                           .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .kind = PIN_DATA };
    assert(graph_add_pin(g, event, 1, &parameter) >= 0);
    pin_spec point_parameter = { .name = "Point",
                                 .type = PIN_STRUCT,
                                 .type_name = "CF_V2",
                                 .kind = PIN_DATA };
    assert(graph_add_pin(g, event, 1, &point_parameter) >= 0);
    node *set_point = behavior_asset_add_set_node(asset, point->id);
    node *set = behavior_asset_add_set_node(asset, other->id);
    node *log = graph_add_node(g, registry_node_by_name("Print String"));
    pin_value text = { .type = PIN_STR, .str = "contact" };
    assert(graph_set_pin_default(g, log, 0, 1, &text) == 0);
    assert(graph_connect_exec(g, event, 0, set_point, 0, 0) == 0);
    assert(graph_connect_data(g, event, 2, set_point, 1) == 0);
    assert(graph_connect_exec(g, set_point, 0, set, 0, 0) == 0);
    assert(graph_connect_data(g, event, 1, set, 1) == 0);
    assert(graph_connect_exec(g, set, 0, log, 0, 0) == 0);
    return asset;
}

static ecs_entity add_body(preview_world *live, unsigned category, unsigned mask)
{
    ecs_entity entity = ecs_world_create_entity(live->runtime->ecs);
    assert(Transform_add(live->runtime->ecs, entity, 0) == ECS_OK);
    assert(Collider_add(live->runtime->ecs, entity, 0) == ECS_OK);
    assert(Body_add(live->runtime->ecs, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(live->runtime->ecs) == ECS_OK);
    *Transform_get(live->runtime->ecs, entity) = (Transform){ 0 };
    *Collider_get(live->runtime->ecs, entity) = (Collider){ .radius = 10,
                                                            .category = category,
                                                            .mask = mask,
                                                            .sensor = true };
    assert(behavior_attach(live->runtime->behaviors, entity, "Contacts") == 0);
    return entity;
}

static void expect_contacts(preview_world *live, int count)
{
    behavior_clear_logs(live->runtime->behaviors);
    assert(physics_tick(live->runtime->physics, 0) == 0);
    assert(behavior_log_count(live->runtime->behaviors) == 0);
    assert(entities_flush(live->runtime->entities) == 0);
    assert(behavior_log_count(live->runtime->behaviors) == 0);
    assert(entities_dispatch_events(live->runtime->entities) >= 0);
    assert(behavior_log_count(live->runtime->behaviors) == count);
}

static void contact_history()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &COMPONENT_Transform, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &COMPONENT_Collider, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &COMPONENT_Body, NULL) == ECS_OK);
    preview_world *live = preview_world_new(world);
    register_damage_event(live);
    assert(
        preview_add_definition(live, preview_definition_new("Contacts", contact_observer())) == 0
    );
    ecs_entity a = add_body(live, 1, 2);
    ecs_entity b = add_body(live, 2, 0);
    expect_contacts(live, 0);
    Collider_get(world, b)->mask = 1;
    expect_contacts(live, 2);
    behavior_instance *observer = behavior_instance(live->runtime->behaviors, a, "Contacts");
    pin_value other;
    assert(
        behavior_instance_get_variable(observer, observer->asset->variables[0].id, &other) == 0
    );
    assert(other.u64 == ecs_entity_to_u64(b));
    expect_contacts(live, 0);
    Transform_get(world, b)->x = 21;
    expect_contacts(live, 0);
    Transform_get(world, b)->x = 20;
    expect_contacts(live, 2);
    physics_reset(live->runtime->physics);
    expect_contacts(live, 2);
    behavior_destroy_entity(live->runtime->behaviors, b);
    ecs_entity replacement = add_body(live, 2, 1);
    assert(ecs_entity_to_u64(replacement) != ecs_entity_to_u64(b));
    expect_contacts(live, 2);
    Collider_get(world, replacement)->mask = 0;
    expect_contacts(live, 0);
    Collider_get(world, replacement)->mask = 1;
    expect_contacts(live, 2);
    behavior_clear_logs(live->runtime->behaviors);
    physics_reset(live->runtime->physics);
    assert(physics_tick(live->runtime->physics, 0) == 0);
    engine *runtime = authoring_engine_new(world, live->runtime);
    assert(engine_set_playing(runtime, false) == 0);
    assert(engine_frame(runtime, 0.0) >= 0);
    assert(behavior_log_count(live->runtime->behaviors) == 0);
    assert(engine_frame(runtime, 0.1) >= 0);
    assert(behavior_log_count(live->runtime->behaviors) == 0);
    assert(engine_set_playing(runtime, true) == 0);
    assert(engine_frame(runtime, 0.2) >= 0);
    assert(engine_frame(runtime, 0.2 + ENGINE_FIXED_SECONDS) >= 0);
    assert(behavior_log_count(live->runtime->behaviors) == 2);
    pin_value point;
    assert(
        behavior_instance_get_variable(observer, observer->asset->variables[1].id, &point) == 0
    );
    assert(point.type == PIN_STRUCT && point.ptr && point.type_name);
    assert(!strcmp(point.type_name, "CF_V2"));
    assert(
        isfinite(((const CF_V2 *)point.ptr)->x) &&
        isfinite(((const CF_V2 *)point.ptr)->y)
    );
    engine_free(runtime);
    preview_world_free(live);
    ecs_world_free(world);
}

#define ORACLE_BODIES 16

static bool oracle_overlap(ecs_world *world, ecs_entity a, ecs_entity b)
{
    const Transform *left = Transform_get_const(world, a);
    const Transform *right = Transform_get_const(world, b);
    const Collider *first = Collider_get_const(world, a);
    const Collider *second = Collider_get_const(world, b);
    if (!(first->category & second->mask) || !(second->category & first->mask) ||
        first->radius < 0 || second->radius < 0) {
        return false;
    }
    double dx = (double)left->x - right->x;
    double dy = (double)left->y - right->y;
    double radius = (double)first->radius + second->radius;
    return isfinite(radius) && dx * dx + dy * dy <= radius * radius;
}

static unsigned random_value(unsigned *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void contact_oracle()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &COMPONENT_Transform, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &COMPONENT_Collider, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &COMPONENT_Body, NULL) == ECS_OK);
    preview_world *live = preview_world_new(world);
    register_damage_event(live);
    assert(
        preview_add_definition(live, preview_definition_new("Contacts", contact_observer())) == 0
    );
    ecs_entity bodies[ORACLE_BODIES];
    for (int i = 0; i < ORACLE_BODIES; i++) {
        bodies[i] = add_body(live, 1, 1);
        assert(i == 0 || ecs_entity_to_u64(bodies[i - 1]) < ecs_entity_to_u64(bodies[i]));
    }
    bool previous[ORACLE_BODIES][ORACLE_BODIES] = { 0 };
    uint64_t observed[ORACLE_BODIES] = { 0 };
    unsigned random = 17;
    for (int phase = 0; phase < 32; phase++) {
        for (int i = 0; i < ORACLE_BODIES; i++) {
            Transform *position = Transform_get(world, bodies[i]);
            position->x = (float)(int)(random_value(&random) % 601) - 300;
            position->y = (float)(int)(random_value(&random) % 601) - 300;
            Collider *collider = Collider_get(world, bodies[i]);
            collider->radius = (float)(random_value(&random) % 150);
            collider->category = 1u << (random_value(&random) % 3);
            collider->mask = random_value(&random) % 8;
            if (phase % 4 == 0) {
                position->x = 0; /* Exercise vertical sweeps. */
            }
        }
        uint64_t expected[ORACLE_BODIES * (ORACLE_BODIES - 1)];
        int count = 0;
        for (int i = 0; i < ORACLE_BODIES; i++) {
            for (int j = i + 1; j < ORACLE_BODIES; j++) {
                bool overlap = oracle_overlap(world, bodies[i], bodies[j]);
                if (overlap && !previous[i][j]) {
                    expected[count++] = ecs_entity_to_u64(bodies[i]);
                    expected[count++] = ecs_entity_to_u64(bodies[j]);
                    observed[i] = ecs_entity_to_u64(bodies[j]);
                    observed[j] = ecs_entity_to_u64(bodies[i]);
                }
                previous[i][j] = overlap;
            }
        }
        behavior_clear_logs(live->runtime->behaviors);
        assert(physics_tick(live->runtime->physics, 0) == 0);
        for (int batch = 0; batch < 4; batch++) {
            assert(entities_dispatch_events(live->runtime->entities) == 0);
        }
        if (behavior_log_count(live->runtime->behaviors) != count) {
            fprintf(
                stderr,
                "contact oracle phase %d: expected %d events, got %d\n",
                phase,
                count,
                behavior_log_count(live->runtime->behaviors)
            );
        }
        assert(behavior_log_count(live->runtime->behaviors) == count);
        for (int i = 0; i < count; i++) {
            behavior_log_info log;
            assert(behavior_log_info_at(live->runtime->behaviors, i, &log) == 0);
            assert(ecs_entity_to_u64(log.entity) == expected[i]);
        }
        for (int i = 0; i < ORACLE_BODIES; i++) {
            behavior_instance
                *instance = behavior_instance(live->runtime->behaviors, bodies[i], "Contacts");
            pin_value other;
            assert(
                behavior_instance_get_variable(
                    instance,
                    instance->asset->variables[0].id,
                    &other
                ) == 0
            );
            assert(other.u64 == observed[i]);
        }
        expect_contacts(live, 0);
    }
    preview_world_free(live);
    ecs_world_free(world);
}

static void invalid_physics()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &COMPONENT_Transform, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &COMPONENT_Collider, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &COMPONENT_Body, NULL) == ECS_OK);
    preview_world *live = preview_world_new(world);
    assert(
        preview_add_definition(live, preview_definition_new("Contacts", contact_observer())) == 0
    );
    ecs_entity entity = add_body(live, 1, 1);
    for (int invalid = 0; invalid < 7; invalid++) {
        Transform *position = Transform_get(world, entity);
        Collider *collider = Collider_get(world, entity);
        switch (invalid) {
            case 0: collider->radius = FLT_MAX; break;
            case 1: position->x = -FLT_MAX; break;
            case 2: collider->radius = INFINITY; break;
            case 3: collider->radius = NAN; break;
            case 4: collider->radius = -1; break;
            case 5: position->x = INFINITY; break;
            case 6: position->y = NAN; break;
        }
        assert(physics_tick(live->runtime->physics, 0) < 0);
        assert(physics_error(live->runtime->physics)[0]);
        *position = (Transform){ 0 };
        collider->radius = 10;
        assert(physics_tick(live->runtime->physics, 0) == 0);
        assert(!physics_error(live->runtime->physics)[0]);
    }
    preview_world_free(live);
    ecs_world_free(world);
}

static char *read_content(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size >= 0);
    rewind(file);
    char *text = xmalloc((size_t)size + 1);
    assert(fread(text, 1, (size_t)size, file) == (size_t)size);
    text[size] = 0;
    fclose(file);
    return text;
}

static prototype *read_prototype(const char *path, const prototype_registry *registry)
{
    char *text = read_content(path);
    char error[256];
    prototype *prototype = prototype_load(text, registry, error, sizeof error);
    free(text);
    assert(prototype);
    return prototype;
}

static behavior_asset *read_behavior(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "content/behaviors/%s.chibi", name);
    char *text = read_content(path);
    behavior_asset *asset = test_project_asset(path, text);
    if (asset) {
        asset = link_test_libraries(asset);
    }
    free(text);
    assert(asset);
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(asset, &report);
    for (int i = 0; i < acount(report.items); i++) {
        fprintf(stderr, "%s: %s\n", path, report.items[i].message);
    }
    assert(errors == 0);
    compile_report_free(&report);
    return asset;
}

static int attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    return behavior_attach(user, entity, name);
}

static ecs_entity spawn(const prototype *prototype, ecs_world *world, behavior_world *behaviors)
{
    ecs_entity entity;
    char error[256];
    assert(
        prototype_spawn(prototype, world, &(prototype_spawn_hooks){ attach, behaviors }, &entity, error, sizeof error) ==
        0
    );
    return entity;
}

static void authored_combat(bool isolated)
{
    prototype_registry registry = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT };
    prototype *enemy = read_prototype("test/fixtures/content/prototypes/Enemy.kdl", &registry);
    prototype *projectile = read_prototype("test/fixtures/content/prototypes/Projectile.kdl", &registry);
    prototype_preview *preview = isolated ? prototype_preview_new(&registry) : NULL;
    register_test_preview_catalog(preview);
    ecs_world *world = isolated ? prototype_preview_world(preview) : ecs_world_new();
    preview_world *live = NULL;
    engine *runtime = NULL;
    if (!isolated) {
        for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
            assert(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
        }
        live = preview_world_new(world);
        register_damage_event(live);
        runtime = authoring_engine_new(world, live->runtime);
        assert(engine_set_playing(runtime, true) == 0);
    }
    const char *names[] = { "HealthBehavior",
                            "ProjectileBehavior",
                            "ProjectileHitBehavior",
                            "EnemyBehavior" };
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        behavior_asset *asset = read_behavior(names[i]);
        if (isolated) {
            assert(prototype_preview_register_behavior(preview, names[i], asset) == 0);
            behavior_asset_free(asset);
        } else {
            assert(preview_add_definition(live, preview_definition_new(names[i], asset)) == 0);
        }
    }
    behavior_world *behaviors = isolated ? prototype_preview_behaviors(preview)
                                         : live->runtime->behaviors;
    ecs_entity target;
    if (isolated) {
        char error[256];
        assert(prototype_preview_launch(preview, enemy, error, sizeof error) == 0);
        target = prototype_preview_entity(preview);
    } else {
        target = spawn(enemy, world, behaviors);
    }
    simulation_fixture clock = simulation_fixture_new(runtime, preview);
    simulation_advance(&clock, 0);
    particles *particles = isolated ? prototype_preview_particles(preview)
                                    : live->runtime->particles;
    assert(particles_count(particles) == 0);
    ecs_entity shot = spawn(projectile, world, behaviors);
    *Transform_get(world, shot) = *Transform_get_const(world, target);
    simulation_frame(&clock, clock.time);
    assert(Health_get_const(world, target)->current == 50);
    assert(ecs_entity_is_alive(world, shot));
    simulation_step(&clock);
    assert(!ecs_entity_is_alive(world, shot));
    assert(particles_count(particles) == 12);
    assert(particles_data(particles)[0].age == 0.0f);
    assert(Health_get_const(world, target)->current == 40);
    simulation_step(&clock);
    assert(Health_get_const(world, target)->current == 40);
    ecs_entity shots[2];
    for (int i = 0; i < 2; i++) {
        shots[i] = spawn(projectile, world, behaviors);
        *Transform_get(world, shots[i]) = *Transform_get_const(world, target);
        Projectile_get(world, shots[i])->damage = 20;
    }
    simulation_step(&clock);
    assert(!ecs_entity_is_alive(world, shots[0]));
    assert(!ecs_entity_is_alive(world, shots[1]));
    assert(!ecs_entity_is_alive(world, target));
    assert(particles_count(particles) == 36);
    if (!isolated) {
        float particle_age = particles_data(particles)[0].age;
        assert(engine_set_playing(runtime, false) == 0);
        simulation_frame(&clock, 10.0);
        assert(particles_count(particles) == 36);
        assert(particles_data(particles)[0].age == particle_age);
        assert(engine_set_playing(runtime, true) == 0);
        simulation_advance(&clock, 10.5);
        assert(particles_count(particles) == 0);
    }
    if (isolated) {
        prototype_preview_stop(preview);
        assert(particles_count(particles) == 0);
    }
    if (isolated) {
        prototype_preview_free(preview);
    } else {
        engine_free(runtime);
        preview_world_free(live);
        ecs_world_free(world);
    }
    prototype_free(projectile);
    prototype_free(enemy);
}

void test_combat()
{
    contact_history();
    contact_oracle();
    invalid_physics();
    authored_combat(false);
    authored_combat(true);
    puts("PASS: contact begin filtering and history; authored projectile damage and death in both preview targets");
}
