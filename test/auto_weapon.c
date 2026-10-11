#include "authoring/engine_preview.h"
#include "authoring/preview.h"
#include "authoring/prototype_prepare.h"
#include "authoring/prototype_preview.h"
#include "execution/inspection.h"
#include "language/identity.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "support/array.h"
#include "support/event_fixture.h"
#include "support/xalloc.h"
#include "test/support/code_fixture.h"
#include "test/support/event_fixture.h"
#include "test/support/simulation_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_content(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = xmalloc((size_t)size + 1);
    assert(fread(text, 1, (size_t)size, file) == (size_t)size);
    text[size] = 0;
    fclose(file);
    return text;
}

typedef struct
{
    uint64_t elapsed, module, function, occurrence;
} weapon_debug;

static void expect_elapsed(execution_instance *instance, uint64_t id, float expected)
{
    const pin_value *value = execution_instance_state(instance, id);
    assert(value && value->type == PIN_F32 && fabsf(value->f - expected) < 0.001f);
}

static bool find_point(const execution_module *module, uint64_t occurrence, weapon_debug *debug, int depth)
{
    assert(depth < 16);
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info f = execution_module_function_at(module, i);
        for (int j = 0; j < behavior_function_source_point_count(f.body); j++) {
            behavior_source_point point = behavior_function_source_point_at(f.body, j);
            if (point.occurrence == occurrence && !point.completion) {
                debug->module = execution_module_id(module);
                debug->function = f.id;
                debug->occurrence = occurrence;
                return true;
            }
        }
    }
    for (int i = 0; i < execution_module_import_count(module); i++) {
        if (find_point(execution_module_import_at(module, i), occurrence, debug, depth + 1)) {
            return true;
        }
    }
    return false;
}

static const char *behavior_root(const char *name)
{
    return !strcmp(name, "AutoWeaponBehavior") || !strcmp(name, "InputBehavior") ||
                   !strcmp(name, "MovementTrailBehavior") ||
                   !strcmp(name, "WeaponBehavior")
             ? "test/fixtures/content/behaviors"
             : "content/behaviors";
}

static void set_elapsed(execution_instance *instance, uint64_t id, float elapsed)
{
    const pin_value *old = execution_instance_state(instance, id);
    assert(old && old->type == PIN_F32);
    pin_value value = *old;
    value.f = elapsed;
    assert(execution_instance_set_state(instance, id, value) == 0);
}

static void register_behavior(preview_runtime *runtime, const char *name, weapon_debug *debug)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s.chibi", behavior_root(name), name);
    char *text = read_content(path);
    test_source_fixture fixture = test_source_prepare(path, text);
    free(text);
    if (!strcmp(name, "AutoWeaponBehavior")) {
        const script_analysis *analysis = fixture.source->analysis;
        for (int i = 0; i < analysis->symbol_count; i++) {
            if (analysis->symbols[i].kind == SCRIPT_STATE &&
                !strcmp(analysis->symbols[i].name, "Elapsed")) {
                debug->elapsed = analysis->symbols[i].id;
            }
        }
        const script_catalog *catalog = authoring_sources_catalog(fixture.sources);
        uint64_t occurrence = 0;
        uint64_t interval = 0;
        for (int i = 0; i < authoring_sources_count(fixture.sources); i++) {
            const authoring_source *source = authoring_sources_at(fixture.sources, i);
            if (strcmp(source->analysis->name, "WeaponActions")) {
                continue;
            }
            for (int j = 0; j < source->analysis->symbol_count; j++) {
                const script_symbol *symbol = &source->analysis->symbols[j];
                if (symbol->kind == SCRIPT_FUNCTION && symbol->declaration.start >= 0 &&
                    !strcmp(symbol->name, "Check Firing Interval")) {
                    interval = symbol->id;
                }
            }
            for (int j = 0; j < source->analysis->expression_count; j++) {
                const script_expression *e = &source->analysis->expressions[j];
                const script_catalog_capability
                    *cap = script_catalog_capability_by_id(catalog, e->capability);
                if (e->kind == SCRIPT_EXPR_CALL && e->awaited && cap &&
                    !strcmp(cap->name, "SetEntity_Transform")) {
                    assert(!occurrence);
                    occurrence = script_record_id(source->analysis, SCRIPT_RECORD_EXPRESSION, j);
                }
            }
        }
        int calls = 0;
        for (int j = 0; j < analysis->expression_count; j++) {
            const script_expression *e = &analysis->expressions[j];
            calls += e->kind == SCRIPT_EXPR_CALL && e->capability == interval;
        }
        assert(interval && calls == 1);
        assert(
            debug->elapsed && occurrence &&
            find_point(fixture.source->module, occurrence, debug, 0)
        );
    }
    test_source_register(runtime, &fixture);
    test_source_free(&fixture);
}

