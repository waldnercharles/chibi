#include "ecs/execution.h"
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

typedef struct
{
    GameHealth *player;
    uint64_t node;
    bool changed;
} query_observer;
static void edit_between_counts(void *user, const behavior_function_edge *edge, uintptr_t)
{
    query_observer *observer = user;
    if (edge->kind == GRAPH_EDGE_DATA && edge->from_id == observer->node) {
        observer->player->current = 0;
        observer->changed = true;
    }
}

static void query_guard_parity(
    const behavior_function *f,
    behavior_function_context *ctx,
    int (*const *entries)(behavior_function_context *)
)
{
    const ecs_execution_column **columns = NULL;
    ecs_execution_column wrong;
    int target = -1;
    for (int i = 0; i < acount(f->components); i++) {
        const ecs_execution_column
            *column = ecs_execution_find_column(ctx->world, f->components[i].id);
        assert(column);
        apush(columns, column);
        if (f->components[i].id == GAME_COMPONENT_GameSpawnRequest.id) {
            target = i;
            wrong = *column;
            wrong.layout ^= UINT64_C(1);
        }
    }
    assert(target >= 0);
    const ecs_execution_view *world = ctx->world;
    for (int scenario = 0; scenario < 3; scenario++) {
        columns[target] = scenario == 1 ? NULL : &wrong;
        ctx->columns = columns;
        ctx->world = scenario == 2 ? NULL : world;
        int status = 0, pin = 0, source = 0;
        uintptr_t steps = 0;
        uint64_t node = 0;
        for (int mode = 0; mode < 3; mode++) {
            ctx->steps = 0;
            ctx->activation->pc = 0;
            int result = mode ? entries[mode - 1](ctx)
                              : behavior_function_run(f, ctx, false);
            assert(result < 0);
            if (!mode) {
                status = result;
                pin = ctx->error_pin;
                source = ctx->error_source;
                steps = ctx->error_steps;
                node = ctx->node_id;
                assert(node == f->query_diagnostics[scenario == 2 ? 0 : 1].node_id);
            } else {
                assert(
                    result == status && ctx->error_pin == pin &&
                    ctx->error_source == source && ctx->error_steps == steps &&
                    ctx->node_id == node
                );
            }
        }
    }
    ctx->world = world;
    ctx->columns = NULL;
    ctx->activation->pc = 0;
    ctx->steps = 0;
    afree(columns);
    /* The first fixture row is the player. An inspector editing its health
     * after the first count must affect the later live-player/death queries. */
    const ecs_execution_column
        *health = ecs_execution_find_column(world, GAME_COMPONENT_GameHealth.id);
    query_observer observer = { .player = (GameHealth *)((char *)health->data +
                                                         health->stride),
                                .node = f->query_diagnostics[1].node_id };
    int saved = observer.player->current;
    ctx->trace = edit_between_counts;
    ctx->inspection_user = &observer;
    for (int mode = 0; mode < 3; mode++) {
        observer.changed = false;
        observer.player->current = saved;
        ctx->steps = 0;
        ctx->activation->pc = 0;
        int result = mode == 2 ? entries[1](ctx)
                               : behavior_function_run(f, ctx, mode == 1);
        assert(
            !result && observer.changed && !ctx->variables[3].b &&
            ctx->variables[4].b
        );
    }
    observer.player->current = saved;
    ctx->trace = NULL;
    ctx->inspection_user = NULL;
    ctx->activation->pc = 0;
    ctx->steps = 0;
}

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
    int diagnostics;
    const compile_diagnostic *items = behavior_function_query_diagnostics(function, &diagnostics);
    int fused = 0;
    for (int i = 0; i < diagnostics; i++) {
        fused += items[i].rule == CMPL_RULE_QUERY_FUSION;
    }
    assert(fused == 4);
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
    query_guard_parity(function, &ctx, entries);
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
