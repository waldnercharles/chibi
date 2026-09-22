#pragma once
/* Private ECS adapter state. Native material algorithms do not include this file. */
#include "runtime/generated.h"
#include "runtime/material_destruction.h"
#include "runtime/materials.h"
#include "runtime/task_pool.h"

typedef struct
{
    ecs_entity entity;
    material_mask *mask;
    bool fragment, volume, terrain;
    uint64_t terrain_hash;
    float glow_age, glow_multiplier;
    int initial_cells;
    ecs_entity terrain_source;
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
    material_destruction *destruction;
    material_damage *damage_requests;
    int fragment_limit;
    fluid_region *regions;
    int fragments;
};

material_record *material_ecs_record(game_materials *materials, ecs_entity entity);
const char *material_ecs_configure_fluid(game_materials *materials);
const char *material_ecs_fill_volume(game_materials *materials, ecs_entity entity);
void material_ecs_fade_fragments(game_materials *materials, float delta);
void material_ecs_tick_fluid(
    game_materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
);
