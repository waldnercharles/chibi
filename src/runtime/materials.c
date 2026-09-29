#include "graph/model/registry.h"
#include "runtime/material_ecs.h"
#include "support/array.h"

#include <math.h>

static node_desc *const MATERIAL_NODES[] = {
    &GAME_NODE_DamageMaterial,
    &GAME_NODE_ReleaseMaterial,
    &GAME_NODE_IsMaterialFragment,
    &GAME_NODE_IsPierceableMaterialFragment,
    &GAME_NODE_EmitFluid,
    &GAME_NODE_HeatArea,
    &GAME_NODE_FluidAt,
    &GAME_NODE_MaterialAt,
    &GAME_NODE_MatterCentroid,
    &GAME_NODE_PushMatter,
    &GAME_NODE_SampleField,
    &GAME_NODE_TerrainDistance,
};
const registry_pack GAME_MATERIAL_PACK = { .name = "engine.materials",
                                           .nodes = MATERIAL_NODES,
                                           .node_count = sizeof MATERIAL_NODES /
                                                         sizeof *MATERIAL_NODES };

material_hit game_materials_at(const game_materials *materials, fluid_vec point)
{
    for (int i = 0; i < acount(materials->records); i++) {
        const material_record *record = &materials->records[i];
        if (!record->mask || !record->mask->count ||
            !ecs_entity_is_alive(materials->world, record->entity)) {
            continue;
        }
        const GameTransform
            *p = GAME_GameTransform_get_const(materials->world, record->entity);
        const GameOrientation
            *o = GAME_GameOrientation_get_const(materials->world, record->entity);
        float angle = o ? o->radians : 0, c = cosf(angle), s = sinf(angle);
        float x = point.x - p->x, y = point.y - p->y;
        if (!material_mask_contains(record->mask, c * x + s * y, -s * x + c * y)) {
            continue;
        }
        const GameSubstance
            *substance = GAME_GameSubstance_get_const(materials->world, record->entity);
        return (material_hit){ record->entity, true, substance ? substance->hardness : 1 };
    }
    return (material_hit){ 0 };
}

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
        const GameSubstance
            *s = GAME_GameSubstance_get_const(materials->world, record->entity);
        apush(
            materials->solid_scratch,
            ((material_solid){ record->mask,
                               { p->x, p->y },
                               o ? o->radians : 0,
                               ecs_entity_to_u64(record->entity),
                               s ? s->corrosion_resistance : 0,
                               s ? s->melts_at : 0 })
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
