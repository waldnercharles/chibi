#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/material_fluid.h"
#include "runtime/materials.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>

static void add(ecs_world *world, ecs_entity entity, const ecs_component_desc *type, const void *value)
{
    assert(ecs_world_record_add(world, entity, type->id, value, 0) == ECS_OK);
}

/* A world with a fluid and a 12-unit dynamic box at the origin, which may
 * rotate when `rotation`. */
static game_preview_world *fixture(ecs_world **out, ecs_entity *body, bool rotation)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_preview_world *live = game_preview_world_new(world);
    ecs_entity config = ecs_world_create_entity(world);
    add(world, config, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    add(world, config, &GAME_COMPONENT_GameCamera, &(GameCamera){ .width = 64, .height = 64 });
    add(world,
        config,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(
            MaterialFluidWorld
        ){ .chunk_size = 64, .buffer = 32, .kernel = 10, .particle_limit = 16 });
    *body = ecs_world_create_entity(world);
    add(world, *body, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    add(world, *body, &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 0 });
    add(world, *body, &GAME_COMPONENT_GameOrientation, &(GameOrientation){ 0 });
    add(world,
        *body,
        &GAME_COMPONENT_GameBody,
        &(GameBody){ .type = GAME_BODY_DYNAMIC, .allow_rotation = rotation });
    add(world,
        *body,
        &GAME_COMPONENT_GameCollider,
        &(GameCollider){ .shape = GAME_COLLIDER_BOX, .width = 12, .height = 12 });
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(game_physics_tick(live->runtime->physics, 0) == 0);
    *out = world;
    return live;
}

static bool fluid_rotation(bool rotation)
{
    ecs_world *world;
    ecs_entity body;
    game_preview_world *live = fixture(&world, &body, rotation);
    const material_fluid *fluid = game_materials_fluid(live->runtime->materials);
    REQUIRE(game_materials_emit(
        live->runtime->materials,
        (fluid_particle){ .position = { -8, 4 },
                          .velocity = { 120, 0 },
                          .mass = .01f,
                          .radius = 1,
                          .lifetime = .025f,
                          .kind = FLUID_GAS }
    ));
    fluid_region bounds = game_materials_inspect(live->runtime->materials).particle_bounds;
    REQUIRE(bounds.x == -8 && bounds.y == 4 && bounds.width == 0 && bounds.height == 0);
    game_physics_body_state state;
    REQUIRE(game_physics_inspect_body(live->runtime->physics, body, &state));
    REQUIRE(game_physics_tick(live->runtime->physics, 1.f / 60) == 0);
    int count;
    const fluid_particle *particle = material_fluid_particles(fluid, &count);
    REQUIRE(count == 1);
    float impulse = .01f * (120 - particle[0].velocity.x);
    REQUIRE(impulse > 0);
    REQUIRE(game_physics_tick(live->runtime->physics, 1.f / 60) == 0);
    bounds = game_materials_inspect(live->runtime->materials).particle_bounds;
    REQUIRE(bounds.x == 0 && bounds.y == 0 && bounds.width == 0 && bounds.height == 0);
    float first = GAME_GameOrientation_get_const(world, body)->radians;
    if (rotation) {
        float expected = -4 * impulse / state.inertia / 60;
        REQUIRE(first < 0 && fabsf(first - expected) < .001f);
        REQUIRE(game_physics_tick(live->runtime->physics, 1.f / 60) == 0);
        float second = GAME_GameOrientation_get_const(world, body)->radians;
        REQUIRE(fabsf(second - 2 * first) < .001f);
        GAME_GameBody_get(world, body)->allow_rotation = false;
        REQUIRE(game_physics_tick(live->runtime->physics, 1.f / 60) == 0);
        REQUIRE(fabsf(GAME_GameOrientation_get_const(world, body)->radians - second) < .001f);
    } else {
        REQUIRE(first == 0);
    }
    game_preview_world_free(live);
    ecs_world_free(world);
    return true;
}

TEST_CASE(fluid_impulse_leaves_fixed_body_unturned)
{
    return fluid_rotation(false);
}

TEST_CASE(fluid_impulse_turns_rotating_body)
{
    return fluid_rotation(true);
}

void test_material_impulse()
{
    RUN_TEST_CASE(fluid_impulse_leaves_fixed_body_unturned);
    RUN_TEST_CASE(fluid_impulse_turns_rotating_body);
}
