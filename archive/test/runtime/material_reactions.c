#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/material_fluid.h"
#include "runtime/materials.h"
#include "runtime/particles.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>

static fluid_contact contact_for(int kind, float body_speed, unsigned color)
{
    material_fluid *fluid = material_fluid_new(10, 4);
    material_fluid_set_contact_settings(fluid, (fluid_contact_settings){ 10, 20 });
    assert(material_fluid_add(
        fluid,
        (fluid_particle){ .position = { 0, 9 }, .mass = .01f, .radius = 2, .color = color, .kind = kind }
    ));
    fluid_region region = { -32, -32, 64, 64 };
    fluid_body body = { .velocity = { 0, body_speed }, .owner = 41 };
    fluid_obstacle obstacle = { .body = 0, .half_size = { 10, 10 } };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    int count;
    const fluid_contact *contacts = material_fluid_contacts(fluid, &count);
    assert(count == 1 && contacts);
    fluid_contact result = contacts[0];
    material_fluid_free(fluid);
    return result;
}

TEST_CASE(quiet_rest)
{
    material_fluid *fluid = material_fluid_new(10, 4);
    material_fluid_set_flow(fluid, (fluid_flow_settings){ 20, 2, .1f });
    material_fluid_set_contact_settings(fluid, (fluid_contact_settings){ 10, 20 });
    REQUIRE(material_fluid_add(
        fluid,
        (
            fluid_particle
        ){ .position = { 0, 9 }, .mass = .01f, .radius = 2, .color = 0x80c0ffff, .kind = FLUID_GAS }
    ));
    fluid_region region = { -32, -32, 64, 64 };
    fluid_body body = { 0 };
    fluid_obstacle obstacle = { .body = 0, .half_size = { 10, 10 } };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    int count;
    material_fluid_contacts(fluid, &count);
    REQUIRE(count == 0);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(bounded_contacts)
{
    enum
    {
        PARTICLES = 160
    };
    material_fluid *fluid = material_fluid_new(10, PARTICLES);
    material_fluid_set_contact_settings(fluid, (fluid_contact_settings){ 10, 20 });
    fluid_body bodies[PARTICLES] = { 0 };
    fluid_obstacle obstacles[PARTICLES] = { 0 };
    for (int i = 0; i < PARTICLES; i++) {
        float x = i * 24;
        bodies[i] = (fluid_body){ .position = { x + 9, 0 },
                                  .velocity = { 1080, 0 },
                                  .previous_position = { x - 9, 0 },
                                  .owner = (uint64_t)i + 1 };
        bodies[i].swept = true;
        obstacles[i] = (fluid_obstacle){ .body = i, .radius = 5 };
        REQUIRE(material_fluid_add(
            fluid,
            (fluid_particle){ .position = { x + 3, 0 },
                              .mass = .01f,
                              .radius = 1,
                              .color = 0x40c0ffff,
                              .kind = FLUID_GAS }
        ));
    }
    fluid_region region = { -20, -32, PARTICLES * 24.f + 40, 64 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, bodies, PARTICLES, obstacles, PARTICLES);
    int count;
    material_fluid_contacts(fluid, &count);
    REQUIRE(count == MATERIAL_FLUID_CONTACT_LIMIT);
    material_fluid_free(fluid);
    return true;
}

static fluid_contact box_sweep(float start, float end, float speed)
{
    material_fluid *fluid = material_fluid_new(10, 4);
    material_fluid_set_contact_settings(fluid, (fluid_contact_settings){ 10, 20 });
    assert(material_fluid_add(
        fluid,
        (
            fluid_particle
        ){ .position = { 0, 0 }, .mass = .01f, .radius = 1, .color = 0x80c0ffff, .kind = FLUID_GAS }
    ));
    fluid_region region = { -32, -32, 64, 64 };
    fluid_body body = { .position = { end, 0 },
                        .velocity = { speed, 0 },
                        .previous_position = { start, 0 },
                        .owner = 45,
                        .swept = true };
    fluid_obstacle obstacle = { .body = 0, .half_size = { 5, 5 } };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    int count;
    const fluid_contact *contacts = material_fluid_contacts(fluid, &count);
    assert(count == 1);
    fluid_contact result = contacts[0];
    material_fluid_free(fluid);
    return result;
}

TEST_CASE(swept_and_rotating_bodies)
{
    material_fluid *fluid = material_fluid_new(10, 4);
    material_fluid_set_contact_settings(fluid, (fluid_contact_settings){ 10, 20 });
    REQUIRE(material_fluid_add(
        fluid,
        (
            fluid_particle
        ){ .position = { 0, 0 }, .mass = .01f, .radius = 1, .color = 0x80c0ffff, .kind = FLUID_GAS }
    ));
    fluid_region region = { -32, -32, 64, 64 };
    fluid_body body = { .position = { 20, 0 },
                        .velocity = { 2400, 0 },
                        .previous_position = { -20, 0 },
                        .owner = 42,
                        .swept = true };
    fluid_obstacle obstacle = { .body = 0, .radius = 4 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    int count;
    const fluid_contact *contacts = material_fluid_contacts(fluid, &count);
    REQUIRE(count == 1 && contacts[0].closing_speed > 1000);
    REQUIRE(body.impulse.x < 0);
    const fluid_particle *particles = material_fluid_particles(fluid, &count);
    REQUIRE(count == 1 && particles[0].position.x > body.position.x + obstacle.radius);
    material_fluid_free(fluid);

    fluid_contact rightward = box_sweep(-20, 20, 2400);
    fluid_contact leftward = box_sweep(20, -20, -2400);
    REQUIRE(rightward.closing_speed > 2000 && rightward.normal.x > .99f);
    REQUIRE(leftward.closing_speed > 2000 && leftward.normal.x < -.99f);

    fluid = material_fluid_new(10, 4);
    material_fluid_set_contact_settings(fluid, (fluid_contact_settings){ 10, 20 });
    REQUIRE(material_fluid_add(
        fluid,
        (
            fluid_particle
        ){ .position = { 9, 9 }, .mass = .01f, .radius = 2, .color = 0x80c0ffff, .kind = FLUID_GAS }
    ));
    body = (fluid_body){ .angular_velocity = 30, .owner = 43 };
    obstacle = (fluid_obstacle){ .body = 0, .half_size = { 10, 10 } };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    contacts = material_fluid_contacts(fluid, &count);
    REQUIRE(count == 1 && contacts[0].closing_speed > 100);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(fast_particle_sweeps_stationary_wall)
{
    material_fluid *fluid = material_fluid_new(10, 4);
    material_fluid_set_contact_settings(fluid, (fluid_contact_settings){ 10, 20 });
    REQUIRE(material_fluid_add(
        fluid,
        (fluid_particle){ .position = { -20, 0 },
                          .velocity = { 4800, 0 },
                          .mass = .01f,
                          .radius = 1,
                          .color = 0x80c0ffff,
                          .kind = FLUID_GAS }
    ));
    fluid_region region = { -32, -32, 64, 64 };
    fluid_body body = { 0 };
    fluid_obstacle obstacle = { .body = 0, .half_size = { 5, 20 } };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    int count;
    const fluid_contact *contacts = material_fluid_contacts(fluid, &count);
    REQUIRE(count == 1 && contacts[0].closing_speed > 4000);
    const fluid_particle *particles = material_fluid_particles(fluid, &count);
    REQUIRE(count == 1 && particles[0].position.x < -6);
    REQUIRE(particles[0].velocity.x <= 0);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(huge_diagonal_particle_sweep)
{
    material_fluid *fluid = material_fluid_new(10, 4);
    material_fluid_set_contact_settings(fluid, (fluid_contact_settings){ 10, 20 });
    REQUIRE(material_fluid_add(
        fluid,
        (fluid_particle){ .position = { -1000, -1000 },
                          .velocity = { 240000, 240000 },
                          .mass = .01f,
                          .radius = 1,
                          .color = 0x80c0ffff,
                          .kind = FLUID_GAS }
    ));
    fluid_region region = { -1100, -1100, 2200, 2200 };
    fluid_body body = { 0 };
    fluid_obstacle obstacle = { .body = 0, .half_size = { 5, 5 } };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    int count;
    const fluid_contact *contacts = material_fluid_contacts(fluid, &count);
    REQUIRE(count == 1 && contacts[0].closing_speed > 200000);
    const fluid_particle *particles = material_fluid_particles(fluid, &count);
    REQUIRE(count == 1 && particles[0].position.x < -6);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(huge_sweep)
{
    material_fluid *fluid = material_fluid_new(10, 4);
    material_fluid_set_contact_settings(fluid, (fluid_contact_settings){ 10, 20 });
    REQUIRE(material_fluid_add(
        fluid,
        (
            fluid_particle
        ){ .position = { 0, 0 }, .mass = .01f, .radius = 1, .color = 0x80c0ffff, .kind = FLUID_GAS }
    ));
    fluid_region region = { -32, -32, 64, 64 };
    fluid_body body = { .position = { 1000000, 0 },
                        .velocity = { 120000000, 0 },
                        .previous_position = { -1000000, 0 },
                        .owner = 44,
                        .swept = true };
    fluid_obstacle obstacle = { .body = 0, .radius = 4 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    int count;
    const fluid_contact *contacts = material_fluid_contacts(fluid, &count);
    REQUIRE(count == 1 && contacts[0].closing_speed > 100000000.f);
    material_fluid_free(fluid);
    return true;
}

static int ecs_contact_spray_count(int maximum)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    ecs_entity settings = ecs_world_create_entity(world);
    MaterialFluidWorld config = { .chunk_size = 64,
                                  .buffer = 32,
                                  .kernel = 10,
                                  .particle_limit = 16,
                                  .gas_wake_speed = 10,
                                  .gas_wake_strength = 10,
                                  .gas_wake_count = maximum,
                                  .gas_wake_lifetime = .5f };
    assert(
        ecs_world_record_add(world, settings, COMPONENT_MaterialFluidWorld.id, &config, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(!materials_prepare(live->runtime->materials));
    assert(materials_emit(
        live->runtime->materials,
        (
            fluid_particle
        ){ .position = { 0, 9 }, .mass = .01f, .radius = 2, .color = 0x80c0ffff, .kind = FLUID_GAS }
    ));
    fluid_body body = { .velocity = { 0, 80 }, .owner = 48 };
    fluid_obstacle obstacle = { .body = 0, .half_size = { 10, 10 } };
    materials_tick(live->runtime->materials, 1.f / 60, &body, 1, &obstacle, 1);
    material_state state = materials_inspect(live->runtime->materials);
    assert(state.particles == 1 && state.active_particles == 1);
    int emitted = particles_count(live->runtime->particles);
    if (maximum) {
        assert(emitted > 0 && emitted <= maximum);
    } else {
        assert(emitted == 0);
    }
    preview_world_free(live);
    ecs_world_free(world);
    return emitted;
}

TEST_CASE(ecs_contact_sprays_are_transient)
{
    REQUIRE(ecs_contact_spray_count(4) > 0);
    REQUIRE(ecs_contact_spray_count(0) == 0);
    return true;
}

TEST_CASE(repeat_step_resets_contacts)
{
    material_fluid *fluid = material_fluid_new(10, 4);
    material_fluid_set_contact_settings(fluid, (fluid_contact_settings){ 10, 20 });
    REQUIRE(material_fluid_add(
        fluid,
        (
            fluid_particle
        ){ .position = { 0, 9 }, .mass = .01f, .radius = 2, .color = 0x80c0ffff, .kind = FLUID_GAS }
    ));
    fluid_region region = { -32, -32, 64, 64 };
    fluid_body body = { .velocity = { 0, 80 } };
    fluid_obstacle obstacle = { .body = 0, .half_size = { 10, 10 } };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    int count;
    material_fluid_contacts(fluid, &count);
    REQUIRE(count == 1);
    body.velocity = (fluid_vec){ 0 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    material_fluid_contacts(fluid, &count);
    REQUIRE(count == 0);
    material_fluid_free(fluid);
    return true;
}

static bool same_contact(fluid_contact a, fluid_contact b)
{
    return a.body == b.body && a.point.x == b.point.x && a.point.y == b.point.y &&
           a.normal.x == b.normal.x && a.normal.y == b.normal.y &&
           a.closing_speed == b.closing_speed && a.impulse == b.impulse &&
           a.mass == b.mass && a.emission == b.emission && a.color == b.color &&
           a.kind == b.kind && a.contacts == b.contacts;
}

TEST_CASE(contact_responses)
{
    fluid_contact moderate = contact_for(FLUID_GAS, 80, 0x80c0ffff);
    fluid_contact repeat = contact_for(FLUID_GAS, 80, 0x80c0ffff);
    fluid_contact strong = contact_for(FLUID_GAS, 160, 0x80c0ffff);
    REQUIRE(same_contact(moderate, repeat));
    REQUIRE(moderate.kind == FLUID_GAS && moderate.body == 41);
    REQUIRE(moderate.closing_speed >= 10 && moderate.mass > 0);
    REQUIRE(strong.closing_speed > moderate.closing_speed);
    REQUIRE(strong.impulse > moderate.impulse);
    fluid_contact liquid = contact_for(FLUID_LIQUID, 80, 0x22aaffff);
    REQUIRE(liquid.kind == FLUID_LIQUID && liquid.color == 0x22aaffff);
    REQUIRE(liquid.normal.y > .99f);
    return true;
}

void test_material_reactions()
{
    RUN_TEST_CASE(quiet_rest);
    RUN_TEST_CASE(contact_responses);
    RUN_TEST_CASE(fast_particle_sweeps_stationary_wall);
    RUN_TEST_CASE(huge_diagonal_particle_sweep);
    RUN_TEST_CASE(swept_and_rotating_bodies);
    RUN_TEST_CASE(huge_sweep);
    RUN_TEST_CASE(ecs_contact_sprays_are_transient);
    RUN_TEST_CASE(repeat_step_resets_contacts);
    RUN_TEST_CASE(bounded_contacts);
}
