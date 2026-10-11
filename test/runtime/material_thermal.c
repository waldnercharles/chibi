#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/material_fluid.h"
#include "runtime/particle_emitters.h"
#include "runtime/task_pool.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static fluid_particle parcel(float x, float y, float heat, float mass, float capacity)
{
    return (fluid_particle){ .position = { x, y },
                             .mass = mass,
                             .radius = .5f,
                             .kind = FLUID_GAS,
                             .heat = heat,
                             .thermal = { capacity, 1000 } };
}

static void step(material_fluid *fluid)
{
    fluid_region region = { -128, -128, 256, 256 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
}

static double energy(const fluid_particle *particles, int count)
{
    double total = 0;
    for (int i = 0; i < count; i++) {
        total += (double)particles[i].mass * particles[i].thermal.capacity *
                 particles[i].heat;
    }
    return total;
}

TEST_CASE(pair_exchange)
{
    material_fluid *fluid = material_fluid_new(10, 2);
    REQUIRE(material_fluid_add(fluid, parcel(-2, 0, 10, 2, 3)));
    REQUIRE(material_fluid_add(fluid, parcel(2, 0, 0, 1, 2)));
    step(fluid);
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    REQUIRE(count == 2 && p[0].heat < 10 && p[1].heat > 0);
    REQUIRE(p[0].heat >= 0 && p[1].heat <= 10);
    REQUIRE(fabs(energy(p, count) - 60) < .0001);
    /* Exchanging heat does not introduce vertical gravity or buoyancy. */
    REQUIRE(p[0].velocity.y == 0 && p[1].velocity.y == 0);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(uniform_bulk_interface)
{
    for (int blocked = 0; blocked < 2; blocked++) {
        material_fluid *fluid = material_fluid_new(5, 9);
        for (int i = 0; i < 9; i++) {
            REQUIRE(material_fluid_add(fluid, parcel((i - 4) * 4, 0, i <= 4 ? 10 : 0, 1, 1)));
        }
        fluid_region region = { -128, -128, 256, 256 };
        fluid_body body = { .position = { 2, 0 } };
        fluid_obstacle wall = { .half_size = { .1f, 10 } };
        material_fluid_tick(
            fluid,
            1.f / 60,
            64,
            &region,
            1,
            blocked ? &body : NULL,
            blocked,
            blocked ? &wall : NULL,
            blocked
        );
        int count;
        const fluid_particle *p = material_fluid_particles(fluid, &count);
        REQUIRE(count == 9 && fabs(energy(p, count) - 50) < .00001);
        for (int i = 0; i < count; i++) {
            float expected = i <= 4 ? 10 : 0;
            if (!blocked && (i == 4 || i == 5)) {
                /* Both interface rates include their equal-heat bulk neighbor.
                 * Omitting that conductance would incorrectly exchange 6.67. */
                expected = 5;
            }
            REQUIRE(p[i].heat == expected);
        }
        material_fluid_free(fluid);
    }
    return true;
}

TEST_CASE(dense_exchange)
{
    enum
    {
        COUNT = 64
    };
    material_fluid *fluid = material_fluid_new(10, COUNT);
    double before = 0;
    for (int i = 0; i < COUNT; i++) {
        fluid_particle
            p = parcel((i % 8) * .2f, (i / 8) * .2f, i == 0 ? 20 : 0, 1 + i % 3, 1 + i % 5);
        before += p.mass * p.thermal.capacity * p.heat;
        REQUIRE(material_fluid_add(fluid, p));
    }
    step(fluid);
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    REQUIRE(count == COUNT && p[0].heat < 20 && p[1].heat > 0);
    for (int i = 0; i < count; i++) {
        REQUIRE(isfinite(p[i].heat) && p[i].heat >= 0 && p[i].heat <= 20);
    }
    REQUIRE(fabs(energy(p, count) - before) < .001);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(coincident_exchange)
{
    material_fluid *fluid = material_fluid_new(10, 14);
    for (int i = 0; i < 14; i++) {
        REQUIRE(material_fluid_add(fluid, parcel(0, 0, i == 0 ? 20 : 0, 1, 1)));
    }
    step(fluid);
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    REQUIRE(count == 14 && p[0].heat < 20 && p[1].heat > 0);
    for (int i = 0; i < count; i++) {
        REQUIRE(p[i].heat >= 0 && p[i].heat <= 20);
    }
    REQUIRE(fabs(energy(p, count) - 20) < .0001);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(wall_and_opening)
{
    material_fluid *fluid = material_fluid_new(10, 2);
    REQUIRE(material_fluid_add(fluid, parcel(-2, 0, 10, 1, 1)));
    REQUIRE(material_fluid_add(fluid, parcel(2, 0, 0, 1, 1)));
    fluid_region region = { -128, -128, 256, 256 };
    fluid_body body = { 0 };
    fluid_obstacle wall = { .half_size = { .1f, 20 } };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &wall, 1);
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    REQUIRE(p[0].heat == 10 && p[1].heat == 0);
    /* A hot solid still blocks conduction through its occupied interior. */
    body.heat = 10;
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &wall, 1);
    p = material_fluid_particles(fluid, &count);
    REQUIRE(p[0].heat == 10 && p[1].heat == 0);
    /* Removing the same collision boundary permits interaction next tick. */
    step(fluid);
    p = material_fluid_particles(fluid, &count);
    REQUIRE(p[0].heat < 10 && p[1].heat > 0);
    REQUIRE(fabs(energy(p, count) - 10) < .0001);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(grazing_wall_conservation)
{
    /* Reverse particle order at a tangent obstacle boundary. Whether a tangent
     * counts as blocked, both ends must agree and conserve exchanged heat. */
    float hot_heat = 0, cold_heat = 0;
    for (int reverse = 0; reverse < 2; reverse++) {
        material_fluid *fluid = material_fluid_new(10, 2);
        fluid_particle hot = parcel(-2, 2.2f, 10, 2, 3),
                       cold = parcel(2, -1.8f, 0, 1, 2);
        REQUIRE(material_fluid_add(fluid, reverse ? cold : hot));
        REQUIRE(material_fluid_add(fluid, reverse ? hot : cold));
        fluid_region region = { -128, -128, 256, 256 };
        fluid_body body = { 0 };
        fluid_obstacle wall = { .half_size = { .1f, .1f } };
        material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &wall, 1);
        int count;
        const fluid_particle *p = material_fluid_particles(fluid, &count);
        REQUIRE(count == 2 && fabs(energy(p, count) - 60) < .0001);
        REQUIRE(p[reverse].heat >= 0 && p[reverse].heat <= 10);
        REQUIRE(p[!reverse].heat >= 0 && p[!reverse].heat <= 10);
        if (reverse) {
            REQUIRE(p[reverse].heat == hot_heat && p[!reverse].heat == cold_heat);
        }
        hot_heat = p[reverse].heat;
        cold_heat = p[!reverse].heat;
        material_fluid_free(fluid);
    }
    return true;
}

TEST_CASE(dormant_isolation)
{
    material_fluid *fluid = material_fluid_new(10, 2);
    REQUIRE(material_fluid_add(fluid, parcel(-2, 0, 10, 1, 1)));
    REQUIRE(material_fluid_add(fluid, parcel(2, 0, 0, 1, 1)));
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    fluid_particle sleeper = p[0];
    fluid_region region = { 0, 0, 64, 64 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
    p = material_fluid_particles(fluid, &count);
    REQUIRE(material_fluid_active_count(fluid) == 1);
    REQUIRE(!memcmp(&sleeper, &p[0], sizeof sleeper) && p[1].heat == 0);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(quench_and_reignite)
{
    material_fluid *fluid = material_fluid_new(10, 2);
    fluid_reaction reaction = { .ignition = 1,
                                .burn_rate = .5f,
                                .heat_release = 2,
                                .smoke_lifetime = 1,
                                .flame_color = 0xff8000ff,
                                .flame_emission = 4,
                                .extinction = .8f };
    material_fluid_set_reaction(fluid, reaction);
    fluid_particle fuel = parcel(-2, 0, 2, 1, 1);
    fuel.fuel = 1;
    fuel.color = 0x4080ffff;
    fuel.emission = .25f;
    fuel.glow = .1f;
    REQUIRE(material_fluid_add(fluid, fuel));
    step(fluid);
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    REQUIRE(p[0].burning && p[0].color == reaction.flame_color);
    float remaining = p[0].fuel;
    fluid_particle coolant = parcel(2, 0, 0, 20, 1);
    coolant.kind = FLUID_LIQUID;
    coolant.lifetime = .025f;
    REQUIRE(material_fluid_add(fluid, coolant));
    step(fluid);
    p = material_fluid_particles(fluid, &count);
    REQUIRE(!p[0].burning && p[0].fuel == remaining && p[0].heat < reaction.extinction);
    REQUIRE(p[0].color == fuel.color && p[0].emission == fuel.emission && p[0].glow == fuel.glow);
    step(fluid);
    p = material_fluid_particles(fluid, &count);
    REQUIRE(count == 1 && !p[0].burning && p[0].fuel == remaining);
    fluid_body igniter = { .position = p[0].position, .heat = 2 };
    fluid_region region = { -128, -128, 256, 256 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &igniter, 1, NULL, 0);
    p = material_fluid_particles(fluid, &count);
    REQUIRE(p[0].burning && p[0].fuel < remaining && p[0].color == reaction.flame_color);
    material_fluid_free(fluid);
    return true;
}

typedef struct
{
    const task_system *pool;
    int submissions;
} thermal_executor;

static void *enqueue_thermal(task_range *run, int count, int minimum, void *context, void *user)
{
    thermal_executor *executor = user;
    executor->submissions++;
    return executor->pool->enqueue(run, count, minimum, context, executor->pool->user);
}

static void finish_thermal(void *task, void *user)
{
    thermal_executor *executor = user;
    executor->pool->finish(task, executor->pool->user);
}

TEST_CASE(parallel_exchange)
{
    enum
    {
        SIDE = 48,
        COUNT = SIDE * SIDE
    };
    for (int pattern = 0; pattern < 2; pattern++) {
        material_fluid *serial = material_fluid_new(10, COUNT);
        material_fluid *parallel = material_fluid_new(10, COUNT);
        for (int i = 0; i < COUNT; i++) {
            fluid_particle p = parcel(
                (i % SIDE) * 2,
                (i / SIDE) * 2,
                (pattern ? i % SIDE < SIDE / 2 : i % 3 == 0) ? 10 : 0,
                1 + i % 3,
                1 + i % 5
            );
            REQUIRE(material_fluid_add(serial, p));
            REQUIRE(material_fluid_add(parallel, p));
        }
        task_pool *pool = task_pool_acquire_workers(4);
        thermal_executor executor = { .pool = task_pool_tasks(pool) };
        task_system tasks = { .workers = executor.pool->workers,
                              .user = &executor,
                              .enqueue = enqueue_thermal,
                              .finish = finish_thermal };
        material_fluid_set_tasks(parallel, &tasks);
        for (int i = 0; i < 5; i++) {
            step(serial);
            task_pool_begin(pool, true);
            step(parallel);
            task_pool_end(pool);
        }
        int count, other_count;
        const fluid_particle *a = material_fluid_particles(serial, &count);
        const fluid_particle *b = material_fluid_particles(parallel, &other_count);
        if (tasks.workers > 1) {
            REQUIRE(executor.submissions > 0);
        } else {
            REQUIRE(executor.submissions == 0);
            puts("Thermal parallel scheduling skipped: worker backend unavailable; serial executor parity checked.");
        }
        REQUIRE(count == other_count && !memcmp(a, b, (size_t)count * sizeof *a));
        material_fluid_free(serial);
        material_fluid_free(parallel);
        task_pool_release(pool);
    }
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

static preview_world *thermal_world(ecs_world **world)
{
    *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(*world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(*world);
    ecs_entity config = ecs_world_create_entity(*world);
    MaterialFluidWorld settings = { .chunk_size = 64,
                                    .buffer = 32,
                                    .kernel = 10,
                                    .particle_limit = 16 };
    add_component(*world, config, &COMPONENT_MaterialFluidWorld, &settings);
    assert(ecs_world_apply_commands(*world) == ECS_OK);
    return live;
}

static ecs_entity add_volume(ecs_world *world, float x, float heat, MaterialThermal thermal)
{
    ecs_entity entity = ecs_world_create_entity(world);
    FluidVolume volume = { .kind = FLUID_LIQUID,
                           .width = 1,
                           .height = 1,
                           .spacing = 4,
                           .mass = 1,
                           .color = 0x4080ffff };
    add_component(world, entity, &COMPONENT_Transform, &(Transform){ x, 0 });
    add_component(world, entity, &COMPONENT_FluidVolume, &volume);
    add_component(world, entity, &COMPONENT_Igniter, &(Igniter){ heat });
    add_component(world, entity, &COMPONENT_MaterialThermal, &thermal);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

TEST_CASE(authored_volumes)
{
    ecs_world *world;
    preview_world *live = thermal_world(&world);
    ecs_entity hot = add_volume(world, -2, 10, (MaterialThermal){ 2, 1000 });
    add_volume(world, 2, 0, (MaterialThermal){ 4, 1000 });
    REQUIRE(!materials_prepare(live->runtime->materials));
    int count;
    const fluid_particle
        *p = material_fluid_particles(materials_fluid(live->runtime->materials), &count);
    REQUIRE(count == 2 && p[0].thermal.capacity == 2 && p[1].thermal.capacity == 4);
    REQUIRE(p[0].thermal.conductivity == 1000 && p[1].thermal.conductivity == 1000);
    /* A moving inspection body activates the volume without combustion or a camera. */
    fluid_body body = { .velocity = { 2, 0 } };
    materials_tick(live->runtime->materials, 1.f / 60, &body, 1, NULL, 0);
    p = material_fluid_particles(materials_fluid(live->runtime->materials), &count);
    REQUIRE(p[0].heat < 10 && p[1].heat > 0 && fabs(energy(p, count) - 20) < .0001);
    uint64_t hash = materials_inspect(live->runtime->materials).hash;
    MaterialThermal_get(world, hot)->capacity = 0;
    REQUIRE(materials_prepare(live->runtime->materials));
    REQUIRE(materials_inspect(live->runtime->materials).hash == hash);
    MaterialThermal_get(world, hot)->capacity = 2;
    MaterialThermal_get(world, hot)->conductivity = -1;
    REQUIRE(materials_prepare(live->runtime->materials));
    MaterialThermal_get(world, hot)->conductivity = 0;
    REQUIRE(!materials_prepare(live->runtime->materials));
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

TEST_CASE(authored_emitter)
{
    ecs_world *world;
    preview_world *live = thermal_world(&world);
    REQUIRE(!materials_prepare(live->runtime->materials));
    ecs_entity entity = ecs_world_create_entity(world);
    ParticleEmitter style = particle_emitter_defaults();
    style.fluid = true;
    style.fluid_kind = FLUID_LIQUID;
    style.fluid_mass = 3;
    style.rate = 0;
    style.burst_count = 1;
    add_component(world, entity, &COMPONENT_Transform, &(Transform){ 0 });
    add_component(world, entity, &COMPONENT_ParticleEmitter, &style);
    add_component(world, entity, &COMPONENT_MaterialThermal, &(MaterialThermal){ 7, 12 });
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    particle_emitters *emitters = particle_emitters_new(NULL);
    particle_emitters_bind_materials(emitters, live->runtime->materials);
    char error[256];
    REQUIRE(particle_emitters_collect(emitters, world, NULL, 0, error, sizeof error));
    int count;
    const fluid_particle
        *p = material_fluid_particles(materials_fluid(live->runtime->materials), &count);
    REQUIRE(
        count == 1 && p[0].mass == 3 && p[0].thermal.capacity == 7 &&
        p[0].thermal.conductivity == 12
    );
    MaterialThermal_get(world, entity)->capacity = 0;
    REQUIRE(!particle_emitters_collect(emitters, world, NULL, .1, error, sizeof error));
    material_fluid_particles(materials_fluid(live->runtime->materials), &count);
    REQUIRE(count == 1);
    particle_emitters_free(emitters);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

static uint64_t thermal_hash(fluid_thermal thermal)
{
    ecs_world *world;
    preview_world *live = thermal_world(&world);
    assert(!materials_prepare(live->runtime->materials));
    fluid_particle p = parcel(0, 0, 2, 1, 1);
    p.thermal = thermal;
    assert(materials_emit(live->runtime->materials, p));
    uint64_t hash = materials_inspect(live->runtime->materials).hash;
    assert(materials_inspect(live->runtime->materials).hash == hash);
    preview_world_free(live);
    ecs_world_free(world);
    return hash;
}

TEST_CASE(thermal_state_hash)
{
    uint64_t initial = thermal_hash((fluid_thermal){ 1, 2 });
    REQUIRE(initial == thermal_hash((fluid_thermal){ 1, 2 }));
    REQUIRE(initial != thermal_hash((fluid_thermal){ 2, 2 }));
    REQUIRE(initial != thermal_hash((fluid_thermal){ 1, 3 }));
    return true;
}

void test_material_thermal()
{
    RUN_TEST_CASE(pair_exchange);
    RUN_TEST_CASE(uniform_bulk_interface);
    RUN_TEST_CASE(dense_exchange);
    RUN_TEST_CASE(coincident_exchange);
    RUN_TEST_CASE(wall_and_opening);
    RUN_TEST_CASE(grazing_wall_conservation);
    RUN_TEST_CASE(dormant_isolation);
    RUN_TEST_CASE(quench_and_reignite);
    RUN_TEST_CASE(parallel_exchange);
    RUN_TEST_CASE(authored_volumes);
    RUN_TEST_CASE(authored_emitter);
    RUN_TEST_CASE(thermal_state_hash);
}
