#include "runtime/material_debris.h"

#include "runtime/component_catalog.h"
#include "runtime/material_ecs.h"
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/runtime/material_fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Solid below y = 10 (gameplay y runs down). */
static bool debris_floor(void *user, fluid_vec point, float *distance)
{
    (void)user;
    *distance = 10 - point.y;
    return true;
}

static material_chunk debris_chunk(fluid_vec position, fluid_vec velocity, float age)
{
    material_chunk chunk = { .position = position,
                             .velocity = velocity,
                             .radius = .8f,
                             .age = age,
                             .lifetime = 5 };
    material_chunk_shape(&chunk, 7);
    return chunk;
}

TEST_CASE(debris_preparation_scratch)
{
    ecs_world *world = ecs_world_new();
    materials *owners[2] = { materials_new(world, NULL, NULL),
                             materials_new(world, NULL, NULL) };
    material_debris *reference = material_debris_new(4);
    material_chunk initial = debris_chunk((fluid_vec){ 0, 8 }, (fluid_vec){ 0 }, 0);
    for (int owner = 0; owner < 2; owner++) {
        materials *materials = owners[owner];
        materials->debris = material_debris_new(4);
        materials->fluid = material_fluid_new(10, 2);
        material_fluid_set_reaction(materials->fluid, (fluid_reaction){ .ignition = 1, .burn_rate = 2 });
        fluid_particle hot = material_test_particle(-8, 0, FLUID_GAS),
                       fuel = material_test_particle(0, 0, FLUID_GAS);
        hot.heat = 2;
        fuel.fuel = 1;
        REQUIRE(material_fluid_add(materials->fluid, hot));
        REQUIRE(material_fluid_add(materials->fluid, fuel));
        material_test_tick(materials->fluid);
        int count;
        const fluid_blast *events = material_fluid_blasts(materials->fluid, &count);
        REQUIRE(count == 1);
        materials->combustion.blast_radius = 30;
        materials->combustion.blast_speed = 20;
        material_debris_blast blast = {
            events[0].position,
            30 * (.35f + .65f * fminf(1, events[0].ignitions / 8.f)),
            20 * fminf(1, events[0].ignitions / 8.f)
        };
        material_debris_add(materials->debris, initial);
        material_ecs_tick_debris(materials, 1.f / 60, NULL, 0, NULL, 0);
        REQUIRE(materials->debris_blasts && !acount(materials->debris_blasts));
        material_debris_blast *storage = materials->debris_blasts;
        int capacity = acap(storage);
        /* Repeat a real populated event while measuring only adapter/tick work. */
        material_debris_clear(materials->debris);
        material_debris_add(materials->debris, initial);
        material_debris_clear(reference);
        material_debris_add(reference, initial);
        xalloc_stats before, after;
        xalloc_stats_get(&before);
        material_ecs_tick_debris(materials, 1.f / 60, NULL, 0, NULL, 0);
        xalloc_stats_get(&after);
        REQUIRE(before.calls == after.calls && before.requested_bytes == after.requested_bytes);
        material_debris_tick(reference, 1.f / 60, NULL, NULL, NULL, 0, NULL, 0, &blast, 1);
        const material_chunk *actual = material_debris_chunks(materials->debris, &count);
        REQUIRE(count == 1 && actual[0].velocity.y > 0);
        const material_chunk *expected = material_debris_chunks(reference, &count);
        REQUIRE(actual[0].velocity.y == expected[0].velocity.y);
        material_fluid_clear(materials->fluid);
        material_ecs_tick_debris(materials, 1.f / 60, NULL, 0, NULL, 0);
        material_debris_tick(reference, 1.f / 60, NULL, NULL, NULL, 0, NULL, 0, NULL, 0);
        actual = material_debris_chunks(materials->debris, &count);
        expected = material_debris_chunks(reference, &count);
        REQUIRE(actual[0].velocity.y == expected[0].velocity.y);
        REQUIRE(actual[0].position.y == expected[0].position.y);
        materials_reset(materials);
        REQUIRE(materials->debris_blasts == storage && acap(storage) == capacity);
        REQUIRE(!acount(materials->debris_blasts) && !acount(materials->debris_tiles));
        material_debris_add(materials->debris, initial);
        material_ecs_tick_debris(materials, 1.f / 60, NULL, 0, NULL, 0);
        actual = material_debris_chunks(materials->debris, &count);
        REQUIRE(count == 1 && actual[0].velocity.y == 0);
    }
    REQUIRE(owners[0]->debris_blasts != owners[1]->debris_blasts);
    materials_free(owners[0]);
    material_ecs_tick_debris(owners[1], 1.f / 60, NULL, 0, NULL, 0);
    materials_free(owners[1]);
    material_debris_free(reference);
    ecs_world_free(world);
    return true;
}

