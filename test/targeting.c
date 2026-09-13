#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow_internal.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "support/arena.h"
#include "support/array.h"
#include "test/whole_data_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    graph graph;
    node *nearest;
    arena arena;
    eval_context eval;
    service_binding *services;
    game_live_world *live;
    behavior_asset *asset;
    behavior_instance *instance;
} targeting_case;

static ecs_entity body(targeting_case *test, float x, float y, unsigned category)
{
    ecs_world *world = test->live->ecs;
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameTransform_add(world, entity, 0) == ECS_OK);
    assert(GAME_GameCollider_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameTransform_get(world, entity) = (GameTransform){ x, y };
    *GAME_GameCollider_get(world, entity) = (GameCollider){ .category = category };
    return entity;
}

static void expect_nearest(
    targeting_case *test,
    CF_V2 origin,
    float radius,
    unsigned categories,
    ecs_entity exclude,
    ecs_entity expected
)
{
    eval_context_reset_runtime(&test->eval);
    arena_reset(&test->arena);
    pin_value inputs[] = {
        { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &origin },
        { .type = PIN_F32, .type_name = "float", .f = radius },
        { .type = PIN_U32, .type_name = "unsigned int", .u = categories },
        { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = ecs_entity_to_u64(exclude) },
    };
    for (int i = 0; i < 4; i++) {
        assert(graph_set_pin_default(&test->graph, test->nearest, 0, i, &inputs[i]) == 0);
    }
    pin_value outputs[4];
    for (int i = 0; i < 4; i++) {
        assert(
            graph_eval_output(&test->graph, test->nearest, i, &test->arena, &outputs[i], &test->eval) == 0
        );
    }
    for (int mode = 0; mode < 2; mode++) {
        assert(
            !behavior_instance_set_backend(test->instance, mode ? BEHAVIOR_BYTECODE : BEHAVIOR_SLJIT)
        );
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        flow_run *run = behavior_instance_dispatch(test->instance, "Run", (flow_inputs){ inputs, 4 }, NULL);
        assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_COMPLETED);
        pin_value *actual = test->instance->variables;
        assert(
            actual[0].b == outputs[0].b && actual[1].u64 == outputs[1].u64 &&
            actual[3].f == outputs[3].f
        );
        assert(actual[2].ptr && !memcmp(actual[2].ptr, outputs[2].ptr, sizeof(CF_V2)));
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        assert(!behavior_instance_release_activation(test->instance, run));
    }
    assert(outputs[0].b == (expected.slot != 0));
    assert(outputs[1].u64 == ecs_entity_to_u64(expected));
    const CF_V2 *position = outputs[2].ptr;
    if (!expected.slot) {
        assert(position->x == 0 && position->y == 0 && outputs[3].f == 0);
        return;
    }
    const GameTransform *transform = GAME_GameTransform_get_const(test->live->ecs, expected);
    assert(position->x == transform->x && position->y == transform->y);
    double x = (double)transform->x - origin.x;
    double y = (double)transform->y - origin.y;
    assert(fabs(outputs[3].f - sqrt(x * x + y * y)) < 0.0001);
}

typedef struct
{
    ecs_world *world;
    ecs_entity entity;
} query_mutation;
static const char *mutate_query(void *user, const pin_value *, pin_value *)
{
    query_mutation *mutation = user;
    GAME_GameCollider_get(mutation->world, mutation->entity)->category = 2;
    return NULL;
}
static int mutate_query_flow(node *, flow_context *ctx)
{
    mutate_query(flow_context_service(ctx, "test.query_mutation"), NULL, NULL);
    return flow_context_continue(ctx, 0);
}

