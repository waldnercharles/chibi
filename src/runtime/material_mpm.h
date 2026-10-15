#ifndef CHIBI_MATERIAL_MPM_H
#define CHIBI_MATERIAL_MPM_H

#include "runtime/material_fluid.h"

typedef struct material_mpm material_mpm;

material_mpm *material_mpm_new();
void material_mpm_free(material_mpm *mpm);
void material_mpm_clear(material_mpm *mpm);

/* Conservative wave/advection/deformation bound for one explicit step. Gas
 * parcels are excluded. Returns zero for invalid material state. */
float material_mpm_timestep(
    fluid_mpm_settings settings,
    const fluid_particle *particles,
    const int *active,
    int count
);

typedef struct
{
    fluid_body *bodies;
    int body_count;
    const fluid_obstacle *obstacles;
    int obstacle_count;
    float sweep_end;
} fluid_mpm_colliders;

/* Grid contacts precede G2P so compression includes boundary gradients.
 * Borrowed collider data; body impulses commit together with particles. */
bool material_mpm_step_colliders(
    material_mpm *mpm,
    fluid_mpm_settings settings,
    float dt,
    fluid_particle *particles,
    const int *active,
    int count,
    const task_system *tasks,
    fluid_mpm_colliders colliders
);

/* Quadratic MLS/APIC with complete particle stencils. One stable substep;
 * particles commit only if every candidate has finite positive deformation.
 * The caller owns solid contacts and timestep budgeting. Grid transfers do
 * not provide CPIC separation across thin walls. */
bool material_mpm_step(
    material_mpm *mpm,
    fluid_mpm_settings settings,
    float dt,
    fluid_particle *particles,
    const int *active,
    int count,
    const task_system *tasks
);
int material_mpm_node_count(const material_mpm *mpm);

#endif
