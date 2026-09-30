#include "graph/model/registry.h"
#include "runtime/material_ecs.h"

static node_desc *const MATERIAL_NODES[] = {
    &GAME_NODE_DamageMaterial,
    &GAME_NODE_ReleaseMaterial,
    &GAME_NODE_IsMaterialFragment,
    &GAME_NODE_IsPierceableMaterialFragment,
};
const registry_pack GAME_MATERIAL_PACK = { .name = "engine.materials",
                                           .nodes = MATERIAL_NODES,
                                           .node_count = 4 };

void game_materials_tick(
    game_materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
)
{
    game_materials_tick_profiled(materials, delta, bodies, body_count, obstacles, obstacle_count, NULL);
}

void game_materials_tick_profiled(
    game_materials *materials,
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
