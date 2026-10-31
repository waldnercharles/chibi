#ifndef CHIBI_MATERIAL_FLOW_H
#define CHIBI_MATERIAL_FLOW_H

#include "runtime/material_fluid.h"

/* Sparse staggered velocity grid. Only the active gas neighborhood is retained;
 * returning regions seed their flow from the particles that slept there. */
typedef struct material_flow material_flow;
material_flow *material_flow_new();
void material_flow_free(material_flow *flow);
void material_flow_clear(material_flow *flow);
uint64_t material_flow_hash(const material_flow *flow);
void material_flow_step(
    material_flow *flow,
    fluid_flow_settings settings,
    float delta,
    fluid_particle *particles,
    const int *active,
    int count,
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    const task_system *tasks
);

#endif
