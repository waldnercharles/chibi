#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/material_fluid.h"
#include "runtime/materials.h"

#include <assert.h>
#include <math.h>

static void add(ecs_world *world, ecs_entity entity, const ecs_component_desc *type, const void *value)
{
    assert(ecs_world_record_add(world, entity, type->id, value, 0) == ECS_OK);
}

static preview_world *fixture(ecs_world **out, ecs_entity *config, ecs_entity *body, bool rotation)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    *config = ecs_world_create_entity(world);
    add(world, *config, &COMPONENT_Transform, &(Transform){ 0 });
    add(world, *config, &COMPONENT_Camera, &(Camera){ .width = 64, .height = 64 });
    add(world,
        *config,
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

static void authored_gravity()
{
    ecs_world *world;
    ecs_entity config, body;
    preview_world *live = fixture(&world, &config, &body, false);
    for (int i = 0; i < 6; i++) {
        assert(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    }
    assert(Transform_get_const(world, body)->y == 0);
    add(world, config, &COMPONENT_Gravity, &(Gravity){ { 0, -320 } });
    assert(ecs_world_apply_commands(world) == ECS_OK);
    for (int i = 0; i < 6; i++) {
        assert(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    }
    assert(fabsf(Velocity_get_const(world, body)->y + 32) < .01f);
    assert(Transform_get_const(world, body)->y < -1);
    Gravity_get(world, config)->acceleration = (CF_V2){ 0 };
    float speed = Velocity_get_const(world, body)->y;
    assert(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    assert(fabsf(Velocity_get_const(world, body)->y - speed) < .01f);
    preview_world_free(live);
    ecs_world_free(world);
}

static void fluid_rotation(bool rotation)
{
    ecs_world *world;
    ecs_entity config, body;
    preview_world *live = fixture(&world, &config, &body, rotation);
    const material_fluid *fluid = materials_fluid(live->runtime->materials);
    assert(materials_emit(
        live->runtime->materials,
        (fluid_particle){ .position = { -8, 4 },
                          .velocity = { 120, 0 },
                          .mass = .01f,
                          .radius = 1,
                          .lifetime = .025f,
                          .kind = FLUID_GAS }
    ));
    fluid_region bounds = materials_inspect(live->runtime->materials).particle_bounds;
    assert(bounds.x == -8 && bounds.y == 4 && bounds.width == 0 && bounds.height == 0);
    physics_body_state state;
    assert(physics_inspect_body(live->runtime->physics, body, &state));
    assert(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    int count;
    const fluid_particle *particle = material_fluid_particles(fluid, &count);
    assert(count == 1);
    float impulse = .01f * (120 - particle[0].velocity.x);
    assert(impulse > 0);
    assert(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    bounds = materials_inspect(live->runtime->materials).particle_bounds;
    assert(bounds.x == 0 && bounds.y == 0 && bounds.width == 0 && bounds.height == 0);
    float first = Orientation_get_const(world, body)->radians;
    if (rotation) {
        float expected = -4 * impulse / state.inertia / 60;
        assert(first < 0 && fabsf(first - expected) < .001f);
        assert(physics_tick(live->runtime->physics, 1.f / 60) == 0);
        float second = Orientation_get_const(world, body)->radians;
        assert(fabsf(second - 2 * first) < .001f);
        Body_get(world, body)->allow_rotation = false;
        assert(physics_tick(live->runtime->physics, 1.f / 60) == 0);
        assert(fabsf(Orientation_get_const(world, body)->radians - second) < .001f);
    } else {
        assert(first == 0);
    }
    preview_world_free(live);
    ecs_world_free(world);
}

void test_material_gravity()
{
    authored_gravity();
    fluid_rotation(false);
    fluid_rotation(true);
}
