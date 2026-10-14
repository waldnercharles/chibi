#include "runtime/materials_native.h"

#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/materials.h"

#include <math.h>
#include <string.h>

const node_access MATERIAL_WRITE_ACCESS = { .flags = NODE_EFFECT_STRUCTURAL };
const node_access MATERIAL_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };

static const char *damage_material(void *service, const pin_value *arguments, pin_value *)
{
    entities *entities = service;
    materials *materials = entities_materials(entities);
    if (!materials) {
        return "Damage Material requires a live world";
    }
    pin_value target = arguments[0], point = arguments[1], radius = arguments[2];
    pin_value energy = arguments[3], cause = arguments[4], source = arguments[5];
    CF_V2 position;
    if (!point.ptr) {
        return "Damage Material needs a position";
    }
    memcpy(&position, point.ptr, sizeof position);
    if (!isfinite(position.x + position.y + radius.f) || radius.f < 0 ||
        !isfinite(energy.f) || energy.f < 0) {
        return "Damage Material needs a finite position and nonnegative radius";
    }
    /* The striking entity's light color and heat shape the impact. */
    material_impact impact = { 0 };
    ecs_world *world = entities_world(entities);
    ecs_entity striker = ecs_entity_from_u64(source.u64);
    if (world && source.u64 && ecs_entity_is_alive(world, striker)) {
        const Material *light = Material_get_const(world, striker);
        const Igniter *igniter = Igniter_get_const(world, striker);
        impact.color = light ? light->emission_color : 0;
        impact.heat = igniter && isfinite(igniter->heat) ? fmaxf(igniter->heat, 0) : 0;
    }
    materials_damage(
        materials,
        ecs_entity_from_u64(target.u64),
        (fluid_vec){ position.x, position.y },
        radius.f,
        energy.f,
        cause.i,
        impact
    );
    return NULL;
}
const node_host_call MATERIAL_DAMAGE_CALL = {
    .service = "game.entities",
    .invoke = damage_material,
    .missing_service = "Damage Material requires a live world",
};
static const char *fragment_host(void *service, const pin_value *arguments, pin_value *out)
{
    materials *materials = entities_materials(service);
    if (!materials) {
        return "Is Material Fragment requires a live world";
    }
    out[0].b = materials_is_fragment(materials, ecs_entity_from_u64(arguments[0].u64));
    return NULL;
}
static const char *pierceable_host(void *service, const pin_value *arguments, pin_value *out)
{
    materials *materials = entities_materials(service);
    if (!materials) {
        return "Is Pierceable Material Fragment requires a live world";
    }
    out[0].b = materials_is_pierceable_fragment(
        materials,
        ecs_entity_from_u64(arguments[0].u64)
    );
    return NULL;
}
static const char *release_host(void *service, const pin_value *arguments, pin_value *)
{
    materials *materials = entities_materials(service);
    if (!materials) {
        return "Release Material requires a live world";
    }
    materials_release(materials, ecs_entity_from_u64(arguments[0].u64));
    return NULL;
}
const node_host_call MATERIAL_FRAGMENT_CALL = {
    .service = "game.entities",
    .invoke = fragment_host,
    .missing_service = "Is Material Fragment requires a live world",
};
const node_host_call MATERIAL_PIERCEABLE_CALL = {
    .service = "game.entities",
    .invoke = pierceable_host,
    .missing_service = "Is Pierceable Material Fragment requires a live world",
};
const node_host_call MATERIAL_RELEASE_CALL = {
    .service = "game.entities",
    .invoke = release_host,
    .missing_service = "Release Material requires a live world",
};
