#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "runtime/behaviors/entities.h"
#include "test/suite_internal.h"
#include "test/whole_query_fixture.h"

#include <dlfcn.h>
#include <time.h>

static const node_desc *query_node()
{
    const node_desc *n = registry_node_by_name("Find Entities With Component");
    assert(n && n->query);
    return n;
}
static ecs_world *world_new(int count, int dense)
{
    ecs_world *w = ecs_world_new();
    ecs_component_desc desc = GAME_COMPONENT_GameHealth;
    desc.index = dense ? ECS_INDEX_DENSE : ECS_INDEX_BITSET;
    assert(!ecs_world_register_component(w, &desc, NULL));
    for (int i = 0; i < count; i++) {
        ecs_entity e = ecs_world_create_entity(w);
        if (i % 3 != 1) {
            assert(!GAME_GameHealth_add(w, e, 0));
        }
    }
    assert(!ecs_world_apply_commands(w));
    if (count > 3) {
        ecs_entity e = { 3, 1 };
        assert(!ecs_world_record_destroy(w, e, 0));
        assert(!ecs_world_apply_commands(w));
        e = ecs_world_create_entity(w);
        assert(e.generation != 1);
        assert(!GAME_GameHealth_add(w, e, 0));
        assert(!ecs_world_apply_commands(w));
    }
    return w;
}
static void parity(behavior_asset *a, int count, int dense, int scenario, unsigned budget)
{
    graph *g = a->graphs[1];
    ecs_world *w = world_new(count, dense);
    game_entities *entities = game_entities_new(w, NULL);
    if (count > 3) {
        game_entities_request_destroy(entities, (ecs_entity){ 1, 1 });
        assert(!ecs_world_record_destroy(w, (ecs_entity){ 4, 1 }, 0));
    }
    ecs_query active = { 0 };
    if (scenario == 2) {
        assert(!ecs_query_begin(w, &active, &GAME_COMPONENT_GameHealth.id, 1));
    }
    int status = 0, visits = 0, frames = 0;
    uint64_t last = 0;
    pin_value returned[3] = { 0 };
    uintptr_t steps = 0;
    flow_error expected = { 0 };
    flow_call_frame stack[8];
    for (int mode = 0; mode < 3; mode++) {
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        assert(!behavior_instance_set_backend(
            bi,
            (behavior_backend[]){ BEHAVIOR_ORACLE,
                                  BEHAVIOR_BYTECODE,
                                  behavior_default_backend() }[mode]
        ));
        if (scenario != 1) {
            assert(!behavior_instance_bind_service(bi, "game.entities", entities));
        }
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_error error = { 0 };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ 0 }, &options, &error);
        assert(run);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = flow_run_resume(run), frame_count = 0;
        const flow_call_frame *actual_stack = flow_run_call_stack(run, &frame_count);
        if (!mode) {
            status = result;
            visits = bi->variables[0].i;
            last = bi->variables[1].u64;
            steps = flow_run_steps(run);
            expected = *flow_run_error(run);
            frames = frame_count;
            if (!result) {
                returned[1] = *flow_run_input_value(run, g->nodes[1], 1);
                returned[2] = *flow_run_input_value(run, g->nodes[1], 2);
            }
            assert(frames <= 8);
            if (frames) {
                memcpy(stack, actual_stack, (size_t)frames * sizeof *stack);
            }
        } else {
            assert(run->whole_function_executed);
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            assert(result == status && flow_run_steps(run) == steps);
            assert(bi->variables[0].i == visits && bi->variables[1].u64 == last);
            if (!result) {
                assert(
                    flow_run_input_value(run, g->nodes[1], 1)->i == returned[1].i
                );
                assert(
                    flow_run_input_value(run, g->nodes[1], 2)->u64 ==
                    returned[2].u64
                );
            }
            if (result) {
                const flow_error *actual = flow_run_error(run);
                assert(
                    actual->node == expected.node &&
                    actual->pin == expected.pin && actual->steps == expected.steps
                );
                assert(frames == frame_count);
                for (int j = 0; j < frames; j++) {
                    assert(
                        stack[j].graph_id == actual_stack[j].graph_id &&
                        stack[j].node_id == actual_stack[j].node_id
                    );
                }
                if (result != FLOW_ERR_STEP_BUDGET) {
                    assert(!strcmp(actual->message, expected.message));
                }
            }
        }
        flow_run_free(run);
        behavior_instance_destroy(bi);
    }
    if (scenario == 2) {
        ecs_query_end(&active);
    }
    game_entities_free(entities);
    ecs_world_free(w);
}
static const behavior_function *body(behavior_asset *a)
{
    behavior_program_publish(a);
    graph *g = a->graphs[1];
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(a->program, g),
        g->nodes[0]
    );
    assert(f && acount(f->queries));
    return f;
}
static void storage_test()
{
    behavior_asset
        *a = whole_query_fixture(query_node(), GAME_COMPONENT_GameHealth.key, WHOLE_QUERY_NESTED);
    const behavior_function *f = body(a);
    behavior_instance *owner = behavior_instance_create(a, NULL, NULL, NULL);
    ecs_world *w = world_new(11, true);
    game_entities *entities = game_entities_new(w, NULL);
    pin_value variables[2] = { { .type = PIN_I32 }, { .type = PIN_HANDLE } },
              results[3] = { 0 };
    service_binding *services = NULL;
    apush(services, ((service_binding){ "game.entities", entities }));
    behavior_function_context ctx = { .variables = variables,
                                      .results = results,
                                      .budget = 1000 };
    behavior_instance_bind_values(owner, &ctx);
    ctx.variables = variables;
    assert(behavior_function_bind(f, &ctx, services, 0));
    behavior_function_prepare_host(f, &ctx);
    assert(!behavior_function_run(f, &ctx, false));
    assert(acount(ctx.queries) == 2);
    pin_value *outer = ctx.queries[0].snapshot.items,
              *inner = ctx.queries[1].snapshot.items;
    assert(outer != inner);
    int capacity = ctx.queries[0].snapshot.capacity;
    for (int i = 0; i < 100; i++) {
        ctx.steps = 0;
        variables[0].i = 0;
        assert(!behavior_function_run(f, &ctx, false));
        assert(ctx.queries[0].snapshot.items == outer && ctx.queries[1].snapshot.items == inner);
        assert(ctx.queries[0].snapshot.capacity == capacity);
    }
    ecs_query q;
    assert(!ecs_query_begin(w, &q, &GAME_COMPONENT_GameHealth.id, 1));
    ecs_entity e;
    int index = 0;
    while (ecs_query_next(&q, &e)) {
        assert(outer[index].u64 == ecs_entity_to_u64(e));
        assert(inner[index].u64 == outer[index].u64);
        index++;
    }
    ecs_query_end(&q);
    assert(index == ctx.queries[0].snapshot.count && variables[0].i == index * index);
    behavior_function_release_host(&ctx);
    assert(!ctx.queries);
    afree(services);
    game_entities_free(entities);
    ecs_world_free(w);
    behavior_instance_destroy(owner);
    behavior_asset_free(a);
}
static void escaping_snapshot()
{
    behavior_asset
        *a = whole_query_fixture(query_node(), GAME_COMPONENT_GameHealth.key, WHOLE_QUERY_RANGE);
    graph *g = a->graphs[1];
    node *query = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i]->desc == query_node()) {
            query = g->nodes[i];
        }
    }
    assert(query);
    assert(graph_add_pin(g, g->nodes[1], 0, &query->output_pins[0]) == 3);
    assert(!graph_connect_data(g, query, 0, g->nodes[1], 3));
    const behavior_function *f = body(a);
    parity(a, 11, true, 0, 1000);
    for (int native = 0; native < 2; native++) {
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        ecs_world *w = world_new(11, true);
        game_entities *entities = game_entities_new(w, NULL);
        service_binding *services = NULL;
        apush(services, ((service_binding){ "game.entities", entities }));
        pin_value results[4] = { 0 };
        arena storage = { 0 };
        behavior_function_context ctx = { .results = results, .budget = 1000 };
        behavior_instance_bind_values(bi, &ctx);
        behavior_function_bind_storage(&ctx, &storage);
        assert(behavior_function_bind(f, &ctx, services, 0));
        behavior_function_activation activation = { 0 };
        behavior_function_activation_init(f, &activation);
        ctx.activation = &activation;
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        assert(!behavior_function_run(f, &ctx, native));
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        uint64_t *expected = NULL;
        ecs_query q;
        ecs_entity e;
        assert(!ecs_query_begin(w, &q, &GAME_COMPONENT_GameHealth.id, 1));
        while (ecs_query_next(&q, &e)) {
            apush(expected, ecs_entity_to_u64(e));
        }
        ecs_query_end(&q);
        behavior_function_activation_free(&activation);
        afree(services);
        game_entities_free(entities);
        ecs_world_free(w);
        behavior_instance_destroy(bi);
        const pin_array *retained = results[3].ptr;
        assert(retained && retained->count == acount(expected));
        assert(results[3].container == PIN_CONTAINER_ARRAY);
        assert(!strcmp(retained->type_name, "ecs.entity"));
        for (int i = 0; i < retained->count; i++) {
            assert(retained->items[i].u64 == expected[i]);
            assert(!strcmp(retained->items[i].type_name, "ecs.entity"));
        }
        afree(expected);
        arena_free(&storage);
    }
    behavior_asset_free(a);
}
void test_compiled_queries()
{
    extern void test_entity_query(behavior_backend backend);
    extern void test_entity_query_diagnostics();
    test_entity_query_diagnostics();
    test_entity_query(BEHAVIOR_ORACLE);
    test_entity_query(BEHAVIOR_BYTECODE);
    test_entity_query(BEHAVIOR_SLJIT);
    for (int kind = 0; kind < WHOLE_QUERY_COUNT; kind++) {
        for (int missing = 0; missing < 2; missing++) {
            behavior_asset *a = whole_query_fixture(
                query_node(),
                missing ? "absent" : GAME_COMPONENT_GameHealth.key,
                kind
            );
            for (int dense = 0; dense < 2; dense++) {
                for (int scenario = 0; scenario < 3; scenario++) {
                    for (unsigned budget = 1; budget <= 20; budget++) {
                        parity(a, 7, dense, scenario, budget);
                    }
                    parity(a, 7, dense, scenario, 1000);
                }
            }
            parity(a, 0, false, 0, 1000);
            behavior_asset_free(a);
        }
    }
    escaping_snapshot();
    storage_test();
    whole_query_smoke();
    test_note("PASS: compiled query order, empty/dense/sparse worlds, retired/reused entities, nested loops/calls, bounded snapshot storage and error/source parity\n");
}
void emit_query_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_QUERY_COUNT; kind++) {
        behavior_asset
            *a = whole_query_fixture(query_node(), GAME_COMPONENT_GameHealth.key, kind);
        char name[40];
        snprintf(name, sizeof name, "generated_query%d", kind);
        behavior_function_write_c(body(a), out, name);
        behavior_asset_free(a);
    }
    assert(!fclose(out));
}
static double seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_query(const char *path, int count, int rotation)
{
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int kind = 0; kind < WHOLE_QUERY_COUNT; kind++) {
        behavior_asset
            *a = whole_query_fixture(query_node(), GAME_COMPONENT_GameHealth.key, kind);
        const behavior_function *f = body(a);
        char name[40];
        snprintf(name, sizeof name, "generated_query%d", kind);
        int (*entries[])(behavior_function_context *) = {
            (int (*)(behavior_function_context *))behavior_function_entry(f),
            (int (*)(behavior_function_context *))dlsym(library, name)
        };
        assert(entries[0] && entries[1]);
        ecs_world *w = world_new(24, true);
        game_entities *entities = game_entities_new(w, NULL);
        service_binding *services = NULL;
        apush(services, ((service_binding){ "game.entities", entities }));
        pin_value vars[2] = { { .type = PIN_I32 }, { .type = PIN_HANDLE } },
                  results[3] = { 0 };
        behavior_function_context ctx = { .variables = vars,
                                          .results = results,
                                          .services = services };
        behavior_instance *owner = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_bind_values(owner, &ctx);
        ctx.variables = vars;
        behavior_function_prepare_host(f, &ctx);
        for (int scenario = 0; scenario < 3; scenario++) {
            for (int q = 0; q < acount(ctx.queries); q++) {
                ctx.queries[q].service = scenario == 1 ? NULL : entities;
                ctx.queries[q].key = scenario == 2 ? "absent"
                                                   : GAME_COMPONENT_GameHealth.key;
            }
            for (unsigned pass = 1; pass <= 33; pass++) {
                unsigned budget = pass == 33 ? 5000 : pass;
                ctx.steps = 0;
                ctx.budget = budget;
                vars[0].i = 0;
                vars[1].u64 = 0;
                int status = behavior_function_run(f, &ctx, false),
                    visits = vars[0].i;
                uint64_t last = vars[1].u64;
                behavior_function_context expected = ctx;
                pin_value returned[3];
                memcpy(returned, results, sizeof returned);
                for (int mode = 0; mode < 2; mode++) {
                    ctx.steps = 0;
                    vars[0].i = 0;
                    vars[1].u64 = 0;
                    assert(
                        entries[mode](&ctx) == status && ctx.steps == expected.steps
                    );
                    assert(vars[0].i == visits && vars[1].u64 == last);
                    if (!status) {
                        assert(
                            results[1].i == returned[1].i &&
                            results[2].u64 == returned[2].u64
                        );
                    }
                    if (status) {
                        assert(
                            ctx.node_id == expected.node_id &&
                            ctx.error_pin == expected.error_pin &&
                            ctx.failure == expected.failure &&
                            ctx.error_stack == expected.error_stack &&
                            ctx.error_steps == expected.error_steps
                        );
                    }
                }
            }
        }
        for (int q = 0; q < acount(ctx.queries); q++) {
            ctx.queries[q].service = entities;
            ctx.queries[q].key = GAME_COMPONENT_GameHealth.key;
        }
        if (kind == WHOLE_QUERY_RANGE) {
            for (int order = 0; order < 3; order++) {
                int mode = (order + rotation) % 3;
                ctx.budget = 1000;
                uint64_t checksum = 0;
                double start = 0;
                for (int i = -10000; i < count; i++) {
                    if (!i) {
                        start = seconds();
                    }
                    vars[0].i = i & 7;
                    ctx.steps = 0;
                    int status = mode < 2 ? entries[mode](&ctx)
                                          : behavior_function_run(f, &ctx, false);
                    assert(!status && vars[0].i == 16 + (i & 7));
                    if (i >= 0) {
                        checksum += vars[0].i + vars[1].u64;
                    }
                }
                printf(
                    "%s query_body ns=%.3f checksum=%llu\n",
                    (const char *[]){ "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
                    (seconds() - start) * 1e9 / count,
                    (unsigned long long)checksum
                );
            }
        }
        if (kind == WHOLE_QUERY_RANGE) {
            behavior_query_binding *q = &ctx.queries[0];
            uint64_t checksum = 0;
            double start = seconds();
            for (int i = 0; i < count; i++) {
                assert(!q->read(q->service, q->key, &q->snapshot));
                assert(q->snapshot.count == 16);
                checksum += q->snapshot.items[15].u64 + 16 + (i & 7);
            }
            printf(
                "C_HELPER query_body ns=%.3f checksum=%llu\n",
                (seconds() - start) * 1e9 / count,
                (unsigned long long)checksum
            );
        }
        behavior_function_release_host(&ctx);
        afree(services);
        game_entities_free(entities);
        ecs_world_free(w);
        behavior_instance_destroy(owner);
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    test_note("PASS: generated-C/SLJIT/bytecode query parity with shared snapshot helper\n");
    return 0;
}
