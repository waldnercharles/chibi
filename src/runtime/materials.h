#ifndef CHIBI_GAME_MATERIALS_H
#define CHIBI_GAME_MATERIALS_H

#include "ecs/ecs.h"
#include "graph/model/node.h"
#include "runtime/material_debris.h"
#include "runtime/material_destruction.h"
#include "runtime/material_field.h"
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"
#include "runtime/material_stream.h"

struct game_entities;
struct game_particles;
typedef struct game_materials game_materials;
typedef struct
{
    int particles, active_particles, solids, cells, fragments, chunks;
    int fuel_particles, burning_particles;
    material_stream_state streaming;
    uint64_t hash;
} game_material_state;

game_materials *game_materials_new(
    ecs_world *world,
    struct game_entities *entities,
    struct game_particles *particles
);
void game_materials_free(game_materials *materials);
void game_materials_reset(game_materials *materials);
/* Outside ECS queries: synchronize authored sources and apply queued damage.
 * Errors describe invalid authored data. Fluid simulation is optional. */
const char *game_materials_prepare(game_materials *materials);
/* Borrowed mask; NULL for ordinary colliders and uninitialized materials. */
const material_mask *game_materials_mask(const game_materials *materials, ecs_entity entity);
bool game_materials_is_fragment(const game_materials *materials, ecs_entity entity);
bool game_materials_is_pierceable_fragment(const game_materials *materials, ecs_entity entity);
/* Queue a world-space circular cut for the next physics boundary. */
void game_materials_damage(
    game_materials *materials,
    ecs_entity entity,
    fluid_vec point,
    float radius,
    float energy,
    int cause,
    material_impact impact
);
/* Replace the remaining hull with unowned debris at the next physics boundary. */
void game_materials_release(game_materials *materials, ecs_entity entity);
bool game_materials_enabled(const game_materials *materials);
/* Static masks join matter as solids; every other body is a collider. */
bool game_materials_static_solid(const game_materials *materials, ecs_entity entity);
/* Gravity is an acceleration in world units per second squared. */
void game_materials_tick(
    game_materials *materials,
    float delta,
    fluid_vec gravity,
    const fluid_colliders *colliders
);
const material_fluid *game_materials_fluid(const game_materials *materials);
/* On-demand inspection includes simulation state outside the ECS columns. */
game_material_state game_materials_inspect(const game_materials *materials);
/* Borrowed crater debris; empty without a debris limit. */
const material_chunk *game_materials_chunks(const game_materials *materials, int *count);
/* Used by optional particle emitters. False at the retained population limit. */
bool game_materials_emit(game_materials *materials, fluid_particle particle);

material_fields *game_materials_fields(game_materials *materials);
int game_materials_dispatch_events(game_materials *materials);

extern node_desc GAME_NODE_DamageMaterial;
extern node_desc GAME_NODE_ReleaseMaterial;
extern node_desc GAME_NODE_IsMaterialFragment;
extern node_desc GAME_NODE_IsPierceableMaterialFragment;

#endif