static void query_cache_barriers(bool update)
{
    for (int mode = 0; mode < 3; mode++) {
        behavior_backend backend = (behavior_backend[]){ BEHAVIOR_ORACLE,
                                                         BEHAVIOR_BYTECODE,
                                                         BEHAVIOR_SLJIT }[mode];
        behavior_backend previous = behavior_debug_backend(backend);
        for (int write = 0; write < 4; write++) {
            ecs_world *world = ecs_world_new();
            assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL));
            assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameCollider, NULL));
            targeting_case test = { .live = game_live_world_new(world) };
            ecs_entity first = body(&test, 1, 0, 1), second = body(&test, 2, 0, 1);
            behavior_asset *reader = whole_data_fixture(registry_node_by_name("Find Nearest Entity"), false);
            CF_V2 origin = { 0 };
            pin_value args[] = {
                { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &origin },
                { .type = PIN_F32, .f = 100 },
                { .type = PIN_U32, .u = 1 },
                { .type = PIN_HANDLE, .type_name = "ecs.entity" }
            };
            for (int i = 0; i < 4; i++) {
                assert(
                    !graph_set_pin_default(reader->graph, reader->graph->nodes[1], 0, i + 1, &args[i])
                );
            }
            behavior_asset *writer = behavior_asset_new("Change query membership");
            node *event = graph_add_node(writer->graph, &NODE_Event);
            assert(!graph_set_event_name(writer->graph, event, update ? "Tick" : "Run"));
            if (update) {
                assert(!graph_set_event_name(reader->graph, reader->graph->nodes[0], "Tick"));
            }
            pin_spec input[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC } };
            pin_spec output[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
            const uint64_t writes[] = { GAME_COMPONENT_GameCollider.id };
            const node_access access = { .writes = writes, .write_count = 1 };
            const node_host_call host = { .service = "test.query_mutation",
                                          .invoke = mutate_query };
            const node_desc mutation_desc = { .name = "Change query category",
                                              .input_count = 1,
                                              .inputs = input,
                                              .output_count = 1,
                                              .outputs = output,
                                              .flow_kind = NODE_FLOW_CUSTOM,
                                              .flow = mutate_query_flow,
                                              .host_call = &host,
                                              .access = write == 3 ? NULL : &access };
            node *set = graph_add_node(
                writer->graph,
                write >= 2 ? &mutation_desc
                           : registry_node_by_name("Set_GameCollider_category")
            );
            if (write < 2) {
                assert(
                    !graph_set_pin_default(writer->graph, set, 0, 1, &(pin_value){ .type = PIN_U32, .u = 2 })
                );
                assert(!graph_set_pin_default(
                    writer->graph,
                    set,
                    0,
                    2,
                    &(pin_value){ .type = PIN_HANDLE,
                                  .type_name = "ecs.entity",
                                  .u64 = ecs_entity_to_u64(first) }
                ));
            }
            assert(!graph_connect_exec(writer->graph, event, 0, set, 0, 0));
            assert(!game_behavior_register_asset(test.live->behaviors, "Read", reader));
            assert(!game_behavior_register_asset(test.live->behaviors, "Write", writer));
            ecs_entity before = ecs_world_create_entity(world),
                       mutation = ecs_world_create_entity(world),
                       after = ecs_world_create_entity(world);
            assert(!game_behavior_attach(test.live->behaviors, before, "Read"));
            if (write) {
                assert(!game_behavior_attach(test.live->behaviors, mutation, "Write"));
            }
            query_mutation mutation_context = { world, first };
            if (write >= 2) {
                assert(!behavior_instance_bind_service(
                    game_behavior_instance(test.live->behaviors, mutation, "Write"),
                    "test.query_mutation",
                    &mutation_context
                ));
            }
            assert(!game_behavior_attach(test.live->behaviors, after, "Read"));
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
            for (int repeat = 0; repeat < 3; repeat++) {
                /* Native mutation between broadcasts must never reuse old membership. */
                GAME_GameCollider_get(world, first)->category = 1;
                game_entities_profile_queries(test.live->entities, true);
                assert(
                    (update ? game_behavior_update(test.live->behaviors, NULL)
                            : game_behavior_dispatch_all(
                                  test.live->behaviors,
                                  "Run",
                                  (flow_inputs){ 0 },
                                  NULL
                              )) == 2 + (write != 0)
                );
                behavior_instance *
                    a = game_behavior_instance(test.live->behaviors, before, "Read"),
                   *b = game_behavior_instance(test.live->behaviors, after, "Read");
                assert(a->variables[1].u64 == ecs_entity_to_u64(first));
                assert(b->variables[1].u64 == ecs_entity_to_u64(write ? second : first));
                game_entity_query_profile q = game_entities_query_profile(
                    test.live->entities
                );
                assert(q.searches == 2);
                assert(q.cache_builds == (uint64_t)(mode ? 1 + (write != 0) : 0));
                assert(q.world_candidates == (uint64_t)(mode && !write ? 2 : 4));
            }
            if (mode) {
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            }
            game_live_world_free(test.live);
            ecs_world_free(world);
            behavior_asset_free(reader);
            behavior_asset_free(writer);
        }
        behavior_debug_backend(previous);
    }
}

