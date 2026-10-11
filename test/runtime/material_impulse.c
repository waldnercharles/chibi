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
static preview_world *fixture(ecs_world **out, ecs_entity *body, bool rotation)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    ecs_entity config = ecs_world_create_entity(world);
    add(world, config, &COMPONENT_Transform, &(Transform){ 0 });
    add(world, config, &COMPONENT_Camera, &(Camera){ .width = 64, .height = 64 });
    add(world,
        config,
        &COMPONENT_MaterialFluidWorld,
        &(
            MaterialFluidWorld
        ){ .chunk_size = 64, .buffer = 32, .kernel = 10, .particle_limit = 16 });
    *body = ecs_world_create_entity(world);
    add(world, *body, &COMPONENT_Transform, &(Transform){ 0 });
    add(world, *body, &COMPONENT_Velocity, &(Velocity){ 0 });
    add(world, *body, &COMPONENT_Orientation, &(Orientation){ 0 });
    add(world,
        *body,
        &COMPONENT_Body,
        &(Body){ .type = BODY_DYNAMIC, .allow_rotation = rotation });
    add(world,
        *body,
        &COMPONENT_Collider,
        &(Collider){ .shape = COLLIDER_BOX, .width = 12, .height = 12 });
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(physics_tick(live->runtime->physics, 0) == 0);
    *out = world;
    return live;
}

static bool fluid_rotation(bool rotation)
{
    ecs_world *world;
    ecs_entity body;
    preview_world *live = fixture(&world, &body, rotation);
    const material_fluid *fluid = materials_fluid(live->runtime->materials);
    REQUIRE(materials_emit(
        live->runtime->materials,
        (fluid_particle){ .position = { -8, 4 },
                          .velocity = { 120, 0 },
                          .mass = .01f,
                          .radius = 1,
                          .lifetime = .025f,
                          .kind = FLUID_GAS }
    ));
    fluid_region bounds = materials_inspect(live->runtime->materials).particle_bounds;
    REQUIRE(bounds.x == -8 && bounds.y == 4 && bounds.width == 0 && bounds.height == 0);
    physics_body_state state;
    REQUIRE(physics_inspect_body(live->runtime->physics, body, &state));
    REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    int count;
    const fluid_particle *particle = material_fluid_particles(fluid, &count);
    REQUIRE(count == 1);
    float impulse = .01f * (120 - particle[0].velocity.x);
    REQUIRE(impulse > 0);
    REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    bounds = materials_inspect(live->runtime->materials).particle_bounds;
    REQUIRE(bounds.x == 0 && bounds.y == 0 && bounds.width == 0 && bounds.height == 0);
    float first = Orientation_get_const(world, body)->radians;
    if (rotation) {
        float expected = -4 * impulse / state.inertia / 60;
        REQUIRE(first < 0 && fabsf(first - expected) < .001f);
        REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
        float second = Orientation_get_const(world, body)->radians;
        REQUIRE(fabsf(second - 2 * first) < .001f);
        Body_get(world, body)->allow_rotation = false;
        REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
        REQUIRE(fabsf(Orientation_get_const(world, body)->radians - second) < .001f);
    } else {
        REQUIRE(first == 0);
    }
    preview_world_free(live);
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