TEST_CASE(debris)
{
    material_debris *debris = material_debris_new(4);
    material_debris_add(debris, debris_chunk((fluid_vec){ 0, 0 }, (fluid_vec){ 0, 60 }, 0));
    int count;
    const material_chunk *chunk = material_debris_chunks(debris, &count);
    REQUIRE(chunk->count >= 5 && chunk->count <= MATERIAL_CHUNK_POINTS);
    for (int k = 0; k < chunk->count; k++) {
        REQUIRE(hypotf(chunk->points[2 * k], chunk->points[2 * k + 1]) <= chunk->radius + 1e-4f);
    }
    for (int i = 0; i < 60; i++) {
        material_debris_tick(debris, 1 / 60.f, debris_floor, NULL, NULL, 0, NULL, 0, NULL, 0);
        chunk = material_debris_chunks(debris, &count);
        REQUIRE(count == 1 && chunk->position.y <= 9.25f);
    }
    /* Too small to split, it bounced back up, losing most of its speed. */
    REQUIRE(chunk->velocity.y < 0 && chunk->velocity.y > -30);

    /* A hard knock splits a large chunk into smaller pieces that keep its
     * material and heat; pieces of pieces split once more at most. */
    material_debris_clear(debris);
    material_chunk large = debris_chunk((fluid_vec){ 0, 0 }, (fluid_vec){ 0, 90 }, 1);
    large.radius = 2;
    large.source = 42;
    large.heat = 1;
    material_chunk_shape(&large, 3);
    material_debris_add(debris, large);
    for (int i = 0; i < 30; i++) {
        material_debris_tick(debris, 1 / 60.f, debris_floor, NULL, NULL, 0, NULL, 0, NULL, 0);
    }
    chunk = material_debris_chunks(debris, &count);
    REQUIRE(count >= 2 && count <= 4);
    for (int i = 0; i < count; i++) {
        REQUIRE(chunk[i].source == 42 && chunk[i].generation >= 1 && chunk[i].radius < 1.5f);
        REQUIRE(chunk[i].heat > 0 && chunk[i].heat < 1 && chunk[i].position.y <= 10);
    }

    /* A blast throws a chunk away from its center. */
    material_debris_clear(debris);
    material_debris_add(debris, debris_chunk((fluid_vec){ 0, 8 }, (fluid_vec){ 0 }, 0));
    material_debris_push(debris, (material_debris_blast){ { 0, 12 }, 20, 200 });
    chunk = material_debris_chunks(debris, &count);
    REQUIRE(chunk->velocity.y < -20 && chunk->shock > 20);

    /* A moving box body shoves chunks it overlaps. */
    material_debris_clear(debris);
    material_debris_add(debris, debris_chunk((fluid_vec){ 0, 0 }, (fluid_vec){ 0 }, 0));
    fluid_body body = { .position = { -1.5f, 0 },
                        .velocity = { 40, 0 },
                        .inverse_mass = 1 };
    fluid_obstacle box = { .body = 0, .half_size = { 1, 1 } };
    material_debris_tick(debris, 1 / 60.f, NULL, NULL, &body, 1, &box, 1, NULL, 0);
    chunk = material_debris_chunks(debris, &count);
    REQUIRE(chunk->position.x >= .29f && chunk->velocity.x > 30);

    /* A full pool replaces its oldest chunk; chunks shrink, then expire. */
    material_debris_free(debris);
    debris = material_debris_new(2);
    material_debris_add(debris, debris_chunk((fluid_vec){ 1, 0 }, (fluid_vec){ 0 }, 3));
    material_debris_add(debris, debris_chunk((fluid_vec){ 2, 0 }, (fluid_vec){ 0 }, 1));
    material_debris_add(debris, debris_chunk((fluid_vec){ 3, 0 }, (fluid_vec){ 0 }, 0));
    chunk = material_debris_chunks(debris, &count);
    REQUIRE(count == 2 && chunk[0].position.x == 3 && chunk[1].position.x == 2);
    REQUIRE(material_chunk_scale(&chunk[0]) == 1);
    int expired = 0;
    for (int i = 0; i < 280; i++) {
        material_debris_tick(debris, 1 / 60.f, NULL, NULL, NULL, 0, NULL, 0, NULL, 0);
        int gone;
        const material_chunk *spent = material_debris_expired(debris, &gone);
        REQUIRE(gone == 0 || (gone == 1 && spent->position.x == 2));
        expired += gone;
    }
    chunk = material_debris_chunks(debris, &count);
    REQUIRE(expired == 1 && count == 1 && chunk->position.x == 3);
    REQUIRE(material_chunk_scale(chunk) < 1 && material_chunk_scale(chunk) > 0);
    material_debris_free(debris);
    return true;
}

void test_material_debris()
{
    RUN_TEST_CASE(debris_preparation_scratch);
    RUN_TEST_CASE(debris);
}
