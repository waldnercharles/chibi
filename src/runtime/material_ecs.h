#pragma once
/* Private ECS adapter state. Native material algorithms do not include this file. */
#include "runtime/generated.h"
#include "runtime/material_debris.h"
#include "runtime/material_destruction.h"
#include "runtime/material_stream.h"
#include "runtime/materials.h"
#include "runtime/task_pool.h"

typedef struct
{
    ecs_entity entity;
    material_mask *mask;
    uint64_t mask_generation;
    bool fragment, volume, terrain;
    uint64_t terrain_hash;
    float glow_age, glow_multiplier;
    int initial_cells;
    ecs_entity terrain_source;
    material_stream *stream;
    uint64_t terrain_key;
    bool streamed_mask;
} material_record;

struct game_materials
{
    ecs_world *world;
    struct game_entities *entities;
    struct game_particles *particles;
    material_fluid *fluid;
    material_fields *fields;
    char error[256];
    game_task_pool *pool;
    MaterialFluidWorld settings;
    GameCombustion combustion;
    material_record *records;
    uint64_t next_mask_generation;
    material_destruction *destruction;
    material_damage *damage_requests;
    int fragment_limit;
    fluid_region *regions;
    int fragments;
    material_debris *debris;
    /* Scratch: resident terrain masks and their offsets, rebuilt each tick. */
    struct material_debris_tile *debris_tiles;
    /* Preparation scratch; synchronous consumers borrow only during the call. */
    material_stream_view *terrain_views;
    /* Views a host adds, in game coordinates (game_materials_set_stream_views). */
    material_stream_view *host_views;
    material_debris_blast *debris_blasts;
};

material_record *material_ecs_record(game_materials *materials, ecs_entity entity);
void material_ecs_release_record(material_record *record);
void material_ecs_add_component(
    ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *component,
    const void *value
);
MaterialBreakProfile material_ecs_break_profile(game_materials *materials, ecs_entity entity);
const char *material_ecs_generate_terrains(game_materials *materials);
const char *material_ecs_configure_fluid(game_materials *materials);
const char *material_ecs_fill_volume(game_materials *materials, ecs_entity entity);
void material_ecs_fade_fragments(game_materials *materials, float delta);
/* Seeds a crater's chunks; source names the entity whose material they show. */
void material_ecs_throw_chunks(
    game_materials *materials,
    fluid_vec point,
    fluid_vec normal,
    int cells,
    uint64_t source,
    float heat,
    MaterialBreakProfile profile
);
void material_ecs_tick_debris(
    game_materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
);
void material_ecs_tick_fluid(
    game_materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
);
void material_ecs_tick_fluid_profiled(
    game_materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    material_fluid_profile *profile
);
