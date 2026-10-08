#include "authoring/engine_preview.h"
#include "authoring/language_catalog.h"
#include "authoring/preview.h"
#include "authoring/source_prepare.h"
#include "execution/inspection.h"
#include "host/native_default.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/systems/scheduler.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    int value;
    int *trace;
    int *count;
} trace_entry;
static int record(ecs_world *, double, double, void *user, game_schedule_error *)
{
    trace_entry *entry = user;
    if (entry) {
        entry->trace[(*entry->count)++] = entry->value;
    }
    return 0;
}
static game_schedule_desc descriptor(const char *name, trace_entry *entry)
{
    return (game_schedule_desc){ .name = name,
                                 .phase = SYSTEM_PRE_PHYSICS,
                                 .declared = true,
                                 .deterministic = true,
                                 .parallel_safe = true,
                                 .run = record,
                                 .user = entry };
}
static void dependencies()
{
    ecs_world *world = ecs_world_new();
    game_schedule *s = game_schedule_new(world, NULL);
    uint64_t transform = GAME_COMPONENT_GameTransform.id,
             health = GAME_COMPONENT_GameHealth.id;
    game_schedule_desc a = descriptor("read A", NULL), b = descriptor("read B", NULL);
    a.reads = &transform;
    a.read_count = 1;
    b.reads = &transform;
    b.read_count = 1;
    assert(
        !game_schedule_register(s, &a) && !game_schedule_register(s, &b) &&
        game_schedule_build(s)
    );
    assert(!game_schedule_dependency(s, "read A", "read B"));
    b.writes = &transform;
    b.write_count = 1;
    assert(!game_schedule_register(s, &b) && game_schedule_build(s));
    assert(game_schedule_dependency(s, "read A", "read B"));
    a.writes = &transform;
    a.write_count = 1;
    assert(!game_schedule_register(s, &a) && game_schedule_build(s));
    assert(game_schedule_dependency(s, "read A", "read B"));
    b.read_count = 0;
    b.writes = &health;
    assert(!game_schedule_register(s, &b) && game_schedule_build(s));
    assert(!game_schedule_dependency(s, "read A", "read B"));
    const char *resource = "materials";
    a.resource_writes = &resource;
    a.resource_write_count = 1;
    b.resource_reads = &resource;
    b.resource_read_count = 1;
    assert(
        !game_schedule_register(s, &a) && !game_schedule_register(s, &b) &&
        game_schedule_build(s)
    );
    assert(game_schedule_dependency(s, "read A", "read B"));
    a.resource_write_count = 0;
    a.structural = true;
    assert(!game_schedule_register(s, &a) && game_schedule_build(s));
    assert(game_schedule_dependency(s, "read A", "read B"));
    a.structural = false;
    a.declared = false;
    assert(!game_schedule_register(s, &a) && game_schedule_build(s));
    assert(game_schedule_warning(s) && game_schedule_dependency(s, "read A", "read B"));
    a.declared = true;
    a.phase = SYSTEM_POST_PHYSICS;
    assert(!game_schedule_register(s, &a) && game_schedule_build(s));
    assert(
        !strcmp(game_schedule_at(s, 0), "read B") &&
        game_schedule_dependency(s, "read B", "read A")
    );
    const char *after = "read A";
    b.after = &after;
    b.after_count = 1;
    assert(!game_schedule_register(s, &b) && !game_schedule_build(s));
    assert(strstr(game_schedule_error_message(s), "cycle"));
    after = "missing";
    assert(!game_schedule_register(s, &b) && !game_schedule_build(s));
    assert(strstr(game_schedule_error_message(s), "missing"));
    game_schedule_free(s);
    ecs_world_free(world);
}
static void stable_events()
{
    int trace[16], count = 0;
    trace_entry events[4];
    ecs_world *world = ecs_world_new();
    game_schedule *s = game_schedule_new(world, NULL);
    const char *names[] = { "presentation", "early", "late", "physics" };
    system_phase phases[] = { SYSTEM_PRESENTATION,
                              SYSTEM_PRE_PHYSICS,
                              SYSTEM_PRE_PHYSICS,
                              SYSTEM_POST_PHYSICS };
    for (int i = 0; i < 4; i++) {
        events[i] = (trace_entry){ i, trace, &count };
        game_schedule_desc d = descriptor(names[i], &events[i]);
        d.phase = phases[i];
        d.host_sink = true;
        d.order = i == 2 ? 1 : 0;
        assert(!game_schedule_register(s, &d));
    }
    for (int parallel = 0; parallel < 2; parallel++) {
        game_schedule_set_parallel(s, parallel);
        count = 0;
        for (int phase = 0; phase < SYSTEM_PHASE_COUNT; phase++) {
            assert(!game_schedule_run(s, phase, 0, .01));
        }
        const int expected[] = { 1, 2, 3, 0 };
        assert(count == 4 && !memcmp(trace, expected, sizeof expected));
    }
    game_schedule_free(s);
    ecs_world_free(world);
}
static ecs_world *world_new()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(!ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL));
    }
    return world;
}
static void component(ecs_world *world, ecs_entity entity, const ecs_component_desc *desc, const void *value)
{
    assert(!ecs_world_record_add(world, entity, desc->id, value, 0));
}
static int publish_source(
    game_preview_runtime *live,
    authoring_language_catalog *catalog,
    const char *text,
    bool replace
)
{
    script_lower_bindings bindings = authoring_language_bindings(catalog);
    authoring_source *source = authoring_source_prepare(
        "FreeMotion.chibi",
        text,
        authoring_language_catalog_view(catalog),
        &bindings,
        BEHAVIOR_BYTECODE
    );
    int result = -1;
    if (source->valid) {
        result = replace
                   ? game_source_replace(live->sources, "FreeMotion", source->module, false)
                   : game_source_register(live->sources, "FreeMotion", source->module, NULL);
        if (result) {
            fprintf(stderr, "system source: %s\n", game_source_error(live->sources));
        }
    }
    authoring_source_free(source);
    return result;
}
static void live_system()
{
    ecs_world *world = world_new();
    game_preview_world *live = game_preview_world_new(world);
    engine *runtime = authoring_engine_new(world, live->runtime);
    char *text = text_file_read("test/fixtures/language/content/systems/FreeMotion.chibi");
    assert(text);
    char error[256];
    native_capabilities *native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    assert(native);
    authoring_language_catalog_input input = { .native = native };
    authoring_language_catalog *catalog = authoring_language_catalog_new(&input, error, sizeof error);
    native_capabilities_free(native);
    assert(catalog);
    assert(!publish_source(live->runtime, catalog, text, false));
    free(text);
    ecs_entity binding = ecs_world_create_entity(world),
               marker = ecs_world_create_entity(world);
    component(
        world,
        binding,
        &GAME_COMPONENT_EngineSystem,
        &(EngineSystem){ "FreeMotion", SYSTEM_PRE_PHYSICS, 0 }
    );
    component(world, marker, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(world, marker, &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 12, 0 });
    component(world, marker, &GAME_COMPONENT_GameAppearance, &(GameAppearance){ 0 });
    assert(!ecs_world_apply_commands(world));
    assert(!engine_frame(runtime, 0));
    for (int i = 1; i <= 60; i++) {
        int result = engine_frame(runtime, i / 60.);
        if (result) {
            fprintf(stderr, "system live: %s\n", engine_error(runtime));
        }
        assert(!result);
    }
    float observed_x = GAME_GameTransform_get_const(world, marker)->x;
    if (fabsf(observed_x - 12) >= .001f) {
        execution_program *program = game_source_program(live->runtime->sources, "FreeMotion");
        execution_effect_summary effects = execution_module_effects(
            execution_program_module(program)
        );
        fprintf(
            stderr,
            "live_system x=%g velocity=%g owners=%d now=%g delta=%g "
            "declared=%d structural=%d host_sink=%d world_read=%d "
            "deterministic=%d parallel=%d\n",
            observed_x,
            GAME_GameVelocity_get_const(world, marker)->x,
            execution_program_owner_count(program),
            live->runtime->now,
            live->runtime->delta,
            effects.declared,
            effects.structural,
            effects.host_sink,
            effects.world_read,
            effects.deterministic,
            effects.parallel_safe
        );
    }
    assert(fabsf(observed_x - 12) < .001f);
    assert(game_schedule_count(engine_system_schedule(runtime)) == 1);
    assert(!publish_source(
        live->runtime,
        catalog,
        "system FreeMotion(position:Transform, velocity:Velocity, "
        "appearance:Appearance, health:Health) "
        "without Collider, Body { position += velocity * dt; }",
        true
    ));
    assert(!engine_frame(runtime, 61 / 60.));
    assert(fabsf(GAME_GameTransform_get_const(world, marker)->x - 12) < .001f);
    execution_program *applied = game_source_program(live->runtime->sources, "FreeMotion");
    assert(publish_source(live->runtime, catalog, "{}", true) < 0);
    assert(game_source_program(live->runtime->sources, "FreeMotion") == applied);
    assert(!ecs_world_record_remove(world, binding, GAME_COMPONENT_EngineSystem.id, 0));
    assert(!ecs_world_apply_commands(world));
    assert(!engine_frame(runtime, 62 / 60.));
    assert(!game_schedule_count(engine_system_schedule(runtime)));
    authoring_language_catalog_free(catalog);
    engine_free(runtime);
    game_preview_world_free(live);
    ecs_world_free(world);
}
/* Real source queries use independent component banks. Their scheduler rows
 * must carry verified effects, including conflict order and observer barriers. */
