#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/material_destruction.h"
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

/* Fragments retain their source material frame, including inherited frames. */
TEST_CASE(fragment_material_frame)
{
    for (int inherited = 0; inherited < 2; inherited++) {
        ecs_world *world = ecs_world_new();
        for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
            REQUIRE(
                ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
            );
        }
        game_preview_world *live = game_preview_world_new(world);
        ecs_entity config = ecs_world_create_entity(world),
                   slab = ecs_world_create_entity(world);
        material_test_component(
            world,
            config,
            &GAME_COMPONENT_MaterialDestructionWorld,
            &(MaterialDestructionWorld){ 8 }
        );
        material_test_component(
            world,
            config,
            &GAME_COMPONENT_MaterialFluidWorld,
            &(MaterialFluidWorld){ 64, 32, 10, 100 }
        );
        material_test_component(world, slab, &GAME_COMPONENT_GameTransform, &(GameTransform){ 30, -12 });
        material_test_component(world, slab, &GAME_COMPONENT_GameOrientation, &(GameOrientation){ .4f });
        material_test_component(
            world,
            slab,
            &GAME_COMPONENT_GameCollider,
            &(GameCollider){ .shape = GAME_COLLIDER_BOX,
                             .width = 20,
                             .height = 20,
                             .category = 8,
                             .mask = 15 }
        );
        material_test_component(
            world,
            slab,
            &GAME_COMPONENT_GameDestructible,
            &(GameDestructible){ .cell_size = 1, .min_fragment_cells = 3, .release_fraction = .3f }
        );
        material_test_component(world, slab, &GAME_COMPONENT_GameShader, &(GameShader){ .material = "Basalt" });
        GameMaterialFrame expected = { 30, -12, .4f };
        if (inherited) {
            expected = (GameMaterialFrame){ -5, 7, 1.1f };
            material_test_component(world, slab, &GAME_COMPONENT_GameMaterialFrame, &expected);
        }
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(game_physics_tick(live->runtime->physics, 0) == 0);
        game_materials_damage(
            live->runtime->materials,
            slab,
            (fluid_vec){ 40, -12 },
            9,
            1,
            0,
            (material_impact){ 0 }
        );
        REQUIRE(game_physics_tick(live->runtime->physics, 0) == 0);
        game_materials_damage(
            live->runtime->materials,
            slab,
            (fluid_vec){ 20, -12 },
            12,
            1,
            0,
            (material_impact){ 0 }
        );
        REQUIRE(game_physics_tick(live->runtime->physics, 0) == 0);
        ecs_query query;
        ecs_entity entity;
        ecs_component_id body = GAME_COMPONENT_GameBody.id;
        int fragments = 0;
        REQUIRE(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
        while (ecs_query_next(&query, &entity)) {
            const GameMaterialFrame *frame = GAME_GameMaterialFrame_get_const(world, entity);
            REQUIRE(
                frame && frame->x == expected.x && frame->y == expected.y &&
                frame->radians == expected.radians
            );
            fragments++;
        }
        ecs_query_end(&query);
        REQUIRE(fragments > 0);
        game_preview_world_free(live);
        ecs_world_free(world);
    }
    return true;
}

TEST_CASE(rotated_occlusion)
{
    fluid_body body = { .position = { 10, 20 }, .angle = 1.57079632679f };
    fluid_obstacle obstacle = { .center = { 30, 0 }, .half_size = { 8, 2 } };
    /* An offset box rotates far from its body's origin. Conservative culling
     * must preserve both a short crossing and a segment starting far away. */
    REQUIRE(
        material_fluid_blocked((fluid_vec){ 0, 50 }, (fluid_vec){ 20, 50 }, &body, &obstacle, 1, -1)
    );
    REQUIRE(
        material_fluid_blocked((fluid_vec){ -1000, 50 }, (fluid_vec){ 1000, 50 }, &body, &obstacle, 1, -1)
    );
    REQUIRE(
        !material_fluid_blocked((fluid_vec){ 100, 50 }, (fluid_vec){ 120, 50 }, &body, &obstacle, 1, -1)
    );
    obstacle.radius = 2;
    REQUIRE(
        material_fluid_blocked((fluid_vec){ 0, 50 }, (fluid_vec){ 20, 50 }, &body, &obstacle, 1, -1)
    );
    REQUIRE(
        !material_fluid_blocked((fluid_vec){ 0, 55 }, (fluid_vec){ 20, 55 }, &body, &obstacle, 1, -1)
    );
    return true;
}

