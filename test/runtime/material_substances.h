#pragma once
/* Substance fixtures for native fluid tests, and authored substances for
 * tests that run a live world. */
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "runtime/material_fluid.h"
#include "support/text_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    TEST_GAS,
    TEST_LIQUID,
    TEST_FUEL,
    TEST_SMOKE,
    TEST_SUBSTANCES
};

static inline material_fluid *test_fluid(float kernel, int capacity)
{
    material_fluid *fluid = material_fluid_new(kernel, capacity);
    fluid_substance substances[TEST_SUBSTANCES] = {
        [TEST_GAS] = { .phase = FLUID_GAS,
                       .density = .016f,
                       .color = 0x80c0ffff,
                       .burns_to = FLUID_GONE,
                       .boils_to = FLUID_KEEP,
                       .freezes_to = FLUID_KEEP },
        [TEST_LIQUID] = { .phase = FLUID_LIQUID,
                          .density = .25f,
                          .stiffness = .7f,
                          .cohesion = 1,
                          .friction = .1f,
                          .color = 0x22aaffff,
                          .burns_to = FLUID_GONE,
                          .boils_to = FLUID_KEEP,
                          .freezes_to = FLUID_KEEP },
        [TEST_FUEL] = { .phase = FLUID_GAS,
                        .density = .016f,
                        .color = 0xffb040c0,
                        .cooling = 2,
                        .ignite_at = 1,
                        .burn_rate = 2,
                        .burn_heat = 2,
                        .blast = 1,
                        .burns_to = TEST_SMOKE,
                        .boils_to = FLUID_KEEP,
                        .freezes_to = FLUID_KEEP },
        [TEST_SMOKE] = { .phase = FLUID_GAS,
                         .density = .016f,
                         .color = 0x444444ff,
                         .absorption = .1f,
                         .cooling = 2,
                         .lifetime = 3,
                         .burns_to = FLUID_GONE,
                         .boils_to = FLUID_KEEP,
                         .freezes_to = FLUID_KEEP },
    };
    material_fluid_set_substances(fluid, substances, TEST_SUBSTANCES);
    material_fluid_set_heat(
        fluid,
        (fluid_heat){ .glow_temperature = 2, .glow_color = 0xff8800ff, .glow_emission = 1 }
    );
    return fluid;
}

/* A test particle of substance with a fixed mass and radius. */
static inline fluid_particle test_particle(float x, float y, int substance)
{
    int phase = substance == TEST_LIQUID ? FLUID_LIQUID : FLUID_GAS;
    /* Liquid fills four units square at a quarter of rigid density. */
    return (fluid_particle){ .position = { x, y },
                             .substance = substance,
                             .phase = phase,
                             .mass = phase == FLUID_LIQUID ? .25f * 16 / 1024 : .01f,
                             .radius = 3,
                             .fuel = substance == TEST_FUEL,
                             .deformation = { 1, 0, 0, 1 } };
}

/* Registers the named authored substance and reaction prototypes from Neon
 * Swarm's content, as the editor does when it opens the project. */
static inline void test_register_prototypes(game_live_world *live, const char *const *names, int count)
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    for (int i = 0; i < count; i++) {
        char path[256];
        snprintf(path, sizeof path, "projects/arena2d/content/prototypes/%s.yaml", names[i]);
        char *text = text_file_read(path);
        assert(text);
        char error[256];
        game_prototype *prototype = game_prototype_load(text, &registry, error, sizeof error);
        free(text);
        if (!prototype) {
            fprintf(stderr, "%s: %s\n", names[i], error);
        }
        assert(prototype);
        assert(game_entities_register(live->entities, prototype) == 0);
        game_prototype_free(prototype);
    }
}
