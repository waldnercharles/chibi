#include "../ecs/ecs.h"
#include "../game/generated.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define BENCHMARK_ENTITIES 4000
#define BENCHMARK_PASSES 200

static uint64_t elapsed_ns(struct timespec start, struct timespec end)
{
    return (uint64_t)(end.tv_sec - start.tv_sec) * UINT64_C(1000000000) +
           (uint64_t)(end.tv_nsec - start.tv_nsec);
}

static int run_simple(ecs_world *world, int expected)
{
    const ecs_component_id components[] = {
        GAME_COMPONENT_GameMovement.id,
        GAME_COMPONENT_GameInput.id,
    };
    int count = 0;
    for (int pass = 0; pass < BENCHMARK_PASSES; pass++) {
        ecs_query query;
        if (ecs_query_begin(world, &query, components, 2) != ECS_OK) {
            return -1;
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity) > 0) {
            count++;
        }
        ecs_query_end(&query);
    }
    return count == expected * BENCHMARK_PASSES ? 0 : -1;
}

static int run_expression(ecs_world *world, int expected)
{
    ecs_query_expr *expression = ecs_query_expr_new(ECS_QUERY_ALL);
    ecs_query_expr_add(
        expression,
        ecs_query_expr_has(GAME_COMPONENT_GameMovement.id)
    );
    ecs_query_expr_add(expression, ecs_query_expr_has(GAME_COMPONENT_GameInput.id));
    int count = 0;
    for (int pass = 0; pass < BENCHMARK_PASSES; pass++) {
        ecs_query query;
        if (ecs_query_begin_expression(world, &query, expression) != ECS_OK) {
            ecs_query_expr_free(expression);
            return -1;
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity) > 0) {
            count++;
        }
        ecs_query_end(&query);
    }
    ecs_query_expr_free(expression);
    return count == expected * BENCHMARK_PASSES ? 0 : -1;
}

int main(void)
{
    ecs_world *world = ecs_world_new();
    if (!world ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) != ECS_OK ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) != ECS_OK ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameInput, NULL) != ECS_OK) {
        ecs_world_free(world);
        return 1;
    }
    for (int i = 0; i < BENCHMARK_ENTITIES; i++) {
        ecs_entity entity = ecs_world_create_entity(world);
        if (GAME_GameTransform_add(world, entity, 0) != ECS_OK ||
            GAME_GameMovement_add(world, entity, 0) != ECS_OK ||
            GAME_GameInput_add(world, entity, 0) != ECS_OK ||
            ecs_world_apply_commands(world) != ECS_OK) {
            ecs_world_free(world);
            return 1;
        }
    }
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int simple_result = run_simple(world, BENCHMARK_ENTITIES);
    clock_gettime(CLOCK_MONOTONIC, &end);
    uint64_t simple_ns = elapsed_ns(start, end);
    clock_gettime(CLOCK_MONOTONIC, &start);
    int expression_result = run_expression(world, BENCHMARK_ENTITIES);
    clock_gettime(CLOCK_MONOTONIC, &end);
    uint64_t expression_ns = elapsed_ns(start, end);
    ecs_world_free(world);
    if (simple_result || expression_result) {
        return 1;
    }
    printf("query_entities=%d passes=%d\n", BENCHMARK_ENTITIES, BENCHMARK_PASSES);
    printf("simple_and_ns_per_entity_pass=%.2f\n", (double)simple_ns / BENCHMARK_ENTITIES / BENCHMARK_PASSES);
    printf("expression_and_ns_per_entity_pass=%.2f\n", (double)expression_ns / BENCHMARK_ENTITIES / BENCHMARK_PASSES);
    return 0;
}
