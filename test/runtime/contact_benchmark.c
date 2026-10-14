#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

/* Fixed 2D physics workloads: circle colliders on a 64-wide lattice, apart or
 * overlapping, timed for the first step and the steady steps after it. Run
 * them through benchmark_trials to compare builds. */
#define PASSES 20

static double seconds()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void add_collider(ecs_world *ecs, int i, float spacing)
{
    ecs_entity entity = ecs_world_create_entity(ecs);
    assert(Transform_add(ecs, entity, 0) == ECS_OK);
    assert(Collider_add(ecs, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(ecs) == ECS_OK);
    *Transform_get(ecs, entity) = (Transform){ (float)(i % 64) * spacing,
                                               (float)(i / 64) * spacing };
    *Collider_get(ecs, entity) = (Collider){ 8.0f, 1, 1 };
}

static bool measure(int count, float spacing)
{
    ecs_world *ecs = ecs_world_new();
    REQUIRE(ecs_world_register_component(ecs, &COMPONENT_Transform, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(ecs, &COMPONENT_Collider, NULL) == ECS_OK);
    preview_world *live = preview_world_new(ecs);
    for (int i = 0; i < count; i++) {
        add_collider(ecs, i, spacing);
    }
    double start = seconds();
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    double first = seconds() - start;
    entities_reset(live->runtime->entities);
    start = seconds();
    for (int i = 0; i < PASSES; i++) {
        REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    }
    double average = (seconds() - start) / PASSES;
    printf(
        "contact benchmark colliders=%d spacing=%.0f first_ms=%.3f "
        "steady_ms=%.3f passes=%d\n",
        count,
        spacing,
        first * 1000.0,
        average * 1000.0,
        PASSES
    );
    preview_world_free(live);
    ecs_world_free(ecs);
    return true;
}

TEST_CASE(contact_benchmark_apart)
{
    REQUIRE(measure(1000, 40));
    REQUIRE(measure(4000, 40));
    return true;
}

TEST_CASE(contact_benchmark_overlapping)
{
    REQUIRE(measure(1000, 12));
    REQUIRE(measure(4000, 12));
    return true;
}

void test_contact_benchmark()
{
    RUN_TEST_CASE(contact_benchmark_apart);
    RUN_TEST_CASE(contact_benchmark_overlapping);
}
