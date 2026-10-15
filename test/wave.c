#include "authoring/engine_preview.h"
#include "authoring/preview.h"
#include "authoring/prototype_prepare.h"
#include "authoring/prototype_preview.h"
#include "graph/document/serialize.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/support/code_fixture.h"
#include "test/support/event_fixture.h"
#include "test/support/simulation_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path)
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

static prototype *prototype(const char *name, const prototype_registry *registry)
{
    char path[256], error[256];
    snprintf(path, sizeof path, "test/fixtures/content/prototypes/%s.kdl", name);
    char *text = read_file(path);
    prototype *result = prototype_load(text, registry, error, sizeof error);
    free(text);
    assert(result);
    return result;
}

static test_source_fixture behavior(const char *name)
{
    char path[256];
    const char *root = !strcmp(name, "WaveBehavior")
                         ? "test/fixtures/content/behaviors"
                         : "content/behaviors";
    snprintf(path, sizeof path, "%s/%s.chibi", root, name);
    char *text = read_file(path);
    test_source_fixture fixture = test_source_prepare(path, text);
    free(text);
    return fixture;
}

typedef struct
{
    ecs_world *world;
    preview_world *live;
    engine *runtime;
    prototype_preview *preview;
    simulation_fixture simulation;
    uint64_t elapsed, spawned;
} host;

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

static void tick(host *h, double time)
{
    simulation_advance(&h->simulation, time);
}

static int enemies(host *h, ecs_entity *latest)
{
    ecs_component_id component = COMPONENT_Collider.id;
    ecs_query query;
    assert(ecs_query_begin(h->world, &query, &component, 1) == ECS_OK);
    int count = 0;
    ecs_entity entity;
    if (latest) {
        *latest = (ecs_entity){ 0 };
    }
    while (ecs_query_next(&query, &entity)) {
        if (!(Collider_get_const(h->world, entity)->category & 2)) {
            continue;
        }
        count++;
        if (latest && entity.slot > latest->slot) {
            *latest = entity;
        }
    }
    ecs_query_end(&query);
    return count;
}

static void position(host *h, ecs_entity entity, float x, float y)
{
    const Transform *value = Transform_get_const(h->world, entity);
    assert(value && fabsf(value->x - x) < 0.001f && fabsf(value->y - y) < 0.001f);
}

