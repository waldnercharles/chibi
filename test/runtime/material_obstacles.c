#include "runtime/material_obstacle_index.h"
#include "support/cos_sin.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t random_bits(uint32_t *seed)
{
    *seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);
    return *seed;
}

static float random_value(uint32_t *seed, float extent)
{
    return ((random_bits(seed) >> 8) / 16777216.f * 2 - 1) * extent;
}

TEST_CASE(known_geometry)
{
    fluid_body bodies[] = { { .position = { -12, -8 } },
                            { .position = { 12, 8 }, .heat = 2 } };
    fluid_obstacle obstacles[] = { { .half_size = { 2, 4 } },
                                   { .body = 1, .radius = 3 } };
    material_obstacle_index *index = material_obstacle_index_new();
    material_obstacle_index_build(index, bodies, 2, obstacles, 2, 4);
    REQUIRE(
        material_obstacle_index_blocked(index, (fluid_vec){ -16, -8 }, (fluid_vec){ -8, -8 }, -1, true)
    );
    REQUIRE(
        !material_obstacle_index_blocked(index, (fluid_vec){ -16, -8 }, (fluid_vec){ -8, -8 }, 0, true)
    );
    REQUIRE(
        !material_obstacle_index_blocked(index, (fluid_vec){ -16, -16 }, (fluid_vec){ -8, -16 }, -1, true)
    );
    REQUIRE(
        material_obstacle_index_blocked(index, (fluid_vec){ 8, 8 }, (fluid_vec){ 16, 8 }, -1, true)
    );
    REQUIRE(
        !material_obstacle_index_blocked(index, (fluid_vec){ 8, 8 }, (fluid_vec){ 16, 8 }, -1, false)
    );
    REQUIRE(
        !material_obstacle_index_blocked(index, (fluid_vec){ 8, 8 }, (fluid_vec){ 16, 8 }, 1, true)
    );
    /* Preserve the exact circle's open tangent convention. */
    REQUIRE(
        !material_obstacle_index_blocked(index, (fluid_vec){ 8, 11 }, (fluid_vec){ 16, 11 }, -1, true)
    );
    /* Rebuild replaces the borrowed geometry; old cells must not survive. */
    bodies[0].position.x = 100;
    material_obstacle_index_build(index, bodies, 2, obstacles, 2, 4);
    REQUIRE(
        !material_obstacle_index_blocked(index, (fluid_vec){ -16, -8 }, (fluid_vec){ -8, -8 }, -1, true)
    );
    material_obstacle_index_build(index, NULL, 0, NULL, 0, 4);
    REQUIRE(
        !material_obstacle_index_blocked(index, (fluid_vec){ -1000, 0 }, (fluid_vec){ 1000, 0 }, -1, true)
    );
    material_obstacle_index_free(index);
    return true;
}

TEST_CASE(compare_queries)
{
    enum
    {
        COUNT = 256
    };
    fluid_body bodies[COUNT];
    fluid_obstacle obstacles[COUNT];
    uint32_t seed = 42;
    for (int i = 0; i < COUNT; i++) {
        bodies[i] = (fluid_body){ .position = { random_value(&seed, 1000),
                                                random_value(&seed, 1000) },
                                  .angle = random_value(&seed, 3.14f),
                                  .heat = i % 7 == 0 ? 2 : 0 };
        obstacles[i] = (fluid_obstacle){ .body = i,
                                         .center = { random_value(&seed, 8),
                                                     random_value(&seed, 8) },
                                         .half_size = { 1 + (i % 9), 1 + (i % 11) },
                                         .radius = i % 2 ? 1 + i % 5 : 0 };
    }
    /* Forces the large-obstacle fallback without a giant reference table. */
    obstacles[0].half_size = (fluid_vec){ 1000, 2 };
    material_obstacle_index *index = material_obstacle_index_new();
    material_obstacle_index_build(index, bodies, COUNT, obstacles, COUNT, 16);
    for (int i = 0; i < 2000; i++) {
        int owner = i % COUNT;
        fluid_vec from = { bodies[owner].position.x + random_value(&seed, 24),
                           bodies[owner].position.y + random_value(&seed, 24) };
        float extent = i % 5 == 0 ? 3000 : 32;
        fluid_vec to = { from.x + random_value(&seed, extent),
                         from.y + random_value(&seed, extent) };
        int ignore = i % 3 == 0 ? owner : -1;
        bool hot_solids = i % 2 != 0;
        bool exact = material_obstacle_blocked(bodies, obstacles, COUNT, from, to, ignore, hot_solids);
        REQUIRE(material_obstacle_index_blocked(index, from, to, ignore, hot_solids) == exact);
        /* Repeated immutable queries produce the same decision. */
        REQUIRE(material_obstacle_index_blocked(index, from, to, ignore, hot_solids) == exact);
    }
    material_obstacle_index_free(index);
    return true;
}

