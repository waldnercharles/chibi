#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/materials.h"
#include "runtime/particle_emitters.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>

static void add_component(
    ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *type,
    const void *value
)
{
    assert(ecs_world_record_add(world, entity, type->id, value, 0) == ECS_OK);
}

static preview_world *authored_world(ecs_world **world, int solver)
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
                                    .particle_limit = 16,
                                    .solver = solver,
                                    .mpm_cell_size = 2,
                                    .mpm_max_substeps = 32 };
    add_component(*world, config, &COMPONENT_MaterialFluidWorld, &settings);
    assert(ecs_world_apply_commands(*world) == ECS_OK);
    return live;
}

static ecs_entity add_source(ecs_world *world, bool emitter, int kind, const MaterialConstitutive *model)
{
    ecs_entity entity = ecs_world_create_entity(world);
    add_component(world, entity, &COMPONENT_Transform, &(Transform){ 0 });
    if (emitter) {
        ParticleEmitter style = particle_emitter_defaults();
        style.fluid = true;
        style.fluid_kind = kind;
        style.fluid_mass = 3;
        style.rate = 0;
        style.burst_count = 1;
        add_component(world, entity, &COMPONENT_ParticleEmitter, &style);
    } else {
        FluidVolume volume = { .kind = kind, .width = 1, .height = 1, .spacing = 4, .mass = 3 };
        add_component(world, entity, &COMPONENT_FluidVolume, &volume);
    }
    if (model) {
        add_component(world, entity, &COMPONENT_MaterialConstitutive, model);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

static void identity(fluid_particle p)
{
    assert(p.mass == 3 && p.deformation.volume == 1);
    assert(p.deformation.f[0] == 1 && p.deformation.f[1] == 0);
    assert(p.deformation.f[2] == 0 && p.deformation.f[3] == 1);
    for (int i = 0; i < 4; i++) {
        assert(p.deformation.affine[i] == 0);
    }
}

TEST_CASE(propagated_models)
{
    ecs_world *world;
    preview_world *live = authored_world(&world, MATERIAL_SOLVER_MPM);
    MaterialConstitutive volume_model = { 2, 80, 12 };
    add_source(world, false, FLUID_LIQUID, &volume_model);
    REQUIRE(!materials_prepare(live->runtime->materials));
    const material_fluid *fluid = materials_fluid(live->runtime->materials);
    REQUIRE(material_fluid_solver(fluid) == FLUID_SOLVER_MPM);
    MaterialConstitutive emitter_model = { 4, 160, 24 };
    ecs_entity source = add_source(world, true, FLUID_LIQUID, &emitter_model);
    particle_emitters *emitters = particle_emitters_new(NULL);
    particle_emitters_bind_materials(emitters, live->runtime->materials);
    char error[256] = { 0 };
    REQUIRE(particle_emitters_collect(emitters, world, NULL, 0, error, sizeof(error)));
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    REQUIRE(count == 2);
    identity(p[0]);
    identity(p[1]);
    REQUIRE(
        p[0].constitutive.density == 2 && p[0].constitutive.bulk_modulus == 80 &&
        p[0].constitutive.shear_modulus == 12
    );
    REQUIRE(
        p[1].constitutive.density == 4 && p[1].constitutive.bulk_modulus == 160 &&
        p[1].constitutive.shear_modulus == 24
    );
    uint64_t before = materials_inspect(live->runtime->materials).hash;
    MaterialConstitutive *
        model = ecs_component_data(world, source, COMPONENT_MaterialConstitutive.id);
    model->bulk_modulus = NAN;
    REQUIRE(!particle_emitters_collect(emitters, world, NULL, .1, error, sizeof(error)));
    REQUIRE(error[0] && materials_inspect(live->runtime->materials).hash == before);
    particle_emitters_free(emitters);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

static bool rejected_model(bool emitter, const MaterialConstitutive *model)
{
    ecs_world *world;
    preview_world *live = authored_world(&world, MATERIAL_SOLVER_MPM);
    if (emitter) {
        REQUIRE(!materials_prepare(live->runtime->materials));
    }
    add_source(world, emitter, FLUID_LIQUID, model);
    if (emitter) {
        particle_emitters *emitters = particle_emitters_new(NULL);
        particle_emitters_bind_materials(emitters, live->runtime->materials);
        char error[256] = { 0 };
        REQUIRE(!particle_emitters_collect(emitters, world, NULL, 0, error, sizeof(error)));
        REQUIRE(error[0]);
        particle_emitters_free(emitters);
    } else {
        REQUIRE(materials_prepare(live->runtime->materials));
    }
    int count;
    material_fluid_particles(materials_fluid(live->runtime->materials), &count);
    REQUIRE(count == 0);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

static bool optional_model(int solver, int kind)
{
    ecs_world *world;
    preview_world *live = authored_world(&world, solver);
    add_source(world, false, kind, NULL);
    REQUIRE(!materials_prepare(live->runtime->materials));
    add_source(world, true, kind, NULL);
    particle_emitters *emitters = particle_emitters_new(NULL);
    particle_emitters_bind_materials(emitters, live->runtime->materials);
    char error[256] = { 0 };
    REQUIRE(particle_emitters_collect(emitters, world, NULL, 0, error, sizeof(error)));
    int count;
    const fluid_particle
        *p = material_fluid_particles(materials_fluid(live->runtime->materials), &count);
    REQUIRE(count == 2 && p[0].kind == kind && p[1].kind == kind);
    identity(p[0]);
    identity(p[1]);
    REQUIRE(p[0].constitutive.density == 0 && p[1].constitutive.density == 0);
    particle_emitters_free(emitters);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

TEST_CASE(rejected_models)
{
    MaterialConstitutive invalid[] = { { 0, 80, 0 },  { NAN, 80, 0 },
                                       { 1, 0, 0 },   { 1, INFINITY, 0 },
                                       { 1, 80, -1 }, { 1, 80, NAN } };
    for (int emitter = 0; emitter < 2; emitter++) {
        REQUIRE(rejected_model(emitter, NULL));
        for (int i = 0; i < (int)(sizeof(invalid) / sizeof(invalid[0])); i++) {
            REQUIRE(rejected_model(emitter, &invalid[i]));
        }
    }
    return true;
}

TEST_CASE(optional_models)
{
    REQUIRE(optional_model(MATERIAL_SOLVER_MPM, FLUID_GAS));
    REQUIRE(optional_model(MATERIAL_SOLVER_DDR, FLUID_LIQUID));
    return true;
}

void test_material_constitutive()
{
    RUN_TEST_CASE(propagated_models);
    RUN_TEST_CASE(rejected_models);
    RUN_TEST_CASE(optional_models);
}
