#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/material_fluid.h"
#include "runtime/materials.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>

static double energy(const fluid_particle *particles, int count)
{
    double sum = 0;
    for (int i = 0; i < count; i++) {
        sum += (double)particles[i].mass * particles[i].thermal.capacity *
               particles[i].heat;
    }
    return sum;
}

TEST_CASE(appearance_preserves_physics)
{
    material_fluid *surface = material_fluid_new(10, 32);
    material_fluid *grains = material_fluid_new(10, 32);
    for (int i = 0; i < 32; i++) {
        fluid_particle p = { .position = { (i % 8) * 3 - 12, (i / 8) * 3 - 6 },
                             .velocity = { 8, 2 },
                             .mass = 1 + i % 3,
                             .radius = 1,
                             .kind = FLUID_LIQUID,
                             .heat = i == 0 ? 4 : 0,
                             .thermal = { 2, 20 } };
        REQUIRE(material_fluid_add(surface, p));
        p.grain_radius = .25f;
        REQUIRE(material_fluid_add(grains, p));
    }
    fluid_region region = { -128, -128, 256, 256 };
    fluid_obstacle wall = { .half_size = { 2, 64 } };
    for (int tick = 0; tick < 120; tick++) {
        fluid_body a = { .position = { 24, 0 } }, b = a;
        material_fluid_tick(surface, 1.f / 60, 64, &region, 1, &a, 1, &wall, 1);
        material_fluid_tick(grains, 1.f / 60, 64, &region, 1, &b, 1, &wall, 1);
        int count, other_count;
        const fluid_particle *ordinary = material_fluid_particles(surface, &count);
        const fluid_particle *fine = material_fluid_particles(grains, &other_count);
        REQUIRE(count == 32 && count == other_count);
        for (int i = 0; i < count; i++) {
            REQUIRE(
                ordinary[i].position.x == fine[i].position.x &&
                ordinary[i].position.y == fine[i].position.y &&
                ordinary[i].velocity.x == fine[i].velocity.x &&
                ordinary[i].velocity.y == fine[i].velocity.y &&
                ordinary[i].heat == fine[i].heat &&
                ordinary[i].radius == fine[i].radius
            );
        }
        REQUIRE(energy(ordinary, count) == energy(fine, count));
        REQUIRE(fabs(energy(fine, count) - 8) < .0001);
        REQUIRE(a.impulse.x == b.impulse.x && a.impulse.y == b.impulse.y);
    }
    material_fluid_free(surface);
    material_fluid_free(grains);
    return true;
}

static void add_component(
    ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *type,
    const void *value
)
{
    assert(ecs_world_record_add(world, entity, type->id, value, 0) == ECS_OK);
}

TEST_CASE(authored_detail)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    ecs_entity config = ecs_world_create_entity(world);
    MaterialFluidWorld settings = { .chunk_size = 64,
                                    .buffer = 32,
                                    .kernel = 10,
                                    .particle_limit = 16 };
    add_component(world, config, &COMPONENT_MaterialFluidWorld, &settings);
    ecs_entity fine = { 0 };
    for (int i = 0; i < 2; i++) {
        ecs_entity entity = ecs_world_create_entity(world);
        FluidVolume volume = { .kind = FLUID_LIQUID,
                               .width = 1,
                               .height = 1,
                               .spacing = 4,
                               .mass = 1,
                               .grain_radius = i ? .25f : 0 };
        add_component(world, entity, &COMPONENT_Transform, &(Transform){ i * 8, 0 });
        add_component(world, entity, &COMPONENT_FluidVolume, &volume);
        if (i) {
            fine = entity;
        }
    }
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!materials_prepare(live->runtime->materials));
    int count;
    const fluid_particle
        *p = material_fluid_particles(materials_fluid(live->runtime->materials), &count);
    REQUIRE(count == 2 && p[0].grain_radius == 0 && p[1].grain_radius == .25f);
    REQUIRE(p[0].radius == p[1].radius && p[0].mass == p[1].mass);
    uint64_t before = materials_inspect(live->runtime->materials).hash;
    FluidVolume *volume = ecs_component_data(world, fine, COMPONENT_FluidVolume.id);
    float invalid[] = { -1, NAN, INFINITY, 11 };
    for (int i = 0; i < 4; i++) {
        volume->grain_radius = invalid[i];
        REQUIRE(materials_prepare(live->runtime->materials));
        REQUIRE(materials_inspect(live->runtime->materials).hash == before);
    }
    volume->grain_radius = .25f;
    REQUIRE(!materials_prepare(live->runtime->materials));
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

TEST_CASE(burnout_becomes_smoke)
{
    material_fluid *fluid = material_fluid_new(10, 1);
    material_fluid_set_reaction(
        fluid,
        (fluid_reaction){ .ignition = 1,
                          .burn_rate = 60,
                          .heat_release = 2,
                          .smoke_lifetime = 2,
                          .extinction = .5f }
    );
    REQUIRE(material_fluid_add(
        fluid,
        (fluid_particle){ .mass = 2,
                          .radius = 1,
                          .kind = FLUID_LIQUID,
                          .heat = 2,
                          .fuel = .5f,
                          .grain_radius = .25f,
                          .thermal = { 3, 20 } }
    ));
    fluid_region region = { -32, -32, 64, 64 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    REQUIRE(count == 1 && p[0].mass == 2 && p[0].thermal.capacity == 3);
    REQUIRE(p[0].kind == FLUID_GAS && p[0].smoke && !p[0].burning);
    REQUIRE(p[0].fuel == 0 && p[0].grain_radius == 0 && p[0].lifetime == 2);
    material_fluid_free(fluid);
    return true;
}

void test_material_detail()
{
    RUN_TEST_CASE(appearance_preserves_physics);
    RUN_TEST_CASE(authored_detail);
    RUN_TEST_CASE(burnout_becomes_smoke);
}
