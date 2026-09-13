#include "graph/compiler/behavior_function_internal.h"
#include "graph/document/serialize.h"
#include "graph/runtime/instance.h"
#include "runtime/behaviors/entities.h"
#include "runtime/generated.h"
#include "support/text_file.h"
#include "test/support/event_fixture.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void bench_encounter_query(int count, unsigned budget, int repeats)
{
    assert(count >= 0 && count <= 20000 && repeats > 0);
    char *json = text_file_read("content/behaviors/ArenaWaveBehavior.json");
    assert(json);
    behavior_asset *asset = link_test_libraries(behavior_asset_load_json(json));
    free(json);
    assert(asset);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(!ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL));
    }
    for (int i = 0; i <= count; i++) {
        ecs_entity e = ecs_world_create_entity(world);
        GameEncounterMember member = { .role = i ? 1 : 0 };
        GameHealth health = { .current = 10, .maximum = 10 };
        assert(
            !ecs_world_record_add(world, e, GAME_COMPONENT_GameEncounterMember.id, &member, 0)
        );
        assert(!ecs_world_record_add(world, e, GAME_COMPONENT_GameHealth.id, &health, 0));
    }
    assert(!ecs_world_apply_commands(world));
    game_entities *entities = game_entities_new(world, NULL);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    behavior_instance_set_trace_enabled(instance, false);
    assert(!behavior_instance_bind_service(instance, "ecs.world", world));
    assert(!behavior_instance_bind_service(instance, "game.entities", entities));
    graph *g = behavior_asset_find_graph(asset, 2010);
    assert(g);
    flow_options options = { .instance = instance, .step_budget = budget };
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    unsigned long steps = 0;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < repeats; i++) {
        flow_run *run = flow_run_start(
            g,
            graph_node_by_id(g, g->entry_id),
            (flow_inputs){ 0 },
            &options,
            NULL
        );
        assert(run);
        int result = flow_run_resume(run);
        if (result) {
            const flow_error *error = flow_run_error(run);
            fprintf(
                stderr,
                "encounter query failed: %s code=%d node=%llu pin=%d "
                "steps=%lu\n",
                error->message,
                error->code,
                (unsigned long long)(error->node ? error->node->id : 0),
                error->pin,
                error->steps
            );
        }
        assert(!result);
        assert(instance->variables[2].i == count && instance->variables[3].b);
        steps = flow_run_steps(run);
        flow_run_free(run);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    printf(
        "encounter_query entities=%d budget=%u repeats=%d ms=%.6f backend=%s "
        "oracle_calls=0 steps=%lu\n",
        count,
        budget,
        repeats,
        ((end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6) / repeats,
        behavior_backend_name(behavior_instance_backend(instance)),
        steps
    );
    behavior_instance_destroy(instance);
    game_entities_free(entities);
    ecs_world_free(world);
    behavior_asset_free(asset);
}

void encounter_query_code(const char *path, int count, int repeats)
{
    assert(count >= 0 && count <= 20000 && repeats >= 0);
    char *json = text_file_read("content/behaviors/ArenaWaveBehavior.json");
    behavior_asset *asset = link_test_libraries(behavior_asset_load_json(json));
    free(json);
    assert(asset);
    behavior_program_publish(asset);
    graph *g = behavior_asset_find_graph(asset, 2010);
    const behavior_function *function = behavior_program_function(
        behavior_program_graph(asset->program, g),
        graph_node_by_id(g, g->entry_id)
    );
    assert(function);
    if (!repeats) {
        FILE *out = fopen(path, "w");
        assert(out);
        behavior_function_write_c(function, out, "generated_encounter_query");
        assert(!fclose(out));
        behavior_asset_free(asset);
        return;
    }
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    int (*entries[])(behavior_function_context *) = {
        (int (*)(behavior_function_context *))behavior_function_entry(function),
        (int (*)(behavior_function_context *))dlsym(library, "generated_encounter_query")
    };
    assert(entries[0] && entries[1]);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(!ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL));
    }
    for (int i = 0; i <= count; i++) {
        ecs_entity e = ecs_world_create_entity(world);
        assert(!ecs_world_record_add(
            world,
            e,
            GAME_COMPONENT_GameEncounterMember.id,
            &(GameEncounterMember){ .role = i ? 1 : 0 },
            0
        ));
        assert(
            !ecs_world_record_add(world, e, GAME_COMPONENT_GameHealth.id, &(GameHealth){ .current = 10 }, 0)
        );
    }
    assert(!ecs_world_apply_commands(world));
    game_entities *entities = game_entities_new(world, NULL);
    behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_bind_service(bi, "ecs.world", world));
    assert(!behavior_instance_bind_service(bi, "game.entities", entities));
    behavior_function_activation activation = { 0 };
    behavior_function_activation_init(function, &activation);
    behavior_function_context ctx = { .budget = 10000,
                                      .services = bi->services,
                                      .activation = &activation };
    behavior_instance_bind_values(bi, &ctx);
    assert(behavior_function_bind(function, &ctx, bi->services, 0));
    behavior_function_prepare_host(function, &ctx);
    const char *order = getenv("CBLUEPRINT_BENCH_QUERY_ROTATION");
    int rotation = order ? atoi(order) : 0;
    assert(rotation >= 0 && rotation < 3);
    for (int at = 0; at < 3; at++) {
        int mode = (at + rotation) % 3;
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (int i = 0; i < repeats; i++) {
            ctx.steps = 0;
            int result = mode ? entries[mode - 1](&ctx)
                              : behavior_function_run(function, &ctx, false);
            assert(
                !result && bi->variables[2].i == count && bi->variables[3].b &&
                !bi->variables[4].b
            );
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        printf(
            "encounter_query_code backend=%s entities=%d repeats=%d ms=%.6f\n",
            (const char *[]){ "BYTECODE", "SLJIT", "C" }[mode],
            count,
            repeats,
            ((end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6) / repeats
        );
    }
    behavior_function_release_host(&ctx);
    behavior_function_activation_free(&activation);
    behavior_instance_destroy(bi);
    game_entities_free(entities);
    ecs_world_free(world);
    behavior_asset_free(asset);
    dlclose(library);
}
