/* Substance and reaction tables from registered prototypes. A substance's
 * prototype name is its name; indices only grow, so particles keep theirs
 * when prototypes change. Product names that are not substances name
 * deposit targets, MaterialTerrain.deposit. */
#include "runtime/behaviors/entities.h"
#include "runtime/material_ecs.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static_assert(MATERIAL_PHASE_GAS == FLUID_GAS && MATERIAL_PHASE_ELASTIC == FLUID_ELASTIC);

static const void *component_of(const game_prototype *prototype, const ecs_component_desc *desc)
{
    for (int i = 0; i < acount(prototype->components); i++) {
        if (prototype->components[i].desc->id == desc->id) {
            return prototype->components[i].data;
        }
    }
    return NULL;
}

static int find_name(char **names, const char *name)
{
    for (int i = 0; i < acount(names); i++) {
        if (!strcmp(names[i], name)) {
            return i;
        }
    }
    return -1;
}

int game_materials_substance(const game_materials *materials, const char *name)
{
    return name && name[0] ? find_name(materials->substance_names, name) : -1;
}

const char *game_materials_substance_name(const game_materials *materials, int substance)
{
    assert(substance >= 0 && substance < acount(materials->substance_names));
    return materials->substance_names[substance];
}

int game_materials_deposit(const game_materials *materials, const char *name)
{
    return name && name[0] ? find_name(materials->deposit_names, name) : -1;
}

/* A product name: a substance, a deposit target, or none. */
static int product(game_materials *materials, const char *name, int none)
{
    if (!name || !name[0]) {
        return none;
    }
    int substance = find_name(materials->substance_names, name);
    if (substance >= 0) {
        return substance;
    }
    int deposit = find_name(materials->deposit_names, name);
    if (deposit < 0) {
        deposit = acount(materials->deposit_names);
        apush(materials->deposit_names, xstrdup(name));
    }
    return FLUID_DEPOSIT - deposit;
}

static bool valid_substance(MaterialSubstance s)
{
    float values[] = { s.stiffness,  s.friction,   s.density,     s.viscosity,
                       s.cohesion,   s.buoyancy,   s.emission,    s.absorption,
                       s.visibility, s.refraction, s.temperature, s.cooling,
                       s.lifetime,   s.ignite_at,  s.burn_rate,   s.burn_heat,
                       s.blast,      s.boil_at,    s.freeze_at,   s.corrosion };
    for (int i = 0; i < (int)(sizeof values / sizeof *values); i++) {
        if (!isfinite(values[i])) {
            return false;
        }
    }
    return s.phase >= MATERIAL_PHASE_GAS && s.phase <= MATERIAL_PHASE_ELASTIC &&
           s.density > 0 && s.viscosity >= 0 && s.cohesion >= 0 &&
           s.emission >= 0 && s.absorption >= 0 && s.visibility >= 0 &&
           s.visibility <= 1 && s.refraction >= 0 && s.refraction <= 16 &&
           s.cooling >= 0 && s.lifetime >= 0 && s.ignite_at >= 0 && s.blast >= 0 &&
           s.corrosion >= 0 && (s.ignite_at == 0 || s.burn_rate > 0) &&
           s.viscosity <= 1 && s.cohesion <= 1 && s.friction >= 0 &&
           s.friction <= 1 && s.stiffness >= 0 && s.stiffness <= 1 &&
           (s.phase == MATERIAL_PHASE_GAS || s.stiffness > 0);
}

static fluid_substance convert(game_materials *materials, MaterialSubstance s)
{
    return (fluid_substance){ .phase = s.phase,
                              .density = s.density,
                              .stiffness = s.stiffness,
                              .viscosity = s.viscosity,
                              .cohesion = s.cohesion,
                              .friction = s.friction,
                              .buoyancy = s.buoyancy,
                              .color = s.color,
                              .emission = s.emission,
                              .absorption = s.absorption,
                              .visibility = s.visibility,
                              .refraction = s.refraction,
                              .temperature = s.temperature,
                              .cooling = s.cooling,
                              .lifetime = s.lifetime,
                              .ignite_at = s.ignite_at,
                              .burn_rate = s.burn_rate,
                              .burn_heat = s.burn_heat,
                              .blast = s.blast,
                              .burns_to = product(materials, s.burns_to, FLUID_GONE),
                              .boil_at = s.boil_at,
                              .boils_to = product(materials, s.boils_to, FLUID_KEEP),
                              .freeze_at = s.freeze_at,
                              .freezes_to = product(materials, s.freezes_to, FLUID_KEEP),
                              .corrosion = s.corrosion,
                              .conductive = s.conductive };
}