static uint64_t publish_system(
    game_preview_runtime *live,
    authoring_language_catalog *catalog,
    const char *name,
    const char *text,
    behavior_backend backend
)
{
    script_lower_bindings bindings = authoring_language_bindings(catalog);
    authoring_source *source = authoring_source_prepare_document(
        name,
        name,
        text,
        authoring_language_catalog_view(catalog),
        &bindings,
        backend
    );
    if (!source->valid) {
        for (int i = 0; i < source->diagnostic_count; i++) {
            fprintf(stderr, "%s: %s\n", name, source->diagnostics[i].message);
        }
    }
    assert(source->valid);
    int status = game_source_registered(live->sources, name)
                   ? game_source_replace(live->sources, name, source->module, false)
                   : game_source_register(live->sources, name, source->module, NULL);
    assert(!status);
    uint64_t hits = 0;
    for (int i = 0; i < source->analysis->symbol_count; i++) {
        const script_symbol *symbol = &source->analysis->symbols[i];
        if (symbol->kind == SCRIPT_SHARED_STATE && !strcmp(symbol->name, "hits")) {
            hits = symbol->id;
        }
    }
    authoring_source_free(source);
    return hits;
}
static int add_query_member(ecs_world *world, double, double, void *user, game_schedule_error *)
{
    ecs_entity *owner = user;
    assert(!ecs_entity_to_u64(*owner));
    *owner = ecs_world_create_entity(world);
    component(world, *owner, &GAME_COMPONENT_GameHealth, &(GameHealth){ .current = 10 });
    return ecs_world_apply_commands(world);
}
static void source_cohorts()
{
    for (int backend_index = 0; backend_index < 2; backend_index++) {
        behavior_backend backend = backend_index ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        ecs_world *world = world_new();
        game_preview_world *live = game_preview_world_new(world);
        char error[256];
        native_capabilities *native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
        assert(native);
        authoring_language_catalog_input input = { .native = native };
        authoring_language_catalog
            *catalog = authoring_language_catalog_new(&input, error, sizeof error);
        native_capabilities_free(native);
        assert(catalog);
        publish_system(live->runtime, catalog, "Motion", "system Motion(position:Transform,velocity:Velocity){position.x+=velocity.x*dt;}", backend);
        publish_system(live->runtime, catalog, "Heal", "system Heal(health:Health){health.current+=1;}", backend);
        publish_system(live->runtime, catalog, "Scale", "system Scale(position:Transform){position.x*=2;}", backend);
        ecs_entity owners[4];
        for (int i = 0; i < 4; i++) {
            owners[i] = ecs_world_create_entity(world);
            component(world, owners[i], &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
            component(world, owners[i], &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 60, 0 });
            component(world, owners[i], &GAME_COMPONENT_GameHealth, &(GameHealth){ .current = 10 });
        }
        const char *names[] = { "Motion", "Heal", "Scale" };
        for (int i = 0; i < 3; i++) {
            ecs_entity binding = ecs_world_create_entity(world);
            component(
                world,
                binding,
                &GAME_COMPONENT_EngineSystem,
                &(EngineSystem){ names[i], SYSTEM_PRE_PHYSICS, i == 2 ? 1 : 0 }
            );
        }
        assert(!ecs_world_apply_commands(world));
        engine *runtime = authoring_engine_new(world, live->runtime);
        assert(runtime);
        assert(!engine_frame(runtime, 0));
        assert(!engine_frame(runtime, 1.0 / 60));
        if (!game_schedule_parallel_batches(engine_system_schedule(runtime))) {
            for (int i = 0; i < 3; i++) {
                execution_program *program = game_source_program(live->runtime->sources, names[i]);
                execution_effect_summary effects = execution_module_effects(
                    execution_program_module(program)
                );
                fprintf(
                    stderr,
                    "source cohort %s backend=%d declared=%d safe=%d "
                    "structural=%d sink=%d world=%d deterministic=%d reads=%d "
                    "writes=%d observed=%d unknown_op=%d unknown_pc=%d "
                    "unknown_cap=%llu unknown_function=%llu\n",
                    names[i],
                    backend,
                    effects.declared,
                    effects.parallel_safe,
                    effects.structural,
                    effects.host_sink,
                    effects.world_read,
                    effects.deterministic,
                    effects.read_count,
                    effects.write_count,
                    execution_program_observed(program),
                    effects.unknown_operation,
                    effects.unknown_instruction,
                    (unsigned long long)effects.unknown_capability,
                    (unsigned long long)effects.unknown_function
                );
                for (int j = 0; j < effects.read_count && j < 16; j++) {
                    fprintf(stderr, " read=%llu", (unsigned long long)effects.reads[j]);
                }
                for (int j = 0; j < effects.write_count && j < 16; j++) {
                    fprintf(stderr, " write=%llu", (unsigned long long)effects.writes[j]);
                }
                fprintf(stderr, "\n");
            }
            const game_schedule *schedule = engine_system_schedule(runtime);
            for (int i = 0; i < game_schedule_count(schedule); i++) {
                fprintf(stderr, "cohort scheduler row=%s\n", game_schedule_at(schedule, i));
            }
            game_task_pool *pool = game_task_pool_acquire();
            fprintf(
                stderr,
                "source cohort workers=%d started=%d batches=%d "
                "engine_error=%s\n",
                game_task_pool_tasks(pool)->workers,
                game_task_pool_started(pool),
                game_schedule_parallel_batches(schedule),
                engine_error(runtime)
            );
            game_task_pool_release(pool);
        }
        assert(game_schedule_parallel_batches(engine_system_schedule(runtime)) > 0);
        for (int i = 0; i < 4; i++) {
            assert(fabsf(GAME_GameTransform_get_const(world, owners[i])->x - 2) < .001f);
            assert(GAME_GameHealth_get_const(world, owners[i])->current == 11);
        }
        int batches = game_schedule_parallel_batches(engine_system_schedule(runtime));
        engine_set_behavior_profiling(runtime, true);
        assert(!engine_frame(runtime, 2.0 / 60));
        assert(game_schedule_parallel_batches(engine_system_schedule(runtime)) == batches);
        assert(game_source_profile_count(live->runtime->sources) > 0);
        engine_set_behavior_profiling(runtime, false);
        execution_program *heal = game_source_program(live->runtime->sources, "Heal");
        uint64_t hits = publish_system(live->runtime, catalog, "Heal", "system Heal(health:Health){shared hits:int=0;health.current+=1;hits+=1;}", backend);
        assert(hits && game_source_program(live->runtime->sources, "Heal") == heal);
        assert(!engine_frame(runtime, 3.0 / 60));
        assert(game_schedule_parallel_batches(engine_system_schedule(runtime)) == batches);
        const pin_value *shared = execution_instance_state(
            execution_program_instance(heal, ecs_entity_to_u64(owners[0])),
            hits
        );
        assert(shared && shared->type == PIN_I32 && shared->i == 4);
        publish_system(live->runtime, catalog, "Heal", "system Heal(health:Health){health.current+=1;}", backend);
        assert(game_source_program(live->runtime->sources, "Heal") == heal);
        assert(!engine_frame(runtime, 4.0 / 60));
        assert(game_schedule_parallel_batches(engine_system_schedule(runtime)) > batches);
        batches = game_schedule_parallel_batches(engine_system_schedule(runtime));
        execution_program *motion = game_source_program(live->runtime->sources, "Motion");
        execution_inspector *inspector = execution_program_inspector(motion);
        assert(inspector);
        assert(!engine_frame(runtime, 5.0 / 60));
        assert(game_schedule_parallel_batches(engine_system_schedule(runtime)) == batches);
        for (int i = 0; i < 4; i++) {
            assert(fabsf(GAME_GameTransform_get_const(world, owners[i])->x - 62) < .001f);
            assert(GAME_GameHealth_get_const(world, owners[i])->current == 15);
        }
        execution_program *previous = game_source_program(live->runtime->sources, "Scale");
        publish_system(live->runtime, catalog, "Scale", "system Scale(position:Transform){position.x*=3;}", backend);
        assert(game_source_program(live->runtime->sources, "Scale") == previous);
        assert(!engine_frame(runtime, 6.0 / 60));
        for (int i = 0; i < 4; i++) {
            assert(fabsf(GAME_GameTransform_get_const(world, owners[i])->x - 189) < .001f);
            assert(GAME_GameHealth_get_const(world, owners[i])->current == 16);
        }
        execution_inspector_free(inspector);
        /* A fresh program has no attached inspector; isolate the mixed-phase
         * preparation barrier from the earlier instrumentation barrier. */
        assert(!game_source_remove(live->runtime->sources, "Motion"));
        publish_system(live->runtime, catalog, "Motion", "system Motion(position:Transform,velocity:Velocity){position.x+=velocity.x*dt;}", backend);
        ecs_entity added = { 0 };
        game_schedule_desc native_row = { .name = "Add query owner",
                                          .phase = SYSTEM_PRE_PHYSICS,
                                          .order = -1,
                                          .structural = true,
                                          .run = add_query_member,
                                          .user = &added };
        assert(!engine_register_system(runtime, &native_row));
        assert(!engine_frame(runtime, 7.0 / 60));
        assert(
            ecs_entity_to_u64(added) &&
            GAME_GameHealth_get_const(world, added)->current == 11
        );
        engine_free(runtime);
        authoring_language_catalog_free(catalog);
        game_preview_world_free(live);
        ecs_world_free(world);
    }
}
typedef struct
{
    GameTransform **position;
    const GameVelocity **velocity;
    GameHealth **health;
    int count;
    bool motion;
} native_cohort;
static int run_cohort(ecs_world *, double, double delta, void *user, game_schedule_error *)
{
    native_cohort *cohort = user;
    for (int i = 0; i < cohort->count; i++) {
        if (cohort->motion) {
            cohort->position[i]->x += cohort->velocity[i]->x * delta;
            cohort->position[i]->y += cohort->velocity[i]->y * delta;
        } else {
            cohort->health[i]->current++;
        }
    }
    return 0;
}
static uint64_t parallel_world(bool parallel, int count, int frames, double *elapsed)
{
    ecs_world *world = world_new();
    ecs_entity *entities = NULL;
    for (int i = 0; i < count; i++) {
        ecs_entity entity = ecs_world_create_entity(world);
        apush(entities, entity);
        component(world, entity, &GAME_COMPONENT_GameTransform, &(GameTransform){ i * .25f, 0 });
        component(world, entity, &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 12, 6 });
        component(world, entity, &GAME_COMPONENT_GameHealth, &(GameHealth){ i, 100 });
        component(world, entity, &GAME_COMPONENT_GameAppearance, &(GameAppearance){ 0 });
    }
    assert(!ecs_world_apply_commands(world));
    /* Queries are bound before opening the actual parallel executor. Callbacks
     * neither query ECS nor change membership while their pointers are borrowed. */
    native_cohort motion = { .count = count, .motion = true },
                  health = { .count = count };
    motion.position = xmalloc(count * sizeof *motion.position);
    motion.velocity = xmalloc(count * sizeof *motion.velocity);
    health.health = xmalloc(count * sizeof *health.health);
    for (int i = 0; i < count; i++) {
        motion.position[i] = GAME_GameTransform_get(world, entities[i]);
        motion.velocity[i] = GAME_GameVelocity_get_const(world, entities[i]);
        health.health[i] = GAME_GameHealth_get(world, entities[i]);
    }
    game_task_pool *pool = game_task_pool_acquire_workers(2);
    const game_task_system *tasks = game_task_pool_tasks(pool);
    game_schedule *s = game_schedule_new(world, tasks);
    game_schedule_set_parallel(s, parallel);
    uint64_t transform = GAME_COMPONENT_GameTransform.id,
             velocity = GAME_COMPONENT_GameVelocity.id,
             health_id = GAME_COMPONENT_GameHealth.id;
    game_schedule_desc movement = { .name = "motion",
                                    .phase = SYSTEM_PRE_PHYSICS,
                                    .declared = true,
                                    .deterministic = true,
                                    .parallel_safe = true,
                                    .reads = &velocity,
                                    .read_count = 1,
                                    .writes = &transform,
                                    .write_count = 1,
                                    .identity = &motion,
                                    .run = run_cohort,
                                    .user = &motion };
    game_schedule_desc healing = { .name = "health",
                                   .phase = SYSTEM_PRE_PHYSICS,
                                   .declared = true,
                                   .deterministic = true,
                                   .parallel_safe = true,
                                   .writes = &health_id,
                                   .write_count = 1,
                                   .identity = &health,
                                   .run = run_cohort,
                                   .user = &health };
    assert(!game_schedule_register(s, &movement));
    assert(!game_schedule_register(s, &healing));
    movement.name = "duplicate";
    assert(game_schedule_register(s, &movement) < 0);
    assert(game_schedule_build(s));
    assert(!game_schedule_dependency(s, "motion", "health"));
    game_task_pool_begin(pool, parallel);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < frames; i++) {
        int result = game_schedule_run(s, SYSTEM_PRE_PHYSICS, i / 60., 1 / 60.);
        if (result) {
            fprintf(stderr, "parallel graph: %s\n", game_schedule_error_message(s));
        }
        assert(!result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    *elapsed = 1000. * (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e6;
    assert(game_schedule_parallel_batches(s) == (parallel ? frames : 0));
    assert(!parallel || tasks->workers == 1 || game_task_pool_started(pool));
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int i = 0; i < count; i++) {
        const GameTransform *p = GAME_GameTransform_get_const(world, entities[i]);
        const GameHealth *h = GAME_GameHealth_get_const(world, entities[i]);
        assert(h->current == i + frames && fabsf(p->x - (i * .25f + frames * .2f)) < .05f);
        const unsigned char *bytes = (const unsigned char *)p;
        for (size_t j = 0; j < sizeof *p; j++) {
            hash = (hash ^ bytes[j]) * UINT64_C(1099511628211);
        }
        hash = (hash ^ (unsigned)h->current) * UINT64_C(1099511628211);
    }
    native_cohort second = health;
    healing.name = "health second";
    healing.identity = &second;
    healing.user = &second;
    assert(!game_schedule_register(s, &healing) && game_schedule_build(s));
    assert(game_schedule_dependency(s, "health", "health second"));
    assert(!game_schedule_run(s, SYSTEM_PRE_PHYSICS, frames / 60., 1 / 60.));
    for (int i = 0; i < count; i++) {
        assert(health.health[i]->current == i + frames + 2);
    }
    game_schedule_free(s);
    free(motion.position);
    free(motion.velocity);
    free(health.health);
    game_task_pool_end(pool);
    game_task_pool_release(pool);
    afree(entities);
    ecs_world_free(world);
    return hash;
}
int bench_system_schedule()
{
    for (int run = 0; run < 5; run++) {
        double serial, parallel;
        uint64_t a = parallel_world(false, 4096, 120, &serial),
                 b = parallel_world(true, 4096, 120, &parallel);
        assert(a == b);
        printf(
            "scheduler %d: serial %.3f ms, parallel %.3f ms, hash %016llx\n",
            run,
            serial,
            parallel,
            (unsigned long long)a
        );
    }
    return 0;
}
TEST_CASE(native_schedule_preserves_dependencies_and_event_order)
{
    dependencies();
    stable_events();
    return true;
}
TEST_CASE(source_schedule_preserves_query_replacement_and_removal)
{
    live_system();
    return true;
}
TEST_CASE(native_schedule_parallel_cohorts_match_serial_outcomes)
{
    double elapsed;
    uint64_t serial = parallel_world(false, 128, 20, &elapsed),
             parallel = parallel_world(true, 128, 20, &elapsed);
    REQUIRE(serial == parallel);
    return true;
}
TEST_CASE(source_schedule_verifies_effects_conflicts_and_observer_barriers)
{
    source_cohorts();
    return true;
}
void test_system_schedule()
{
    RUN_TEST_CASE(native_schedule_preserves_dependencies_and_event_order);
    RUN_TEST_CASE(source_schedule_preserves_query_replacement_and_removal);
    RUN_TEST_CASE(native_schedule_parallel_cohorts_match_serial_outcomes);
    RUN_TEST_CASE(source_schedule_verifies_effects_conflicts_and_observer_barriers);
}