static void run(
    bool isolated,
    const prototype_registry *registry,
    prototype *encounter,
    prototype *player,
    prototype *enemy,
    bool restarted
)
{
    host h = { 0 };
    h.preview = isolated ? prototype_preview_new(registry) : NULL;
    register_test_preview_catalog(h.preview);
    h.world = h.preview ? prototype_preview_world(h.preview) : ecs_world_new();
    if (!isolated) {
        for (int i = 0; i < registry->component_count; i++) {
            assert(ecs_world_register_component(h.world, registry->components[i], NULL) == ECS_OK);
        }
        h.live = preview_world_new(h.world);
        register_damage_event(h.live);
        h.runtime = authoring_engine_new(h.world, h.live->runtime);
    }
    const char *names[] = { "WaveBehavior", "EnemyBehavior", "HealthBehavior" };
    for (int i = 0; i < 3; i++) {
        test_source_fixture fixture = behavior(names[i]);
        if (!strcmp(names[i], "WaveBehavior")) {
            for (int j = 0; j < fixture.source->analysis->symbol_count; j++) {
                const script_symbol *symbol = &fixture.source->analysis->symbols[j];
                if (symbol->kind != SCRIPT_STATE) {
                    continue;
                }
                if (!strcmp(symbol->name, "Elapsed")) {
                    h.elapsed = symbol->id;
                }
                if (!strcmp(symbol->name, "Spawned")) {
                    h.spawned = symbol->id;
                }
            }
            assert(h.elapsed && h.spawned);
        }
        test_source_register(
            h.preview ? prototype_preview_live(h.preview)->runtime : h.live->runtime,
            &fixture
        );
        test_source_free(&fixture);
    }
    if (h.preview) {
        assert(prototype_preview_register_prototype(h.preview, enemy) == 0);
    } else {
        assert(entities_register(h.live->runtime->entities, enemy) == 0);
    }
    char error[256];
    for (int launch = 0; launch < 2; launch++) {
        ecs_entity owner;
        if (h.preview) {
            assert(prototype_preview_launch(h.preview, encounter, error, sizeof error) == 0);
            owner = prototype_preview_entity(h.preview);
        } else {
            assert(
                prototype_spawn(
                    encounter,
                    h.world,
                    &(prototype_spawn_hooks){ attach, h.live->runtime },
                    &owner,
                    error,
                    sizeof error
                ) == 0
            );
            assert(engine_set_playing(h.runtime, true) == 0);
        }
        h.simulation = simulation_fixture_new(h.runtime, h.preview);
        tick(&h, 0);
        tick(&h, 10);
        assert(enemies(&h, NULL) == 0);
        ecs_entity target;
        assert(prototype_spawn_components(player, h.world, &target, error, sizeof error) == 0);
        *Transform_get(h.world, target) = (Transform){ 100, 25 };
        tick(&h, 11.49);
        assert(enemies(&h, NULL) == 0);
        /* Float-authored elapsed time crosses 1.5 on tick 91. */
        tick(&h, 11.5 + ENGINE_FIXED_SECONDS);
        ecs_entity latest;
        int first_count = enemies(&h, &latest);
        if (first_count != 1) {
            preview_runtime *runtime = h.preview
                                         ? prototype_preview_live(h.preview)->runtime
                                         : h.live->runtime;
            execution_program *program = source_program(runtime->sources, "WaveBehavior");
            execution_instance *instance = execution_program_instance(program, ecs_entity_to_u64(owner));
            assert(instance);
            const pin_value *elapsed = execution_instance_state(instance, h.elapsed);
            const pin_value *spawned = execution_instance_state(instance, h.spawned);
            assert(elapsed && spawned && elapsed->type == PIN_F32 && spawned->type == PIN_I32);
            fprintf(
                stderr,
                "wave first spawn: isolated=%d launch=%d count=%d elapsed=%.9g "
                "spawned=%d\n",
                isolated,
                launch,
                first_count,
                elapsed->f,
                spawned->i
            );
        }
        assert(first_count == 1);
        position(&h, latest, -160, 25);
        /* The spawned authored Enemy really begins pursuing the player. */
        tick(&h, 11.6 + ENGINE_FIXED_SECONDS);
        position(&h, latest, -151, 25);
        Movement_get(h.world, latest)->speed = 0;
        if (launch || restarted) {
            break;
        }
        *Transform_get(h.world, target) = (Transform){ 200, -40 };
        tick(&h, 12.99);
        assert(enemies(&h, NULL) == 1);
        tick(&h, 13 + ENGINE_FIXED_SECONDS);
        assert(enemies(&h, &latest) == 2);
        position(&h, latest, 460, -40);
        Movement_get(h.world, latest)->speed = 0;
        engine *runtime = h.simulation.runtime;
        assert(engine_set_playing(runtime, false) == 0);
        simulation_frame(&h.simulation, 100);
        assert(enemies(&h, NULL) == 2);
        assert(engine_step(runtime) == 0);
        simulation_frame(&h.simulation, 101.5);
        assert(enemies(&h, NULL) == 2); /* Step advances 1/60, regardless of wall time. */
        assert(engine_set_playing(runtime, true) == 0);
        tick(&h, 101.5 + 89 * ENGINE_FIXED_SECONDS);
        assert(enemies(&h, &latest) == 3);
        position(&h, latest, -60, -40);
        Movement_get(h.world, latest)->speed = 0;
        tick(&h, 110.5);
        assert(enemies(&h, NULL) == 8);
        tick(&h, 130.5);
        assert(enemies(&h, NULL) == 8);
        if (h.preview) {
            /* This fixture's player is external to preview ownership. */
            assert(ecs_world_destroy_entity(h.world, target) == ECS_OK);
            assert(ecs_world_apply_commands(h.world) == ECS_OK);
            prototype_preview_stop(h.preview);
        } else {
            /* Restart the scene's owned runtime and world, preserving authored assets. */
            engine_free(h.runtime);
            preview_world_free(h.live);
            ecs_world_free(h.world);
            run(false, registry, encounter, player, enemy, true);
            return;
        }
    }
    if (h.preview) {
        prototype_preview_free(h.preview);
    } else {
        engine_free(h.runtime);
        preview_world_free(h.live);
        ecs_world_free(h.world);
    }
}

void test_wave()
{
    prototype_registry registry = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT };
    prototype *encounter = prototype("Encounter", &registry);
    prototype *player = prototype("Player", &registry);
    prototype *enemy = prototype("Enemy", &registry);
    run(false, &registry, encounter, player, enemy, false);
    run(true, &registry, encounter, player, enemy, false);
    prototype_free(encounter);
    prototype_free(player);
    prototype_free(enemy);
    puts("wave PASS: authored spawn cadence, relative alternating positions, eight cap, pursuit, pause and reset");
}
