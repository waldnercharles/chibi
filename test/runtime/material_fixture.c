#include "test/runtime/material_fixture.h"

#include "support/array.h"

#include <assert.h>

void material_test_check_cover(const material_mask *mask)
{
    int total = 0;
    for (int y = 0; y < mask->height; y++) {
        for (int x = 0; x < mask->width; x++) {
            float px = (x + .5f - mask->width * .5f) * mask->cell_size;
            float py = (y + .5f - mask->height * .5f) * mask->cell_size;
            int covers = 0;
            for (int i = 0; i < acount(mask->rects); i++) {
                material_rect r = mask->rects[i];
                covers += fabsf(px - r.x) < r.width * .5f &&
                          fabsf(py - r.y) < r.height * .5f;
            }
            assert(covers == mask->cells[y * mask->width + x]);
            assert(material_mask_contains(mask, px, py) == (covers == 1));
            total += covers;
        }
    }
    assert(total == mask->count);
}

fluid_particle material_test_particle(float x, float y, int kind)
{
    return (fluid_particle){ .position = { x, y },
                             .kind = kind,
                             .mass = .01f,
                             .radius = 3,
                             .deformation = { .f = { 1, 0, 0, 1 }, .volume = 1 } };
}

void material_test_tick(material_fluid *fluid)
{
    fluid_region region = { -1000, -1000, 2000, 2000 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
}

void material_test_component(
    ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *desc,
    const void *value
)
{
    assert(ecs_world_record_add(world, entity, desc->id, value, 0) == ECS_OK);
    if (desc == &COMPONENT_Destructible) {
        MaterialBreakProfile profile = { .chip_speed = 35,
                                         .fragment_speed = 30,
                                         .blast_speed = 70,
                                         .release_speed = 12,
                                         .dust_limit = 8,
                                         .dust_speed_min = 4,
                                         .dust_speed_max = 12,
                                         .dust_lifetime_min = .15f,
                                         .dust_lifetime_max = .3f,
                                         .dust_size_min = .25f,
                                         .dust_size_max = .5f };
        assert(
            ecs_world_record_add(world, entity, COMPONENT_MaterialBreakProfile.id, &profile, 0) == ECS_OK
        );
    }
}