TEST_CASE(distant_population)
{
    enum
    {
        COUNT = 1024
    };
    fluid_body bodies[COUNT];
    fluid_obstacle obstacles[COUNT];
    for (int i = 0; i < COUNT; i++) {
        bodies[i] = (fluid_body){ .position = { i ? 1000.f + i * 32 : -2,
                                                i ? -1000.f : 0 } };
        obstacles[i] = (fluid_obstacle){ .body = i, .radius = 1 };
    }
    material_obstacle_index *index = material_obstacle_index_new();
    material_obstacle_index_build(index, bodies, COUNT, obstacles, COUNT, 8);
    REQUIRE(
        material_obstacle_index_blocked(index, (fluid_vec){ -4, 0 }, (fluid_vec){ 0, 0 }, -1, true)
    );
    REQUIRE(
        !material_obstacle_index_blocked(index, (fluid_vec){ -4, 0 }, (fluid_vec){ 0, 0 }, 0, true)
    );
    REQUIRE(
        !material_obstacle_index_blocked(index, (fluid_vec){ -4, 4 }, (fluid_vec){ 0, 4 }, -1, true)
    );
    /* Very large finite coordinates select exact scanning safely. */
    REQUIRE(!material_obstacle_index_blocked(
        index,
        (fluid_vec){ -1e20f, 1e20f },
        (fluid_vec){ 1e20f, 1e20f },
        -1,
        true
    ));
    material_obstacle_index_free(index);
    return true;
}

static fluid_vec world_point(fluid_body body, fluid_vec local)
{
    cos_sin r = cos_sin_compute(body.angle);
    double norm = (double)r.c * r.c + (double)r.s * r.s;
    return (fluid_vec){
        body.position.x + (r.c * (double)local.x - r.s * (double)local.y) / norm,
        body.position.y + (r.s * (double)local.x + r.c * (double)local.y) / norm
    };
}

TEST_CASE(thin_walls)
{
    fluid_body bodies[] = { { .position = { -24, -72 } },
                            { .position = { -24, 72 } },
                            { .position = { -160, 0 } },
                            { .position = { 112, 0 } } };
    fluid_obstacle obstacles[] = { { .body = 0, .half_size = { 136, .5f } },
                                   { .body = 1, .half_size = { 136, .5f } },
                                   { .body = 2, .half_size = { .5f, 72 } },
                                   { .body = 3, .half_size = { .5f, 72 } } };
    material_obstacle_index *index = material_obstacle_index_new();
    material_obstacle_index_build(index, bodies, 4, obstacles, 4, 4);
    for (int y = -60; y <= 60; y += 4) {
        for (int x = -140; x <= 92; x += 4) {
            fluid_vec a = { x, y }, b = { x + 2, y + 2 };
            REQUIRE(!material_obstacle_index_blocked(index, a, b, -1, true));
            REQUIRE(!material_obstacle_blocked(bodies, obstacles, 4, a, b, -1, true));
        }
    }
    /* Probe both sides and exact tangencies of translated/rotated narrow hulls.
     * The coarse independent scan remains the oracle at rounding boundaries. */
    uint32_t seed = 17;
    for (int turn = 0; turn < 40; turn++) {
        fluid_body body = { .position = { random_value(&seed, 10000),
                                          random_value(&seed, 10000) },
                            .angle = random_value(&seed, 3.14f) };
        fluid_obstacle obstacle = { .center = { -7, 11 }, .half_size = { 100, .5f } };
        material_obstacle_index_build(index, &body, 1, &obstacle, 1, 4);
        for (int sample = -20; sample <= 20; sample++) {
            float edge = 11.5f + sample * .0001f;
            fluid_vec a = world_point(body, (fluid_vec){ -11, edge }),
                      b = world_point(body, (fluid_vec){ -3, edge });
            bool exact = material_obstacle_blocked(&body, &obstacle, 1, a, b, -1, true);
            REQUIRE(material_obstacle_index_blocked(index, a, b, -1, true) == exact);
        }
        obstacle.radius = 2;
        material_obstacle_index_build(index, &body, 1, &obstacle, 1, 4);
        for (int sample = -20; sample <= 20; sample++) {
            float edge = 13 + sample * .0001f;
            fluid_vec a = world_point(body, (fluid_vec){ -11, edge }),
                      b = world_point(body, (fluid_vec){ -3, edge });
            bool exact = material_obstacle_blocked(&body, &obstacle, 1, a, b, -1, true);
            REQUIRE(material_obstacle_index_blocked(index, a, b, -1, true) == exact);
        }
    }
    material_obstacle_index_free(index);
    return true;
}

void test_material_obstacles()
{
    RUN_TEST_CASE(known_geometry);
    RUN_TEST_CASE(compare_queries);
    RUN_TEST_CASE(distant_population);
    RUN_TEST_CASE(thin_walls);
}
