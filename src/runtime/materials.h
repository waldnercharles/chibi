#ifndef CHIBI_GAME_MATERIALS_H
#define CHIBI_GAME_MATERIALS_H

#include "ecs/ecs.h"
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
    int thermal_particles;
    int elastic_particles;
    float volume_min, volume_max, strain_max;
    fluid_mpm_state mpm;
    /* Exact retained-particle centers; all zeros when empty. */
    fluid_region particle_bounds;
    double fuel_remaining, thermal_energy;
    material_stream_state streaming;
    uint64_t hash;
} game_material_state;
typedef struct
{
    uint64_t hash, after_flow, after_chunks, after_particles, after_masks;
    uint64_t particle_position, particle_velocity, particle_parameters,
        particle_appearance;
} game_material_hash_parts;

game_materials *game_materials_new(
    ecs_world *world,
    struct game_entities *entities,
    struct game_particles *particles
);
/* Takes ownership of an already decoded field/argument catalog; NULL creates
 * an empty catalog. No source parsing or text resolver is installed. */
game_materials *game_materials_new_prepared(
    ecs_world *,
    struct game_entities *,
    struct game_particles *,
    material_fields *
);
void game_materials_free(game_materials *materials);
void game_materials_reset(game_materials *materials);
/* Resets all but terrain, which regenerates itself when its own inputs change:
 * the next prepare makes masks, fluid and debris again from their sources. */
void game_materials_reset_except_terrain(game_materials *materials);
/* Outside ECS queries: synchronize authored sources and apply queued damage.
 * Errors describe invalid authored data. Fluid simulation is optional. */
const char *game_materials_prepare(game_materials *materials);
/* Views streamed terrain prepares beside its cameras', such as an editor's:
 * game coordinates, prepared under each terrain's stream budget. Copied;
 * they stay until replaced or reset. */
void game_materials_set_stream_views(
    game_materials *materials,
    const material_stream_view *views,
    int count
);
/* Whether streamed terrain still waits on tiles its views want. */
bool game_materials_streaming(const game_materials *materials);
/* A host that can wait for terrain, as an editor's thumbnail can, streams even
 * the tiles its cameras and moving bodies see under the stream budget, rather
 * than preparing them at once, and polls game_materials_streaming. Kept until
 * reset. */
void game_materials_set_stream_deferred(game_materials *materials, bool deferred);
/* Borrowed mask; NULL for ordinary colliders and uninitialized materials. */
const material_mask *game_materials_mask(const game_materials *materials, ecs_entity entity);
/* Mask incarnation survives revision reuse after reset/reinitialization. */
uint64_t game_materials_mask_generation(const game_materials *materials, ecs_entity entity);
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
void game_materials_tick(
    game_materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
);
void game_materials_tick_profiled(
    game_materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    material_fluid_profile *profile
);
const material_fluid *game_materials_fluid(const game_materials *materials);
/* Material fluids use this gravity from the next prepare on. */
void game_materials_set_gravity(game_materials *materials, fluid_vec gravity);
/* On-demand inspection includes simulation state outside the ECS columns. */
game_material_state game_materials_inspect(const game_materials *materials);
/* Expensive cross-platform diagnostic hashes; compute only when requested. */
game_material_hash_parts game_materials_hash_parts(const game_materials *materials);
/* Borrowed crater debris; empty without a debris limit. */
const material_chunk *game_materials_chunks(const game_materials *materials, int *count);
/* Used by optional particle emitters. False at the retained population limit. */
bool game_materials_emit(game_materials *materials, fluid_particle particle);

material_fields *game_materials_fields(game_materials *materials);
int game_materials_dispatch_events(game_materials *materials);

#endif
