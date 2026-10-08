#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/material_fluid.h"
#include "runtime/materials.h"

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

static void appearance_preserves_physics()
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
        assert(material_fluid_add(surface, p));
        p.grain_radius = .25f;
        assert(material_fluid_add(grains, p));
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
        assert(count == 32 && count == other_count);
        for (int i = 0; i < count; i++) {
            assert(
                ordinary[i].position.x == fine[i].position.x &&
                ordinary[i].position.y == fine[i].position.y &&
                ordinary[i].velocity.x == fine[i].velocity.x &&
                ordinary[i].velocity.y == fine[i].velocity.y &&
                ordinary[i].heat == fine[i].heat &&
                ordinary[i].radius == fine[i].radius
            );
        }
        assert(energy(ordinary, count) == energy(fine, count));
        assert(fabs(energy(fine, count) - 8) < .0001);
        assert(a.impulse.x == b.impulse.x && a.impulse.y == b.impulse.y);
    }
    material_fluid_free(surface);
    material_fluid_free(grains);
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

static void authored_detail()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_preview_world *live = game_preview_world_new(world);
    ecs_entity config = ecs_world_create_entity(world);
    MaterialFluidWorld settings = { .chunk_size = 64,
                                    .buffer = 32,
                                    .kernel = 10,
                                    .particle_limit = 16 };
    add_component(world, config, &GAME_COMPONENT_MaterialFluidWorld, &settings);
    ecs_entity fine = { 0 };
    for (int i = 0; i < 2; i++) {
        ecs_entity entity = ecs_world_create_entity(world);
        GameFluidVolume volume = { .kind = FLUID_LIQUID,
                                   .width = 1,
                                   .height = 1,
                                   .spacing = 4,
                                   .mass = 1,
                                   .grain_radius = i ? .25f : 0 };
        add_component(world, entity, &GAME_COMPONENT_GameTransform, &(GameTransform){ i * 8, 0 });
        add_component(world, entity, &GAME_COMPONENT_GameFluidVolume, &volume);
        if (i) {
            fine = entity;
        }
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(!game_materials_prepare(live->runtime->materials));
    int count;
    const fluid_particle *p = material_fluid_particles(
        game_materials_fluid(live->runtime->materials),
        &count
    );
    assert(count == 2 && p[0].grain_radius == 0 && p[1].grain_radius == .25f);
    assert(p[0].radius == p[1].radius && p[0].mass == p[1].mass);
    uint64_t before = game_materials_inspect(live->runtime->materials).hash;
    GameFluidVolume
        *volume = ecs_component_data(world, fine, GAME_COMPONENT_GameFluidVolume.id);
    float invalid[] = { -1, NAN, INFINITY, 11 };
    for (int i = 0; i < 4; i++) {
        volume->grain_radius = invalid[i];
        assert(game_materials_prepare(live->runtime->materials));
        assert(game_materials_inspect(live->runtime->materials).hash == before);
    }
    volume->grain_radius = .25f;
    assert(!game_materials_prepare(live->runtime->materials));
    game_preview_world_free(live);
    ecs_world_free(world);
}

static void burnout_becomes_smoke()
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
    assert(material_fluid_add(
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
    assert(count == 1 && p[0].mass == 2 && p[0].thermal.capacity == 3);
    assert(p[0].kind == FLUID_GAS && p[0].smoke && !p[0].burning);
    assert(p[0].fuel == 0 && p[0].grain_radius == 0 && p[0].lifetime == 2);
    material_fluid_free(fluid);
}

void test_material_detail()
{
    appearance_preserves_physics();
    authored_detail();
    burnout_becomes_smoke();
}
