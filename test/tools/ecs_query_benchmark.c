#include "ecs/ecs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* Fixed workloads for today's query shapes, timed per visited entity. Run them
 * through benchmark_trials to compare builds. */
#define BENCHMARK_ENTITIES 4096
#define BENCHMARK_PASSES 2000

static const type_field value_fields[] = {
    { .name = "value", .type = PIN_I32, .offset = 0, .size = sizeof(int), .type_name = "int" },
};
static const type_desc value_type = {
    .name = "sdk.ecs.benchmark_value",
    .size = sizeof(int),
    .fields = value_fields,
    .field_count = 1,
};
enum
{
    POSITION,
    VELOCITY,
    SPRITE,
    FROZEN,
    COMPONENT_COUNT
};
static const ecs_component_desc component_descs[COMPONENT_COUNT] = {
    { .id = 801,
      .key = "sdk.bench.position",
      .name = "Position",
      .element_size = sizeof(int),
      .type = &value_type,
      .index = ECS_INDEX_DENSE },
    { .id = 802,
      .key = "sdk.bench.velocity",
      .name = "Velocity",
      .element_size = sizeof(int),
      .type = &value_type,
      .index = ECS_INDEX_DENSE },
    { .id = 803,
      .key = "sdk.bench.sprite",
      .name = "Sprite",
      .element_size = sizeof(int),
      .type = &value_type },
    { .id = 804,
      .key = "sdk.bench.frozen",
      .name = "Frozen",
      .element_size = sizeof(int),
      .type = &value_type },
};

static ecs_world *world;

/* Every entity has Position, half have Velocity, a quarter Sprite and an
 * eighth Frozen. */
static void prepare_world()
{
    world = ecs_world_new();
    for (int i = 0; i < COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, &component_descs[i], NULL) == ECS_OK);
    }
    ecs_entity entities[BENCHMARK_ENTITIES];
    for (int i = 0; i < BENCHMARK_ENTITIES; i++) {
        entities[i] = ecs_world_record_create(world, 0);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    for (int i = 0; i < BENCHMARK_ENTITIES; i++) {
        for (int c = 0; c < COMPONENT_COUNT; c++) {
            if (i % (1 << c) == 0) {
                int added = ecs_world_record_add(
                    world,
                    entities[i],
                    component_descs[c].id,
                    NULL,
                    0
                );
                assert(added == ECS_OK);
                (void)added;
            }
        }
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
}

static void release_world()
{
    ecs_world_free(world);
    world = NULL;
}

static double now()
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
}

static int drain(ecs_query *query)
{
    int visited = 0;
    ecs_entity entity;
    while (ecs_query_next(query, &entity) > 0) {
        visited++;
    }
    ecs_query_end(query);
    return visited;
}

static void report(const char *name, double started, int visited)
{
    double elapsed = now() - started;
    printf(
        "ecs query benchmark %-12s %8.2f ns/entity %8.2f ms\n",
        name,
        elapsed * 1e9 / visited,
        elapsed * 1e3
    );
}

static int run_components(const ecs_component_id *components, int count)
{
    int visited = 0;
    for (int pass = 0; pass < BENCHMARK_PASSES; pass++) {
        ecs_query query;
        assert(ecs_query_begin(world, &query, components, count) == ECS_OK);
        visited += drain(&query);
    }
    return visited;
}

TEST_CASE(ecs_query_benchmark_dense_pair)
{
    ecs_component_id components[] = { component_descs[POSITION].id,
                                      component_descs[VELOCITY].id };
    double started = now();
    int visited = run_components(components, 2);
    report("dense-pair", started, visited);
    REQUIRE(visited == BENCHMARK_ENTITIES / 2 * BENCHMARK_PASSES);
    return true;
}

TEST_CASE(ecs_query_benchmark_bitset_scan)
{
    double started = now();
    int visited = run_components(&component_descs[SPRITE].id, 1);
    report("bitset-scan", started, visited);
    REQUIRE(visited == BENCHMARK_ENTITIES / 4 * BENCHMARK_PASSES);
    return true;
}

/* Position, (Velocity || Sprite), !Frozen, in the scoped form compiled
 * system queries lower to. */
TEST_CASE(ecs_query_benchmark_boolean)
{
    const ecs_term terms[] = {
        { .id = component_descs[POSITION].id },
        { .kind = ECS_TERM_OPEN },
        { .oper = ECS_OR, .id = component_descs[VELOCITY].id },
        { .oper = ECS_OR, .id = component_descs[SPRITE].id },
        { .kind = ECS_TERM_CLOSE },
        { .oper = ECS_NOT, .id = component_descs[FROZEN].id },
    };
    double started = now();
    int visited = 0;
    for (int pass = 0; pass < BENCHMARK_PASSES; pass++) {
        ecs_query query;
        assert(ecs_query_begin_terms(world, &query, terms, 6) == ECS_OK);
        visited += drain(&query);
    }
    report("boolean", started, visited);
    REQUIRE(visited == BENCHMARK_ENTITIES * 3 / 8 * BENCHMARK_PASSES);
    return true;
}

void test_tool_ecs_query_benchmark()
{
    pu_setup(prepare_world, release_world);
    RUN_TEST_CASE(ecs_query_benchmark_dense_pair);
    RUN_TEST_CASE(ecs_query_benchmark_bitset_scan);
    RUN_TEST_CASE(ecs_query_benchmark_boolean);
    pu_clear_setup();
}
