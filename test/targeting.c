#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "support/arena.h"
#include "support/array.h"

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
    CF_V2 origin = { 0 };
    ecs_entity none = { 0 };
    expect_nearest(&test, origin, 5, 1, none, none);
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
    eval_context_free(&test.eval);
    arena_free(&test.arena);
    graph_free(&test.graph);
    afree(test.services);
    game_live_world_free(test.live);
    ecs_world_free(world);
    puts("PASS: nearest entity query filters categories, exclusion, retirement, invalid positions and stable ties");
}
