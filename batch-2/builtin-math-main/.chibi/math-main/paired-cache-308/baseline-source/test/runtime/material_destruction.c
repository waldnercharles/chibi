#include "runtime/material_destruction.h"

#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/material_debris.h"
#include "runtime/material_ecs.h"
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/runtime/material_fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST_CASE(physical_destruction)
{
    /* A second, noncombat consumer: a movable probe and a cuttable slab. */
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    ecs_entity settings = ecs_world_create_entity(world);
    material_test_component(
        world,
        settings,
        &COMPONENT_MaterialDestructionWorld,
        &(MaterialDestructionWorld){ 8 }
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    ecs_entity slab = ecs_world_create_entity(world);
    material_test_component(world, slab, &COMPONENT_Transform, &(Transform){ 0 });
    material_test_component(
        world,
        slab,
        &COMPONENT_Collider,
        &(Collider){ .shape = COLLIDER_BOX, .width = 64, .height = 16, .category = 8, .mask = 1 }
    );
    material_test_component(
        world,
        slab,
        &COMPONENT_Destructible,
        &(Destructible){ .cell_size = 2, .min_fragment_cells = 4 }
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    ray_hit hit;
    REQUIRE(
        physics_raycast(
            world,
            live->runtime->entities,
            (CF_V2){ 0, -30 },
            (CF_V2){ 0, 30 },
            8,
            (ecs_entity){ 0 },
            false,
            &hit
        ) == 0
    );
    REQUIRE(hit.found);
    materials_damage(live->runtime->materials, slab, (fluid_vec){ 0, 0 }, 11, 1, 0, (material_impact){ 0 });
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    REQUIRE(ecs_world_live_count(world) == 3); /* settings, core, detached body */
    REQUIRE(
        physics_raycast(
            world,
            live->runtime->entities,
            (CF_V2){ 0, -30 },
            (CF_V2){ 0, 30 },
            8,
            (ecs_entity){ 0 },
            false,
            &hit
        ) == 0
    );
    REQUIRE(!hit.found);
    REQUIRE(
        physics_raycast(
            world,
            live->runtime->entities,
            (CF_V2){ 24, -30 },
            (CF_V2){ 24, 30 },
            8,
            (ecs_entity){ 0 },
            false,
            &hit
        ) == 0
    );
    REQUIRE(hit.found && Body_get_const(world, hit.entity)->type == BODY_DYNAMIC);

    ecs_entity probe = ecs_world_create_entity(world);
    material_test_component(world, probe, &COMPONENT_Transform, &(Transform){ 0, -24 });
    material_test_component(world, probe, &COMPONENT_Velocity, &(Velocity){ 0, 2880 });
    material_test_component(
        world,
        probe,
        &COMPONENT_Body,
        &(Body){ .type = BODY_KINEMATIC, .continuous = true }
    );
    material_test_component(
        world,
        probe,
        &COMPONENT_Collider,
        &(Collider){ .radius = 1, .category = 1, .mask = 8, .sensor = true }
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    REQUIRE(Transform_get_const(world, probe)->y > 20);
    *Transform_get(world, probe) = (Transform){ 24, -24 };
    REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    REQUIRE(Transform_get_const(world, probe)->y < -7);
    /* Ordinary overlap callbacks must find individual compound pieces, keep
     * the carved opening empty, and deduplicate multiple pieces per body. */
    Body_get(world, probe)->continuous = false;
    *Velocity_get(world, probe) = (Velocity){ 0 };
    *Transform_get(world, probe) = (Transform){ 24, 0 };
    physics_set_profiling(live->runtime->physics, true);
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    REQUIRE(physics_last_profile(live->runtime->physics)->pairs == 1);
    *Transform_get(world, probe) = (Transform){ 0 };
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    REQUIRE(physics_last_profile(live->runtime->physics)->pairs == 0);
    Collider *bar = Collider_get(world, probe);
    bar->shape = COLLIDER_BOX;
    bar->width = 60;
    bar->height = 2;
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    REQUIRE(physics_last_profile(live->runtime->physics)->pairs == 2);
    /* Destruction and fragment masks survive fluid sleep, without catch-up. */
    const material_mask *mask = materials_mask(live->runtime->materials, slab);
    int cells = mask->count;
    for (int i = 0; i < 30; i++) {
        REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    }
    REQUIRE(mask->count == cells);
    physics_reset(live->runtime->physics);
    materials_reset(live->runtime->materials);
    REQUIRE(!materials_mask(live->runtime->materials, slab));
    REQUIRE(ecs_world_live_count(world) == 3); /* detached fragment was removed */
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

TEST_CASE(finite_drive)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    ecs_entity entity = ecs_world_create_entity(world);
    material_test_component(world, entity, &COMPONENT_Transform, &(Transform){ 0 });
    material_test_component(world, entity, &COMPONENT_Velocity, &(Velocity){ 0 });
    material_test_component(world, entity, &COMPONENT_Body, &(Body){ .type = BODY_DYNAMIC });
    material_test_component(world, entity, &COMPONENT_Collider, &(Collider){ .radius = 10 });
    material_test_component(
        world,
        entity,
        &COMPONENT_Drive,
        &(Drive){ .target = { 120, 0 }, .acceleration = 600 }
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    float speed = Velocity_get_const(world, entity)->x;
    REQUIRE(speed > 9.9f && speed < 10.1f);
    for (int i = 0; i < 14; i++) {
        REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    }
    REQUIRE(fabsf(Velocity_get_const(world, entity)->x - 120) < .1f);
    Drive_get(world, entity)->target.x = 0;
    REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    REQUIRE(fabsf(Velocity_get_const(world, entity)->x - 110) < .1f);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

TEST_CASE(reactions)
{
    material_fluid *f = material_fluid_new(10, 8);
    fluid_reaction r = { .ignition = 1,
                         .cooling = 2,
                         .burn_rate = 2,
                         .heat_release = 2,
                         .smoke_lifetime = 3,
                         .smoke_color = 0x444444ff,
                         .smoke_absorption = .1f,
                         .flame_color = 0xff8800ff,
                         .flame_emission = 1 };
    material_fluid_set_reaction(f, r);
    fluid_particle hot = material_test_particle(-8, 0, FLUID_GAS);
    hot.heat = 2;
    fluid_particle fuel = material_test_particle(0, 0, FLUID_GAS);
    fuel.fuel = 1;
    material_fluid_add(f, hot);
    material_fluid_add(f, fuel);
    fuel.position.x = 8;
    material_fluid_add(f, fuel);
    material_test_tick(f);
    int count;
    const fluid_particle *p = material_fluid_particles(f, &count);
    REQUIRE(p[1].burning && !p[2].burning); /* synchronous propagation */
    material_test_tick(f);
    REQUIRE(p[2].burning);
    int blasts;
    material_fluid_blasts(f, &blasts);
    REQUIRE(blasts == 1);
    for (int i = 0; i < 45; i++) {
        material_test_tick(f);
    }
    REQUIRE(p[1].fuel == 0 && p[2].fuel == 0 && !p[1].burning);
    REQUIRE(p[1].smoke && p[1].glow > 0 && p[1].glow < 1);
    REQUIRE(p[1].color != r.smoke_color && p[1].lifetime > 0 && p[1].lifetime < 3);
    material_fluid_blasts(f, &blasts);
    REQUIRE(!blasts);
    float heat = p[1].heat, life = p[1].lifetime;
    material_fluid_tick(f, 1.f / 60, 64, NULL, 0, NULL, 0, NULL, 0);
    REQUIRE(p[1].heat == heat && p[1].lifetime == life);
    float glow = p[1].glow, emission = p[1].emission;
    unsigned red = p[1].color >> 24;
    for (int i = 0; i < 60; i++) {
        material_test_tick(f);
    }
    REQUIRE(p[1].glow < glow * .15f && p[1].emission < emission * .15f);
    REQUIRE((p[1].color >> 24) < red && p[1].lifetime < life);
    material_fluid_clear(f);
    material_fluid_add(f, hot);
    fuel.position.x = 0;
    material_fluid_add(f, fuel);
    fluid_body wall = { .position = { -4, 0 } };
    fluid_obstacle obstacle = { .body = 0, .half_size = { 1, 20 } };
    fluid_region region = { -50, -50, 100, 100 };
    material_fluid_tick(f, 1.f / 60, 64, &region, 1, &wall, 1, &obstacle, 1);
    p = material_fluid_particles(f, &count);
    REQUIRE(!p[1].burning); /* even a thin wall shields the fuel */
    material_fluid_clear(f);
    material_fluid_add(f, fuel);
    fluid_body shot = { .position = { 20, 0 }, .velocity = { 2400, 0 }, .heat = 2 };
    material_fluid_tick(f, 1.f / 60, 64, &region, 1, &shot, 1, NULL, 0);
    p = material_fluid_particles(f, &count);
    REQUIRE(p[0].burning);
    material_fluid_free(f);
    return true;
}

TEST_CASE(resistance_and_chips)
{
    material_mask mask = material_mask_new(10, 10, 1, false);
    REQUIRE(!material_mask_apply_damage(&mask, 0, 0, 3, 1, 3));
    REQUIRE(!material_mask_apply_damage(&mask, 0, 0, 3, 1, 3));
    REQUIRE(material_mask_apply_damage(&mask, 0, 0, 3, 1, 3) > 0);
    material_mask_rebuild(&mask);
    material_test_check_cover(&mask);
    material_mask_free(&mask);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    ecs_entity config = ecs_world_create_entity(world),
               slab = ecs_world_create_entity(world);
    material_test_component(
        world,
        config,
        &COMPONENT_MaterialDestructionWorld,
        &(MaterialDestructionWorld){ 8 }
    );
    material_test_component(
        world,
        config,
        &COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ 64, 32, 10, 100 }
    );
    material_test_component(world, slab, &COMPONENT_Transform, &(Transform){ 0 });
    material_test_component(
        world,
        slab,
        &COMPONENT_Collider,
        &(Collider){ .shape = COLLIDER_BOX, .width = 20, .height = 20, .category = 8, .mask = 15 }
    );
    material_test_component(world, slab, &COMPONENT_Destructible, &(Destructible){ 1, 3 });
    material_test_component(world, slab, &COMPONENT_Material, &(Material){ .emission_multiplier = .5f });
    material_test_component(world, slab, &COMPONENT_FragmentGlow, &(FragmentGlow){ 1, .3f });
    material_test_component(
        world,
        slab,
        &COMPONENT_Substance,
        &(Substance){ .hardness = 1, .blast_multiplier = 3, .retain_chips = true }
    );
    material_test_component(
        world,
        slab,
        &COMPONENT_Reflection,
        &(Reflection){ .strength = .8f, .roughness = .2f, .angle = 1.2f }
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    materials_damage(live->runtime->materials, slab, (fluid_vec){ 8, 0 }, 3, 1, 0, (material_impact){ 0 });
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    ecs_query query;
    ecs_entity entity;
    int pieces = 0;
    ecs_component_id body = COMPONENT_Body.id;
    REQUIRE(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const Velocity *v = Velocity_get_const(world, entity);
        REQUIRE(v && hypotf(v->x, v->y) > 20);
        REQUIRE(Substance_get_const(world, entity)->blast_multiplier == 3);
        REQUIRE(Reflection_get_const(world, entity)->strength == .8f);
        REQUIRE(materials_mask(live->runtime->materials, entity)->count >= 3);
        REQUIRE(Material_get_const(world, entity)->emission_multiplier == .5f);
        pieces++;
    }
    ecs_query_end(&query);
    REQUIRE(pieces > 0);
    for (int step = 0; step < 30; step++) {
        REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    }
    REQUIRE(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        /* The chipped right edge must drift out of the slab, not into it. */
        REQUIRE(Transform_get_const(world, entity)->x > 8);
        float glow = Material_get_const(world, entity)->emission_multiplier;
        REQUIRE(glow > 0 && glow < .15f);
    }
    ecs_query_end(&query);
    for (int step = 0; step < 35; step++) {
        REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    }
    REQUIRE(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        REQUIRE(Material_get_const(world, entity)->emission_multiplier == 0);
        REQUIRE(materials_mask(live->runtime->materials, entity)->count >= 3);
    }
    ecs_query_end(&query);
    /* Losing power must not remove material or require combat components. */
    REQUIRE(Material_get_const(world, slab)->emission_multiplier == .5f);
    int cells = materials_mask(live->runtime->materials, slab)->count;
    materials_release(live->runtime->materials, slab);
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    REQUIRE(!materials_mask(live->runtime->materials, slab));
    int largest = 0;
    REQUIRE(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const material_mask *hull = materials_mask(live->runtime->materials, entity);
        if (hull && hull->count > largest) {
            largest = hull->count;
        }
    }
    ecs_query_end(&query);
    REQUIRE(largest == cells);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

/* Material Impact energy per target entity, recorded from the entity host. */
typedef struct
{
    ecs_entity target;
    float energy;
} impact;

static int record_impact(void *context, ecs_entity owner, const char *event, execution_arguments arguments)
{
    impact **impacts = context;
    assert(!strcmp(event, "Material Impact") && arguments.count == 3);
    apush(*impacts, ((impact){ owner, arguments.values[0].f }));
    return 0;
}

/* Nothing in the blast case destroys or spawns entities. */
static int refuse_destroy(void *, const ecs_entity *, int)
{
    return -1;
}

static int refuse_attach(void *, ecs_entity, const char *)
{
    return -1;
}

static float impact_energy(const impact *impacts, ecs_entity target)
{
    float energy = 0;
    for (int i = 0; i < acount(impacts); i++) {
        if (ecs_entity_to_u64(impacts[i].target) == ecs_entity_to_u64(target)) {
            energy += impacts[i].energy;
        }
    }
    return energy;
}

/* A fuel blast becomes Material Impact events on the bodies it reaches, at
 * the next event boundary rather than during the step that raised it. */
TEST_CASE(blast_impacts_reach_bodies)
{
    impact *impacts = NULL;
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(!ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL));
    }
    const entity_host host = { .context = &impacts,
                               .destroy = refuse_destroy,
                               .attach = refuse_attach,
                               .dispatch = record_impact };
    entities *entities = entities_new(world, &host);
    materials *materials = materials_new(world, entities, NULL);
    entities_bind_materials(entities, materials);
    physics *physics = physics_new(world, entities);
    ecs_entity config = ecs_world_create_entity(world), bodies[3];
    material_test_component(
        world,
        config,
        &COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ 64, 32, 10, 100 }
    );
    material_test_component(world, config, &COMPONENT_Transform, &(Transform){ 0 });
    material_test_component(world, config, &COMPONENT_Camera, &(Camera){ .width = 256, .height = 256 });
    material_test_component(
        world,
        config,
        &COMPONENT_Combustion,
        &(Combustion){ .ignition = 1,
                       .cooling = 2,
                       .burn_rate = 2,
                       .heat_release = 2,
                       .smoke_lifetime = 3,
                       .blast_radius = 200,
                       .blast_speed = 10,
                       .blast_damage = 20 }
    );
    Transform positions[] = { { 30, 0 }, { 0, 30 }, { -30, 0 } };
    for (int i = 0; i < 3; i++) {
        bodies[i] = ecs_world_create_entity(world);
        material_test_component(world, bodies[i], &COMPONENT_Transform, &positions[i]);
        material_test_component(world, bodies[i], &COMPONENT_Velocity, &(Velocity){ 0 });
        material_test_component(world, bodies[i], &COMPONENT_Collider, &(Collider){ .radius = 2 });
        material_test_component(world, bodies[i], &COMPONENT_Body, &(Body){ .type = BODY_DYNAMIC });
    }
    REQUIRE(!ecs_world_apply_commands(world));
    REQUIRE(!physics_tick(physics, 0));
    for (int i = 0; i < 8; i++) {
        fluid_particle fuel = material_test_particle((i % 4) * 2 - 3, (i / 4) * 2 - 1, FLUID_GAS);
        fuel.fuel = 1;
        fuel.heat = 2;
        REQUIRE(materials_emit(materials, fuel));
    }
    REQUIRE(!physics_tick(physics, 1.f / 60));
    int blast_count;
    material_fluid_blasts(materials_fluid(materials), &blast_count);
    REQUIRE(blast_count > 0);
    REQUIRE(!acount(impacts));
    REQUIRE(!materials_dispatch_events(materials));
    REQUIRE(entities_dispatch_events(entities) >= 0);
    for (int i = 0; i < 3; i++) {
        REQUIRE(impact_energy(impacts, bodies[i]) > 0);
    }
    REQUIRE(impact_energy(impacts, config) == 0);
    afree(impacts);
    physics_free(physics);
    materials_free(materials);
    entities_free(entities);
    ecs_world_free(world);
    return true;
}

void test_material_body_destruction()
{
    RUN_TEST_CASE(physical_destruction);
    RUN_TEST_CASE(finite_drive);
    RUN_TEST_CASE(reactions);
    RUN_TEST_CASE(resistance_and_chips);
    RUN_TEST_CASE(blast_impacts_reach_bodies);
}
