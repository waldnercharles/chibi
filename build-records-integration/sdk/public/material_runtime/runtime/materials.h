#ifndef CHIBI_MATERIALS_H
#define CHIBI_MATERIALS_H

#include "ecs/ecs.h"
#include "runtime/geometry/terrain_volume.h"
#include "runtime/material_debris.h"
#include "runtime/material_destruction.h"
#include "runtime/material_field.h"
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"
#include "runtime/material_stream.h"

struct entities;
struct particles;
typedef struct materials materials;
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
} material_state;
typedef struct
{
    uint64_t hash, after_flow, after_chunks, after_particles, after_masks;
    uint64_t particle_position, particle_velocity, particle_parameters,
        particle_appearance;
} material_hash_parts;

materials *materials_new(ecs_world *world, struct entities *entities, struct particles *particles);
/* Takes ownership of an already decoded field/argument catalog; NULL creates
 * an empty catalog. No source parsing or text resolver is installed. */
materials *materials_new_prepared(ecs_world *, struct entities *, struct particles *, material_fields *);
void materials_free(materials *materials);
void materials_reset(materials *materials);
/* Resets all but terrain, which regenerates itself when its own inputs change:
 * the next prepare makes masks, fluid and debris again from their sources. */
void materials_reset_except_terrain(materials *materials);
/* Outside ECS queries: synchronize authored sources and apply queued damage.
 * Errors describe invalid authored data. Fluid simulation is optional. */
const char *materials_prepare(materials *materials);
/* Host-owned durable storage: call outside queries after prepare. Identity is a
 * nonempty stable caller key, unique per terrain within its save namespace.
 * Write returns malloc bytes. Read validates the full source snapshot before
 * replacing masks; failure preserves the source. After successful read call
 * materials_prepare again before physics/render, to load streamed views and
 * synchronize derived geometry. Only terrain occupancy, signed
 * distances and partial damage persist, not fragments, fluids or actors.
 * Imported terrain requires explicit materials_reset before changing generation
 * or placement/configuration. Limits: 256 tiles, 64 MiB per source snapshot. */
bool materials_terrain_checkpoint_write(
    materials *,
    ecs_entity source,
    const char *identity,
    unsigned char **bytes,
    size_t *length,
    char *error,
    size_t size
);
bool materials_terrain_checkpoint_read(
    materials *,
    ecs_entity source,
    const char *identity,
    const void *bytes,
    size_t length,
    char *error,
    size_t size
);
/* Views streamed terrain prepares beside its cameras', such as an editor's:
 * game coordinates, prepared under each terrain's stream budget. Copied;
 * they stay until replaced or reset. */
void materials_set_stream_views(materials *materials, const material_stream_view *views, int count);
/* Whether streamed terrain still waits on tiles its views want. */
bool materials_streaming(const materials *materials);
/* A host that can wait for terrain, as an editor's thumbnail can, streams even
 * the tiles its cameras and moving bodies see under the stream budget, rather
 * than preparing them at once, and polls materials_streaming. Kept until
 * reset. */
void materials_set_stream_deferred(materials *materials, bool deferred);
/* Borrowed mask; NULL for ordinary colliders and uninitialized materials. */
const material_mask *materials_mask(const materials *materials, ecs_entity entity);
/* Derived rock behind a TerrainVolume terrain source and its live carve,
 * borrowed until the next step. Identity changes with each bake; revision
 * advances whenever the carve follows the mask. */
typedef struct
{
    const terrain_volume *volume;
    const float *removed;
    uint64_t identity, revision;
} materials_depth_volume;
bool materials_terrain_volume(const materials *materials, ecs_entity source, materials_depth_volume *out);
/* Mask incarnation survives revision reuse after reset/reinitialization. */
uint64_t materials_mask_generation(const materials *materials, ecs_entity entity);
bool materials_is_fragment(const materials *materials, ecs_entity entity);
bool materials_is_pierceable_fragment(const materials *materials, ecs_entity entity);
/* Queue a world-space circular cut for the next physics boundary. */
void materials_damage(
    materials *materials,
    ecs_entity entity,
    fluid_vec point,
    float radius,
    float energy,
    int cause,
    material_impact impact
);
/* Replace the remaining hull with unowned debris at the next physics boundary. */
void materials_release(materials *materials, ecs_entity entity);
bool materials_enabled(const materials *materials);
void materials_tick(
    materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
);
void materials_tick_profiled(
    materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    material_fluid_profile *profile
);
const material_fluid *materials_fluid(const materials *materials);
/* Material fluids use this gravity from the next prepare on. */
void materials_set_gravity(materials *materials, fluid_vec gravity);
/* On-demand inspection includes simulation state outside the ECS columns. */
material_state materials_inspect(const materials *materials);
/* Expensive cross-platform diagnostic hashes; compute only when requested. */
material_hash_parts materials_hash_parts(const materials *materials);
/* Borrowed crater debris; empty without a debris limit. */
const material_chunk *materials_chunks(const materials *materials, int *count);
/* Used by optional particle emitters. False at the retained population limit. */
bool materials_emit(materials *materials, fluid_particle particle);

material_fields *materials_fields(materials *materials);
int materials_dispatch_events(materials *materials);

#endif
