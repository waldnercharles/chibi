#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/material_fluid.h"
#include "runtime/materials.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>

static void add(ecs_world *world, ecs_entity entity, const ecs_component_desc *component, const void *value)
{
    assert(ecs_world_record_add(world, entity, component->id, value, 0) == ECS_OK);
}

static ecs_entity slab(ecs_world *world, float y, float height, bool retain)
{
    ecs_entity entity = ecs_world_create_entity(world);
    add(world, entity, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0, y });
    add(world, entity, &GAME_COMPONENT_GameBody, &(GameBody){ .type = GAME_BODY_STATIC });
    add(world,
        entity,
        &GAME_COMPONENT_GameCollider,
        &(GameCollider){ .shape = GAME_COLLIDER_BOX, .width = 40, .height = height });
    add(world,
        entity,
        &GAME_COMPONENT_GameDestructible,
        &(GameDestructible){ .cell_size = 2, .min_fragment_cells = 1 });
    add(world,
        entity,
        &GAME_COMPONENT_GameSubstance,
        &(GameSubstance){ .hardness = 1, .blast_multiplier = 1, .retain_chips = retain });
    return entity;
}

static bool ignition_crater(bool retain)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_preview_world *live = game_preview_world_new(world);
    ecs_entity config = ecs_world_create_entity(world);
    add(world,
        config,
        &GAME_COMPONENT_MaterialDestructionWorld,
        &(MaterialDestructionWorld){ .fragment_limit = 96 });
    add(world,
        config,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(
            MaterialFluidWorld
        ){ .chunk_size = 64, .buffer = 128, .kernel = 3, .particle_limit = 64 });
    add(world,
        config,
        &GAME_COMPONENT_GameCombustion,
        &(GameCombustion){ .ignition = .9f,
                           .burn_rate = .1f,
                           .heat_release = 2,
                           .smoke_lifetime = 3,
                           .blast_radius = 80,
                           .blast_speed = 90,
                           .blast_damage = 20 });
    ecs_entity front = slab(world, 20, 12, retain), rear = slab(world, 36, 8, false);
    ecs_entity fuel = ecs_world_create_entity(world);
    add(world, fuel, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    add(world,
        fuel,
        &GAME_COMPONENT_GameFluidVolume,
        &(
            GameFluidVolume
        ){ .kind = FLUID_GAS, .width = 6, .height = 6, .spacing = 1, .mass = .001f });
    add(world, fuel, &GAME_COMPONENT_GameFuel, &(GameFuel){ .amount = 1 });
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!game_materials_prepare(live->runtime->materials));
    int front_cells = game_materials_mask(live->runtime->materials, front)->count;
    int rear_cells = game_materials_mask(live->runtime->materials, rear)->count;
    game_material_state before = game_materials_inspect(live->runtime->materials);
    /* The body ignites the fuel; no direct Damage Material call is used. */
    fluid_body igniter = { .velocity = { 2, 0 }, .heat = 2 };
    game_materials_tick(live->runtime->materials, 1.f / 60, &igniter, 1, NULL, 0);
    int blasts;
    material_fluid_blasts(game_materials_fluid(live->runtime->materials), &blasts);
    REQUIRE(blasts > 0);
    /* Blast requests resolve at the next material preparation boundary. */
    REQUIRE(!game_materials_prepare(live->runtime->materials));
    REQUIRE(game_materials_mask(live->runtime->materials, front)->count < front_cells);
    REQUIRE(game_materials_mask(live->runtime->materials, rear)->count == rear_cells);
    game_material_state after = game_materials_inspect(live->runtime->materials);
    if (retain) {
        REQUIRE(after.cells == before.cells);
        REQUIRE(after.fragments > before.fragments);
    } else {
        REQUIRE(after.cells < before.cells);
    }
    game_preview_world_free(live);
    ecs_world_free(world);
    return true;
}

TEST_CASE(ignition_crater_removes_cells)
{
    return ignition_crater(false);
}

TEST_CASE(ignition_crater_retains_chips)
{
    return ignition_crater(true);
}

void test_material_blast()
{
    RUN_TEST_CASE(ignition_crater_removes_cells);
    RUN_TEST_CASE(ignition_crater_retains_chips);
}