static const char *load_substances(game_materials *materials, fluid_substance **table)
{
    const game_entities *entities = materials->entities;
    int count = game_entities_prototype_count(entities);
    for (int i = 0; i < count; i++) {
        const game_prototype *prototype = game_entities_prototype(entities, i);
        if (component_of(prototype, &GAME_COMPONENT_MaterialSubstance) &&
            find_name(materials->substance_names, prototype->name) < 0) {
            apush(materials->substance_names, xstrdup(prototype->name));
        }
    }
    int previous;
    const fluid_substance *old = material_fluid_substances(materials->fluid, &previous);
    for (int n = 0; n < acount(materials->substance_names); n++) {
        const MaterialSubstance *authored = NULL;
        for (int i = 0; i < count && !authored; i++) {
            const game_prototype *prototype = game_entities_prototype(entities, i);
            if (!strcmp(prototype->name, materials->substance_names[n])) {
                authored = component_of(prototype, &GAME_COMPONENT_MaterialSubstance);
            }
        }
        /* A substance whose prototype went away keeps its last properties. */
        if (!authored) {
            apush(*table, old[n]);
            continue;
        }
        if (!valid_substance(*authored)) {
            snprintf(
                materials->error,
                sizeof materials->error,
                "Substance %s needs a phase, positive density, finite "
                "nonnegative look, cooling and lifetime, and a burn rate when "
                "flammable",
                materials->substance_names[n]
            );
            return materials->error;
        }
        apush(*table, convert(materials, *authored));
    }
    return NULL;
}

static const char *load_reactions(game_materials *materials, fluid_reaction **table)
{
    const game_entities *entities = materials->entities;
    for (int i = 0; i < game_entities_prototype_count(entities); i++) {
        const game_prototype *prototype = game_entities_prototype(entities, i);
        const MaterialReaction *r = component_of(prototype, &GAME_COMPONENT_MaterialReaction);
        if (!r) {
            continue;
        }
        bool burning = r->b && !strcmp(r->b, "Burning");
        int a = game_materials_substance(materials, r->a);
        int b = burning ? FLUID_BURNING : game_materials_substance(materials, r->b);
        if (a < 0 || (!burning && b < 0) || !isfinite(r->rate + r->heat) ||
            r->rate <= 0) {
            snprintf(
                materials->error,
                sizeof materials->error,
                "Reaction %s needs substance a, substance or Burning b, and a "
                "positive rate",
                prototype->name
            );
            return materials->error;
        }
        apush(
            *table,
            ((fluid_reaction){ a,
                               b,
                               r->rate,
                               product(materials, r->a_becomes, FLUID_KEEP),
                               product(materials, r->b_becomes, FLUID_KEEP),
                               r->heat })
        );
    }
    return NULL;
}

void material_ecs_free_substances(game_materials *materials)
{
    for (int i = 0; i < acount(materials->substance_names); i++) {
        free(materials->substance_names[i]);
    }
    for (int i = 0; i < acount(materials->deposit_names); i++) {
        free(materials->deposit_names[i]);
    }
    afree(materials->substance_names);
    afree(materials->deposit_names);
    materials->substances_loaded = false;
}

const char *material_ecs_load_substances(game_materials *materials)
{
    unsigned revision = game_entities_prototype_revision(materials->entities);
    if (materials->substances_loaded && materials->substance_revision == revision) {
        return NULL;
    }
    fluid_substance *substances = NULL;
    fluid_reaction *reactions = NULL;
    const char *error = load_substances(materials, &substances);
    if (!error) {
        error = load_reactions(materials, &reactions);
    }
    if (!error) {
        material_fluid_set_substances(materials->fluid, substances, acount(substances));
        material_fluid_set_reactions(materials->fluid, reactions, acount(reactions));
        materials->substances_loaded = true;
        materials->substance_revision = revision;
    }
    afree(substances);
    afree(reactions);
    return error;
}