TEST_CASE(fragment_penetration)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_preview_world *live = game_preview_world_new(world);
    ecs_entity config = ecs_world_create_entity(world);
    material_test_component(
        world,
        config,
        &GAME_COMPONENT_MaterialDestructionWorld,
        &(MaterialDestructionWorld){ 8 }
    );
    material_test_component(
        world,
        config,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ 64, 32, 10, 100 }
    );
    for (int i = 0; i < 2; i++) {
        ecs_entity slab = ecs_world_create_entity(world);
        material_test_component(
            world,
            slab,
            &GAME_COMPONENT_GameTransform,
            &(GameTransform){ 20 + 60 * i, 0 }
        );
        material_test_component(
            world,
            slab,
            &GAME_COMPONENT_GameCollider,
            &(GameCollider){ .shape = GAME_COLLIDER_BOX,
                             .width = 8,
                             .height = 16,
                             .category = 8,
                             .mask = 4 }
        );
        material_test_component(
            world,
            slab,
            &GAME_COMPONENT_GameDestructible,
            &(GameDestructible){ .cell_size = 1,
                                 .min_fragment_cells = 2,
                                 .pierceable_fragments = i == 0 }
        );
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        /* Releasing before mask initialization must preserve the request. */
        game_materials_release(live->runtime->materials, slab);
    }
    ecs_entity wall = ecs_world_create_entity(world),
               shot = ecs_world_create_entity(world);
    material_test_component(world, wall, &GAME_COMPONENT_GameTransform, &(GameTransform){ 50, 0 });
    material_test_component(
        world,
        wall,
        &GAME_COMPONENT_GameCollider,
        &(
            GameCollider
        ){ .shape = GAME_COLLIDER_BOX, .width = 8, .height = 16, .category = 8, .mask = 4 }
    );
    material_test_component(world, shot, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    material_test_component(world, shot, &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 6000, 0 });
    material_test_component(
        world,
        shot,
        &GAME_COMPONENT_GameBody,
        &(GameBody){ .type = GAME_BODY_KINEMATIC, .continuous = true }
    );
    material_test_component(
        world,
        shot,
        &GAME_COMPONENT_GameCollider,
        &(GameCollider){ .radius = 2, .category = 4, .mask = 8, .sensor = true }
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    game_physics_set_profiling(live->runtime->physics, true);
    REQUIRE(game_physics_tick(live->runtime->physics, 1.f / 60) == 0);
    REQUIRE(game_materials_inspect(live->runtime->materials).fragments == 2);
    REQUIRE(GAME_GameTransform_get_const(world, shot)->x < 20);
    REQUIRE(game_physics_last_profile(live->runtime->physics)->pairs == 1);

    game_physics_reset(live->runtime->physics);
    GAME_GameTransform_get(world, shot)->x = 0;
    GAME_GameCollider_get(world, shot)->pierce_fragments = true;
    REQUIRE(game_physics_tick(live->runtime->physics, 1.f / 60) == 0);
    float x = GAME_GameTransform_get_const(world, shot)->x;
    REQUIRE(x > 43 && x < 45);
    /* The wall clips both travel and events: the far fragment is untouched. */
    REQUIRE(game_physics_last_profile(live->runtime->physics)->pairs == 2);

    REQUIRE(GAME_GameCollider_remove(world, wall, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    game_physics_reset(live->runtime->physics);
    GAME_GameTransform_get(world, shot)->x = 0;
    REQUIRE(game_physics_tick(live->runtime->physics, 1.f / 60) == 0);
    REQUIRE(
        GAME_GameTransform_get_const(world, shot)->x > 73 &&
        GAME_GameTransform_get_const(world, shot)->x < 77
    );
    REQUIRE(game_physics_last_profile(live->runtime->physics)->pairs == 2);
    game_preview_world_free(live);
    ecs_world_free(world);
    return true;
}

TEST_CASE(swept_ignition)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_preview_world *live = game_preview_world_new(world);
    ecs_entity config = ecs_world_create_entity(world),
               wall = ecs_world_create_entity(world),
               shot = ecs_world_create_entity(world);
    material_test_component(
        world,
        config,
        &GAME_COMPONENT_MaterialDestructionWorld,
        &(MaterialDestructionWorld){ 8 }
    );
    material_test_component(
        world,
        config,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ 64, 32, 10, 100 }
    );
    material_test_component(
        world,
        config,
        &GAME_COMPONENT_GameCombustion,
        &(GameCombustion){ .ignition = 1,
                           .cooling = 2,
                           .burn_rate = 2,
                           .heat_release = 2,
                           .smoke_lifetime = 3 }
    );
    material_test_component(world, wall, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    material_test_component(
        world,
        wall,
        &GAME_COMPONENT_GameCollider,
        &(
            GameCollider
        ){ .shape = GAME_COLLIDER_BOX, .width = 2, .height = 40, .category = 8, .mask = 4 }
    );
    material_test_component(world, wall, &GAME_COMPONENT_GameDestructible, &(GameDestructible){ 1, 3 });
    material_test_component(world, shot, &GAME_COMPONENT_GameTransform, &(GameTransform){ -20, 0 });
    material_test_component(world, shot, &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 2400, 0 });
    material_test_component(
        world,
        shot,
        &GAME_COMPONENT_GameBody,
        &(GameBody){ .type = GAME_BODY_KINEMATIC, .continuous = true }
    );
    material_test_component(
        world,
        shot,
        &GAME_COMPONENT_GameCollider,
        &(GameCollider){ .radius = 2, .category = 4, .mask = 8, .sensor = true }
    );
    material_test_component(world, shot, &GAME_COMPONENT_GameProjectile, &(GameProjectile){ 0 });
    material_test_component(world, shot, &GAME_COMPONENT_GameIgniter, &(GameIgniter){ 2 });
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(game_physics_tick(live->runtime->physics, 0) == 0);
    fluid_particle fuel = material_test_particle(8, 0, FLUID_GAS);
    fuel.fuel = 1;
    REQUIRE(game_materials_emit(live->runtime->materials, fuel));
    REQUIRE(game_physics_tick(live->runtime->physics, 1.f / 60) == 0);
    REQUIRE(GAME_GameTransform_get_const(world, shot)->x < 0);
    REQUIRE(!game_materials_inspect(live->runtime->materials).burning_particles);
    game_materials_damage(live->runtime->materials, wall, (fluid_vec){ 0 }, 10, 1, 0, (material_impact){ 0 });
    *GAME_GameTransform_get(world, shot) = (GameTransform){ -20, 0 };
    REQUIRE(game_physics_tick(live->runtime->physics, 1.f / 60) == 0);
    REQUIRE(game_materials_inspect(live->runtime->materials).burning_particles == 1);
    game_preview_world_free(live);
    ecs_world_free(world);
    return true;
}

void test_material_fragments()
{
    RUN_TEST_CASE(fragment_material_frame);
    RUN_TEST_CASE(rotated_occlusion);
    RUN_TEST_CASE(fragment_penetration);
    RUN_TEST_CASE(swept_ignition);
}
