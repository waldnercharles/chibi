#pragma once

#include "runtime/component_catalog.h"
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"

/* Shared setup and invariant checks for the material runtime cases. */
void material_test_check_cover(const material_mask *mask);
fluid_particle material_test_particle(float x, float y, int kind);
void material_test_tick(material_fluid *fluid);
void material_test_component(
    ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *desc,
    const void *value
);