void test_targeting()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameCollider, NULL) == ECS_OK);
    targeting_case test = { .live = game_live_world_new(world) };
    graph_init(&test.graph);
    test.nearest = graph_add_node(&test.graph, registry_node_by_name("Find Nearest Entity"));
    assert(test.nearest && (test.nearest->desc->access->flags & NODE_EFFECT_WORLD_READ));
    arena_init(&test.arena, 4096);
    eval_context_init(&test.eval);
    apush(test.services, ((service_binding){ "game.entities", test.live->entities }));
    test.eval.services = test.services;
    test.asset = whole_data_fixture(registry_node_by_name("Find Nearest Entity"), true);
    test.instance = behavior_instance_create(test.asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(test.instance, false);
    assert(!behavior_instance_set_activation_pool(test.instance, true));
    assert(!behavior_instance_bind_service(
        test.instance,
        "game.entities",
        test.live->entities
    ));
    CF_V2 origin = { 0 };
    ecs_entity none = { 0 };
    expect_nearest(&test, origin, 5, 1, none, none);
    uint64_t data_allocations = behavior_function_data_allocations();
    ecs_entity left = body(&test, -3, -4, 1);
    ecs_entity right = body(&test, 3, 4, 1);
    ecs_entity closer = body(&test, 0, 1, 2);
    assert(ecs_entity_to_u64(left) < ecs_entity_to_u64(right));
    expect_nearest(&test, origin, 5, 1, none, left);
    expect_nearest(&test, origin, 5, 1, left, right);
    expect_nearest(&test, origin, 5, 3, none, closer);
    expect_nearest(&test, origin, 4.99f, 1, none, none);
    expect_nearest(&test, origin, 5, 0, none, none);
    expect_nearest(&test, origin, -1, 3, none, none);
    expect_nearest(&test, origin, NAN, 3, none, none);
    expect_nearest(&test, origin, INFINITY, 3, none, none);
    expect_nearest(&test, (CF_V2){ NAN, 0 }, 5, 3, none, none);
    expect_nearest(&test, (CF_V2){ 0, INFINITY }, 5, 3, none, none);
    GAME_GameTransform_get(world, closer)->x = NAN;
    expect_nearest(&test, origin, 5, 3, none, left);
    GAME_GameTransform_get(world, closer)->x = INFINITY;
    expect_nearest(&test, origin, 5, 3, none, left);
    game_entities_request_destroy(test.live->entities, left);
    expect_nearest(&test, origin, 5, 1, none, right);
    assert(game_entities_flush(test.live->entities) == 0);
    ecs_entity replacement = body(&test, -3, -4, 1);
    ecs_entity winner = ecs_entity_to_u64(replacement) < ecs_entity_to_u64(right)
                          ? replacement
                          : right;
    expect_nearest(&test, origin, 5, 1, left, winner);
    GAME_GameTransform_get(world, replacement)->x = 0;
    GAME_GameTransform_get(world, replacement)->y = 0;
    expect_nearest(&test, origin, 0, 1, none, replacement);
    /* Keep the node memo alive while unrelated graph work grows its arena. */
    pin_value cached;
    assert(graph_eval_output(&test.graph, test.nearest, 2, &test.arena, &cached, &test.eval) == 0);
    CF_V2 before = *(const CF_V2 *)cached.ptr;
    for (int i = 1; i <= 4; i++) {
        size_t size = (size_t)i * 1024 * 1024;
        memset(arena_alloc(&test.arena, size), 0xa5, size);
    }
    pin_value reused;
    assert(graph_eval_output(&test.graph, test.nearest, 2, &test.arena, &reused, &test.eval) == 0);
    assert(reused.ptr == cached.ptr);
    assert(((const CF_V2 *)reused.ptr)->x == before.x);
    assert(((const CF_V2 *)reused.ptr)->y == before.y);
    assert(data_allocations == behavior_function_data_allocations());
    behavior_instance_destroy(test.instance);
    behavior_asset_free(test.asset);
    eval_context_free(&test.eval);
    arena_free(&test.arena);
    graph_free(&test.graph);
    afree(test.services);
    game_live_world_free(test.live);
    ecs_world_free(world);
    whole_data_smoke();
    query_cache_barriers(false);
    query_cache_barriers(true);
    puts("PASS: nearest entity query filters categories, exclusion, retirement, invalid positions and stable ties");
}