static int attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    preview_runtime *runtime = user;
    return source_attach(
        runtime->sources,
        entity,
        name,
        runtime->deferred,
        runtime->now,
        runtime->delta
    );
}

static ecs_entity target_at(ecs_world *world, float x, float y)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(Transform_add(world, entity, 0) == ECS_OK);
    assert(Collider_add(world, entity, 0) == ECS_OK);
    assert(Body_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *Transform_get(world, entity) = (Transform){ x, y };
    *Collider_get(world, entity) = (Collider){ .radius = 12, .category = 2 };
    return entity;
}

static int shots(ecs_world *world, ecs_entity *latest)
{
    ecs_query query;
    ecs_component_id id = COMPONENT_Projectile.id;
    assert(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    ecs_entity entity;
    int count = 0;
    *latest = (ecs_entity){ 0 };
    while (ecs_query_next(&query, &entity)) {
        count++;
        if (ecs_entity_to_u64(entity) > ecs_entity_to_u64(*latest)) {
            *latest = entity;
        }
    }
    ecs_query_end(&query);
    return count;
}

static prototype *read_prototype(const char *name, const prototype_registry *registry)
{
    char path[256], error[256];
    snprintf(path, sizeof path, "test/fixtures/content/prototypes/%s.kdl", name);
    char *text = read_content(path);
    prototype *prototype = prototype_load(text, registry, error, sizeof error);
    free(text);
    assert(prototype);
    return prototype;
}

static void expect_shot(ecs_world *world, int count, float x, float y, int damage)
{
    ecs_entity shot;
    assert(shots(world, &shot) == count);
    const Transform *position = Transform_get_const(world, shot);
    const Projectile *projectile = Projectile_get_const(world, shot);
    assert(position && projectile);
    assert(fabsf(position->x) < 0.001f && fabsf(position->y) < 0.001f);
    const Velocity *velocity = Velocity_get_const(world, shot);
    const Orientation *orientation = Orientation_get_const(world, shot);
    assert(velocity && velocity->x == 0 && velocity->y == 0);
    assert(orientation && fabsf(orientation->radians - atan2f(y, x)) < 0.001f);
    assert(projectile->damage == damage && projectile->speed == 0 && projectile->lifetime == 100);
}

void test_auto_weapon()
{
    prototype_registry registry = { .components = REGISTRY_COMPONENTS,
                                    .component_count = REGISTRY_COMPONENT_COUNT };
    prototype *player = read_prototype("Player", &registry);
    prototype *projectile = read_prototype("Projectile", &registry);
    assert(
        prototype_write_field(projectile, "Projectile", "speed", (pin_value){ .type = PIN_F32, .f = 0 }) == 0
    );
    assert(
        prototype_write_field(
            projectile,
            "Projectile",
            "lifetime",
            (pin_value){ .type = PIN_F32, .f = 100 }
        ) == 0
    );
    for (int isolated = 0; isolated < 2; isolated++) {
        prototype_preview *preview = isolated ? prototype_preview_new(&registry) : NULL;
        register_test_preview_catalog(preview);
        ecs_world *world = preview ? prototype_preview_world(preview)
                                   : ecs_world_new();
        preview_world *live = NULL;
        engine *runtime = NULL;
        if (!isolated) {
            for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
                assert(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
            }
            live = preview_world_new(world);
            register_damage_event(live);
            runtime = authoring_engine_new(world, live->runtime);
        }
        const char *names[] = {
            "InputBehavior",         "WeaponBehavior",
            "AutoWeaponBehavior",    "HealthBehavior",
            "ProjectileBehavior",    "ProjectileHitBehavior",
            "PlayerContactBehavior", "MovementTrailBehavior"
        };
        weapon_debug debug = { 0 };
        preview_runtime *host = isolated ? prototype_preview_live(preview)->runtime
                                         : live->runtime;
        for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
            register_behavior(host, names[i], &debug);
        }
        ecs_entity owner;
        char error[256];
        if (isolated) {
            assert(prototype_preview_register_prototype(preview, projectile) == 0);
            assert(prototype_preview_launch(preview, player, error, sizeof error) == 0);
            owner = prototype_preview_entity(preview);
        } else {
            assert(entities_register(live->runtime->entities, projectile) == 0);
            assert(
                prototype_spawn(player, world, &(prototype_spawn_hooks){ attach, host }, &owner, error, sizeof error) ==
                0
            );
            assert(engine_set_playing(runtime, true) == 0);
        }
        execution_program *program = source_program(host->sources, "AutoWeaponBehavior");
        execution_instance *instance = execution_program_instance(program, ecs_entity_to_u64(owner));
        assert(instance);
        execution_inspector *inspector = execution_program_inspector(program);
        assert(
            inspector &&
            execution_inspector_select_owner(inspector, ecs_entity_to_u64(owner))
        );
        *Transform_get(world, owner) = (Transform){ 0 };
        Weapon_get(world, owner)->damage = 17;
        ecs_entity latest;
        simulation_fixture clock = simulation_fixture_new(runtime, preview);
        simulation_advance(&clock, 0);
        simulation_advance(&clock, 0.5);
        assert(shots(world, &latest) == 0);
        ecs_entity target = target_at(world, 100, 0);
        simulation_advance(&clock, 0.6);
        assert(shots(world, &latest) == 0);
        simulation_advance(&clock, 0.75);
        expect_shot(world, 1, 1, 0, 17);
        simulation_advance(&clock, 0.9);
        assert(shots(world, &latest) == 1);
        expect_elapsed(instance, debug.elapsed, 0.15f);
        simulation_advance(&clock, 1.1);
        expect_shot(world, 2, 1, 0, 17);
        expect_elapsed(instance, debug.elapsed, 0.10f);
        simulation_advance(&clock, 1.25);
        expect_shot(world, 3, 1, 0, 17);
        expect_elapsed(instance, debug.elapsed, 0);
        *Transform_get(world, target) = (Transform){ -100, 0 };
        simulation_advance(&clock, 1.5);
        expect_shot(world, 4, -1, 0, 17);
        *Transform_get(world, target) = (Transform){ 30, 40 };
        simulation_advance(&clock, 1.75);
        expect_shot(world, 5, 0.6f, 0.8f, 17);
        *Transform_get(world, target) = (Transform){ 0 };
        simulation_advance(&clock, 2.0);
        expect_shot(world, 6, 0, 0, 17);
        assert(ecs_world_destroy_entity(world, target) == ECS_OK);
        simulation_advance(&clock, 2.5);
        assert(shots(world, &latest) == 6);
        target = target_at(world, 100, 0);
        assert(execution_inspector_breakpoint(
            inspector,
            debug.module,
            debug.function,
            debug.occurrence,
            true
        ));
        simulation_advance(&clock, 2.75);
        assert(shots(world, &latest) == 7 && execution_instance_pending(instance) == 1);
        assert(execution_program_paused(program));
        Weapon_get(world, owner)->damage = 999;
        assert(ecs_world_destroy_entity(world, target) == ECS_OK);
        *Transform_get(world, owner) = (Transform){ 1000, 1000 };
        assert(execution_inspector_control(inspector, EXECUTION_DEBUG_RESUME) == 0);
        simulation_advance(&clock, 2.75);
        expect_shot(world, 7, 1, 0, 17);
        assert(execution_inspector_breakpoint(
            inspector,
            debug.module,
            debug.function,
            debug.occurrence,
            false
        ));
        *Transform_get(world, owner) = (Transform){ 0 };
        target_at(world, -100, 0);
        Weapon_get(world, owner)->cooldown = 0;
        simulation_step(&clock);
        expect_shot(world, 8, -1, 0, 999);
        expect_elapsed(instance, debug.elapsed, (float)ENGINE_FIXED_SECONDS - 0.01f);
        simulation_frame(&clock, clock.time);
        assert(shots(world, &latest) == 8);
        Weapon_get(world, owner)->cooldown = -1;
        simulation_step(&clock);
        expect_shot(world, 10, -1, 0, 999);
        expect_elapsed(instance, debug.elapsed, (float)(2 * ENGINE_FIXED_SECONDS) - 0.03f);
        Weapon_get(world, owner)->cooldown = 0.25f;
        simulation_advance(&clock, clock.time + 0.5);
        expect_shot(world, 12, -1, 0, 999);
        expect_elapsed(instance, debug.elapsed, (float)(2 * ENGINE_FIXED_SECONDS) - 0.03f);

        /* Seed two intervals to exercise one activation across two suspended
         * projectile initializers; display frame length cannot create backlog. */
        set_elapsed(instance, debug.elapsed, 0.5f);
        assert(execution_inspector_breakpoint(
            inspector,
            debug.module,
            debug.function,
            debug.occurrence,
            true
        ));
        simulation_step(&clock);
        assert(shots(world, &latest) == 13 && execution_instance_pending(instance) == 1);
        assert(execution_program_paused(program));
        double held_time = engine_last_timing(clock.runtime)->simulation_time;
        Weapon_get(world, owner)->damage = 777;
        *Transform_get(world, owner) = (Transform){ 1000, 1000 };
        assert(execution_inspector_control(inspector, EXECUTION_DEBUG_RESUME) >= 0);
        expect_shot(world, 13, -1, 0, 999);
        simulation_frame(&clock, clock.time);
        assert(shots(world, &latest) == 14 && execution_program_paused(program));
        assert(execution_inspector_control(inspector, EXECUTION_DEBUG_RESUME) >= 0);
        simulation_frame(&clock, clock.time);
        expect_shot(world, 14, -1, 0, 999);
        expect_elapsed(instance, debug.elapsed, (float)ENGINE_FIXED_SECONDS);
        assert(engine_last_timing(clock.runtime)->simulation_time == held_time);
        assert(execution_inspector_breakpoint(
            inspector,
            debug.module,
            debug.function,
            debug.occurrence,
            false
        ));
        *Transform_get(world, owner) = (Transform){ 0 };
        simulation_advance(&clock, clock.time + 0.25);
        expect_shot(world, 15, -1, 0, 777);
        expect_elapsed(instance, debug.elapsed, (float)ENGINE_FIXED_SECONDS);
        execution_inspector_free(inspector);
        if (isolated) {
            prototype_preview_free(preview);
            continue;
        }
        engine_free(runtime);
        preview_world_free(live);
        ecs_world_free(world);
    }
    prototype_free(player);
    prototype_free(projectile);
    puts("auto weapon PASS: authored target aiming, cooldown remainder, projectile defaults and held initialization in both hosts");
}
