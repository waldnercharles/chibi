#include "assets/system_document.h"
#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/systems/schedule.h"
#include "support/text_file.h"
#include "test/support/code_fixture.h"

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
static int record(ecs_world *, double, double, void *user, flow_error *)
{
    trace_entry *entry = user;
    if (entry) {
        entry->trace[(*entry->count)++] = entry->value;
    }
    return 0;
}
static engine_system_desc descriptor(const char *name, trace_entry *entry)
{
    return (engine_system_desc){ .name = name,
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
    system_schedule *s = system_schedule_new(world);
    uint64_t transform = GAME_COMPONENT_GameTransform.id,
             health = GAME_COMPONENT_GameHealth.id;
    engine_system_desc a = descriptor("read A", NULL), b = descriptor("read B", NULL);
    a.reads = &transform;
    a.read_count = 1;
    b.reads = &transform;
    b.read_count = 1;
    assert(
        !system_schedule_register(s, &a) && !system_schedule_register(s, &b) &&
        system_schedule_build(s)
    );
    assert(!system_schedule_dependency(s, "read A", "read B"));
    b.writes = &transform;
    b.write_count = 1;
    assert(!system_schedule_register(s, &b) && system_schedule_build(s));
    assert(system_schedule_dependency(s, "read A", "read B"));
    a.writes = &transform;
    a.write_count = 1;
    assert(!system_schedule_register(s, &a) && system_schedule_build(s));
    assert(system_schedule_dependency(s, "read A", "read B"));
    b.read_count = 0;
    b.writes = &health;
    assert(!system_schedule_register(s, &b) && system_schedule_build(s));
    assert(!system_schedule_dependency(s, "read A", "read B"));
    const char *resource = "materials";
    a.resource_writes = &resource;
    a.resource_write_count = 1;
    b.resource_reads = &resource;
    b.resource_read_count = 1;
    assert(
        !system_schedule_register(s, &a) && !system_schedule_register(s, &b) &&
        system_schedule_build(s)
    );
    assert(system_schedule_dependency(s, "read A", "read B"));
    a.resource_write_count = 0;
    a.structural = true;
    assert(!system_schedule_register(s, &a) && system_schedule_build(s));
    assert(system_schedule_dependency(s, "read A", "read B"));
    a.structural = false;
    a.declared = false;
    assert(!system_schedule_register(s, &a) && system_schedule_build(s));
    assert(system_schedule_warning(s) && system_schedule_dependency(s, "read A", "read B"));
    a.declared = true;
    a.phase = SYSTEM_POST_PHYSICS;
    assert(!system_schedule_register(s, &a) && system_schedule_build(s));
    assert(
        !strcmp(system_schedule_at(s, 0), "read B") &&
        system_schedule_dependency(s, "read B", "read A")
    );
    const char *after = "read A";
    b.after = &after;
    b.after_count = 1;
    assert(!system_schedule_register(s, &b) && !system_schedule_build(s));
    assert(strstr(system_schedule_error(s), "cycle"));
    after = "missing";
    assert(!system_schedule_register(s, &b) && !system_schedule_build(s));
    assert(strstr(system_schedule_error(s), "missing"));
    system_schedule_free(s);
    ecs_world_free(world);
}
static void stable_events()
{
    int trace[16], count = 0;
    trace_entry events[4];
    ecs_world *world = ecs_world_new();
    system_schedule *s = system_schedule_new(world);
    const char *names[] = { "presentation", "early", "late", "physics" };
    system_phase phases[] = { SYSTEM_PRESENTATION,
                              SYSTEM_PRE_PHYSICS,
                              SYSTEM_PRE_PHYSICS,
                              SYSTEM_POST_PHYSICS };
    for (int i = 0; i < 4; i++) {
        events[i] = (trace_entry){ i, trace, &count };
        engine_system_desc d = descriptor(names[i], &events[i]);
        d.phase = phases[i];
        d.host_sink = true;
        d.order = i == 2 ? 1 : 0;
        assert(!system_schedule_register(s, &d));
    }
    for (int parallel = 0; parallel < 2; parallel++) {
        system_schedule_set_parallel(s, parallel);
        count = 0;
        for (int phase = 0; phase < SYSTEM_PHASE_COUNT; phase++) {
            assert(!system_schedule_run(s, phase, 0, .01));
        }
        const int expected[] = { 1, 2, 3, 0 };
        assert(count == 4 && !memcmp(trace, expected, sizeof expected));
    }
    system_schedule_free(s);
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
static const ecs_component_desc *resolve(const char *name, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *d = GAME_REGISTRY_COMPONENTS[i];
        if (!strcmp(d->name, name) || !strcmp(d->key, name)) {
            return d;
        }
    }
    return NULL;
}
static const char *component_name(ecs_component_id id, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        if (GAME_REGISTRY_COMPONENTS[i]->id == id) {
            return GAME_REGISTRY_COMPONENTS[i]->name;
        }
    }
    return NULL;
}
static void live_system()
{
    ecs_world *world = world_new();
    game_live_world *live = game_live_world_new(world);
    engine *runtime = engine_new(world, live);
    char *json = text_file_read("projects/arena2d/content/systems/FreeMotion.code");
    assert(json);
    char error[256];
    assert(!game_live_register_system(live, "FreeMotion", json, error, sizeof error));
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
    assert(fabsf(GAME_GameTransform_get_const(world, marker)->x - 12) < .001f);
    assert(system_schedule_count(engine_system_schedule(runtime)) == 1);
    game_system_document *doc = game_system_document_load(json, resolve, NULL, error, sizeof error);
    assert(doc);
    free(json);
    ecs_query_expr *query = ecs_query_expr_has(GAME_COMPONENT_GameHealth.id);
    assert(!game_system_document_set_query(doc, query));
    ecs_query_expr_free(query);
    json = game_system_document_save(doc, resolve, component_name, NULL);
    assert(json);
    assert(!game_live_register_system(live, "FreeMotion", json, error, sizeof error));
    assert(!engine_frame(runtime, 61 / 60.));
    assert(fabsf(GAME_GameTransform_get_const(world, marker)->x - 12) < .001f);
    assert(game_live_register_system(live, "FreeMotion", "{}", error, sizeof error) < 0);
    assert(!ecs_world_record_remove(world, binding, GAME_COMPONENT_EngineSystem.id, 0));
    assert(!ecs_world_apply_commands(world));
    assert(!engine_frame(runtime, 62 / 60.));
    assert(!system_schedule_count(engine_system_schedule(runtime)));
    free(json);
    game_system_document_free(doc);
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(world);
}
static behavior_asset *copy_system(const char *path, const char *name)
{
    char *json = text_file_read(path);
    assert(json);
    behavior_asset *asset = test_project_asset(path, json);
    free(json);
    assert(asset);
    assert(!behavior_asset_set_name(asset, name));
    return asset;
}
static game_system *health_system(ecs_world *world)
{
    behavior_asset *a = behavior_asset_new("health");
    assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    graph *g = a->graph;
    node *tick = graph_add_node(g, registry_node_by_name("Event"));
    assert(!graph_set_event_name(g, tick, "Tick"));
    node *get = graph_add_node(g, registry_node_by_name("Get_GameHealth"));
    assert(graph_expose_output_fields(g, get, 0) == 0);
    node *plus = graph_add_node(g, registry_node_by_name("Add"));
    for (int i = 0; i < 2; i++) {
        assert(!graph_set_pin_type(g, plus, 0, i, PIN_I32, "int"));
    }
    assert(!graph_set_pin_type(g, plus, 1, 0, PIN_I32, "int"));
    assert(!graph_set_pin_default(g, plus, 0, 1, &(pin_value){ .type = PIN_I32, .i = 1 }));
    node *make = graph_add_node(g, registry_node_by_name("Make_GameHealth"));
    node *set = graph_add_node(g, registry_node_by_name("Set_GameHealth"));
    assert(!graph_connect_data(g, get, 1, plus, 0));
    assert(!graph_connect_data(g, plus, 0, make, 0));
    assert(!graph_connect_data(g, get, 2, make, 1));
    assert(!graph_connect_data(g, make, 0, set, 1));
    assert(!graph_connect_exec(g, tick, 0, set, 0, 0));
    game_system *result = game_system_new(world, a);
    assert(result);
    assert(!game_system_compile(result)->error_count);
    return result;
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
    }
    assert(!ecs_world_apply_commands(world));
    game_system *motion = game_system_new(
        world,
        copy_system("projects/arena2d/content/systems/FreeMotion.code", "motion")
    );
    game_system *health = health_system(world);
    assert(motion);
    system_schedule *s = system_schedule_new(world);
    system_schedule_set_parallel(s, parallel);
    assert(!system_schedule_register(s, &(engine_system_desc){ .name = "motion", .graph = motion }));
    assert(!system_schedule_register(s, &(engine_system_desc){ .name = "health", .graph = health }));
    assert(
        system_schedule_register(s, &(engine_system_desc){ .name = "duplicate", .graph = motion }) < 0
    );
    assert(system_schedule_build(s));
    for (int i = 0; i < 2; i++) {
        const compile_report *r = game_system_compile(i ? health : motion);
        if (!r->effects.parallel_eligible) {
            fprintf(
                stderr,
                "effects %d node %llu: %s\n",
                i,
                (unsigned long long)r->effects.blocking_node_id,
                r->effects.blocking_reason
            );
        }
    }
    assert(!system_schedule_dependency(s, "motion", "health"));
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < frames; i++) {
        int result = system_schedule_run(s, SYSTEM_PRE_PHYSICS, i / 60., 1 / 60.);
        if (result) {
            fprintf(stderr, "parallel graph: %s\n", system_schedule_error(s));
        }
        assert(!result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    *elapsed = 1000. * (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e6;
    assert(system_schedule_parallel_batches(s) == (parallel ? frames : 0));
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
    system_schedule_free(s);
    game_system_free(motion);
    game_system_free(health);
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
void test_system_schedule()
{
    dependencies();
    stable_events();
    live_system();
    double elapsed;
    uint64_t serial = parallel_world(false, 128, 20, &elapsed),
             parallel = parallel_world(true, 128, 20, &elapsed);
    assert(serial == parallel);
    puts("PASS: System read/write dependencies, barriers, diagnostics, stable events and live query hot apply");
}
