#include "runtime/material_ecs.h"

void materials_tick(
    materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
)
{
    materials_tick_profiled(materials, delta, bodies, body_count, obstacles, obstacle_count, NULL);
}

void materials_tick_profiled(
    materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    material_fluid_profile *profile
)
{
    if (profile) {
        *profile = (material_fluid_profile){ 0 };
    }
    if (delta <= 0) {
        return;
    }
    material_ecs_fade_fragments(materials, delta);
    material_ecs_tick_fluid_profiled(materials, delta, bodies, body_count, obstacles, obstacle_count, profile);
    material_ecs_tick_debris(materials, delta, bodies, body_count, obstacles, obstacle_count);
}
