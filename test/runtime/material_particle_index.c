#include "runtime/material_particle_index.h"

#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>

static void expect(material_particle_index *index, fluid_region region, const int *expected, int count)
{
    int *ids = NULL;
    material_particle_index_query(index, &region, 1, &ids);
    assert(acount(ids) == count);
    for (int i = 0; i < count; i++) {
        assert(ids[i] == expected[i]);
    }
    afree(ids);
}

TEST_CASE(boundaries_and_removal)
{
    material_particle_index *index = material_particle_index_new(16);
    material_particle_index_add(index, 0, (fluid_vec){ -16, -16 });
    material_particle_index_add(index, 1, (fluid_vec){ -.01f, -.01f });
    material_particle_index_add(index, 2, (fluid_vec){ 0, 0 });
    material_particle_index_add(index, 3, (fluid_vec){ 16, 16 });
    expect(index, (fluid_region){ -16, -16, 0, 0 }, (int[]){ 0, 1 }, 2);
    expect(index, (fluid_region){ 0, 0, 16, 16 }, (int[]){ 2, 3 }, 2);
    material_particle_index_move(index, 0, (fluid_vec){ 0, 0 });
    expect(index, (fluid_region){ -16, -16, 0, 0 }, (int[]){ 1 }, 1);
    material_particle_index_swap_remove(index, 2, 3);
    expect(index, (fluid_region){ 16, 16, 0, 0 }, (int[]){ 2 }, 1);
    material_particle_index_swap_remove(index, 0, 2);
    expect(index, (fluid_region){ 0, 0, 0, 0 }, NULL, 0);
    expect(index, (fluid_region){ 16, 16, 0, 0 }, (int[]){ 0 }, 1);
    material_particle_index_move(index, 1, (fluid_vec){ 16, 16 });
    material_particle_index_swap_remove(index, 0, 1);
    expect(index, (fluid_region){ 16, 16, 0, 0 }, (int[]){ 0 }, 1);
    material_particle_index_swap_remove(index, 0, 0);
    expect(index, (fluid_region){ -100, -100, 200, 200 }, NULL, 0);
    material_particle_index_free(index);
    return true;
}

TEST_CASE(sparse_regions_and_rebuild)
{
    material_particle_index *index = material_particle_index_new(64);
    int *ids = NULL;
    for (int rebuild = 0; rebuild < 8; rebuild++) {
        material_particle_index_clear(index);
        for (int i = 0; i < 12000; i++) {
            material_particle_index_add(index, i, (fluid_vec){ 100000 + i * 128, -100000 });
        }
        material_particle_index_add(index, 12000, (fluid_vec){ -100000000, 100000000 });
        material_particle_index_add(index, 12001, (fluid_vec){ 1, 1 });
        fluid_region regions[] = { { 0, 0, 32, 32 },
                                   { -100000008, 99999992, 16, 16 },
                                   { 0, 0, 64, 64 } };
        material_particle_index_query(index, regions, 3, &ids);
        REQUIRE(acount(ids) == 2 && ids[0] == 12000 && ids[1] == 12001);
        material_particle_index_query(
            index,
            &(fluid_region){ -200000000, -200000000, 400000000, 400000000 },
            1,
            &ids
        );
        REQUIRE(acount(ids) == 12002);
        for (int i = 0; i < 12002; i++) {
            REQUIRE(ids[i] == i);
        }
        /* Repeated single-particle travel must reuse empty chunks and hash slots. */
        for (int i = 0; i < 1000; i++) {
            material_particle_index_move(index, 12001, (fluid_vec){ -5000 - i * 64, 5000 });
        }
        material_particle_index_query(index, NULL, 0, &ids);
        REQUIRE(acount(ids) == 0);
    }
    afree(ids);
    material_particle_index_free(index);
    return true;
}

TEST_CASE(moving_population)
{
    material_particle_index *index = material_particle_index_new(8);
    fluid_vec *positions = NULL;
    int *ids = NULL;
    for (int i = 0; i < 256; i++) {
        fluid_vec position = { (i % 16) * 4 - 32, (i / 16) * 4 - 32 };
        apush(positions, position);
        material_particle_index_add(index, i, position);
    }
    for (int tick = 0; tick < 256; tick++) {
        int moving = tick % acount(positions);
        positions[moving].x += 13;
        positions[moving].y -= 7;
        material_particle_index_move(index, moving, positions[moving]);
        if (tick % 3 == 0) {
            int removed = tick % acount(positions);
            material_particle_index_swap_remove(index, removed, acount(positions) - 1);
            adel(positions, removed);
        }
        fluid_region region = { -24, -24, 48, 48 };
        material_particle_index_query(index, &region, 1, &ids);
        int found = 0;
        for (int i = 0; i < acount(positions); i++) {
            float x = floorf(positions[i].x / 8), y = floorf(positions[i].y / 8);
            if (x >= -3 && x <= 3 && y >= -3 && y <= 3) {
                REQUIRE(found < acount(ids) && ids[found++] == i);
            }
        }
        REQUIRE(found == acount(ids));
    }
    afree(ids);
    afree(positions);
    material_particle_index_free(index);
    return true;
}

void test_material_particle_index()
{
    RUN_TEST_CASE(boundaries_and_removal);
    RUN_TEST_CASE(sparse_regions_and_rebuild);
    RUN_TEST_CASE(moving_population);
}
