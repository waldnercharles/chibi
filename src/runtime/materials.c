#include "graph/model/registry.h"
#include "runtime/material_ecs.h"
#include "support/array.h"

static node_desc *const MATERIAL_NODES[] = {
    &GAME_NODE_DamageMaterial,
    &GAME_NODE_ReleaseMaterial,
    &GAME_NODE_IsMaterialFragment,
    &GAME_NODE_IsPierceableMaterialFragment,
};
const registry_pack GAME_MATERIAL_PACK = { .name = "engine.materials",
                                           .nodes = MATERIAL_NODES,
                                           .node_count = 4 };

bool game_materials_static_solid(const game_materials *materials, ecs_entity entity)
{
    if (!game_materials_mask(materials, entity) ||
        materials->records[entity.slot].fragment) {
        return false;
    }
    const GameBody *body = GAME_GameBody_get_const(materials->world, entity);
    return !body || body->type == GAME_BODY_STATIC;
}

static void place_solids(game_materials *materials)
{
    aclear(materials->solid_scratch);
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (!record->mask || !record->mask->width || !record->mask->count ||
            !ecs_entity_is_alive(materials->world, record->entity) ||
            !game_materials_static_solid(materials, record->entity)) {
            continue;
        }
        const GameTransform
            *p = GAME_GameTransform_get_const(materials->world, record->entity);
        const GameOrientation
            *o = GAME_GameOrientation_get_const(materials->world, record->entity);
        apush(
            materials->solid_scratch,
            ((material_solid){ record->mask,
                               { p->x, p->y },
                               o ? o->radians : 0,
                               ecs_entity_to_u64(record->entity) })
        );
    }
    material_solids_set(
        materials->solids,
        materials->solid_scratch,
        acount(materials->solid_scratch)
    );
}

void game_materials_tick(game_materials *materials, float delta, fluid_vec gravity, const fluid_colliders *colliders)
{
    if (delta <= 0) {
        return;
    }
    place_solids(materials);
    fluid_step step = { .delta = delta,
                        .chunk_size = materials->settings.chunk_size,
                        .gravity = gravity,
                        .colliders = *colliders,
                        .solids = materials->solids };
    material_ecs_fade_fragments(materials, delta);
    material_ecs_tick_fluid(materials, &step);
    material_ecs_tick_debris(materials, &step);
}
