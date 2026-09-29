#pragma once
/* Private state shared by the fluid solver and its heat and reaction pass. */
#include "runtime/material_flow.h"
#include "runtime/material_fluid.h"

typedef struct
{
    fluid_vec previous, correction;
    float pressure, near_pressure, heat;
    int x, y, next;
    /* A reaction proposal for this tick, or -1. */
    int partner;
    /* Position in the active set, or -1 while asleep. */
    int slot;
    bool reacted, doomed;
} particle_work;

typedef struct
{
    int body_index, patch_x, patch_y, next;
    double weight, point_x, point_y, normal_x, normal_y, emission;
    double color[4];
} contact_work;

struct material_fluid
{
    fluid_particle *particles;
    particle_work *work;
    int *heads, *active, *used;
    int *contact_heads, *contact_used;
    fluid_contact *contacts;
    contact_work *contact_work;
    fluid_blast *blasts;
    fluid_deposit *deposits;
    fluid_touch *touches;
    fluid_substance *substances;
    fluid_reaction *reactions;
    fluid_heat heat;
    /* Scratch: active particles grouped by substance, then burning ones. */
    int *by_substance, *substance_start, *burning;
    /* Particles leaving the world at the end of this tick, ascending. */
    int *doomed;
    /* Per hash bucket: whether it holds warm or burning matter this tick,
     * and the buckets marked so. */
    unsigned char *warm;
    int *warm_used;
    fluid_region bounds;
    int capacity, buckets;
    float kernel, delta;
    unsigned tick;
    fluid_contact_settings contact_settings;
    game_task_system tasks;
    material_flow *flow;
    fluid_flow_settings flow_settings;
    /* Borrowed for the duration of one tick. */
    const fluid_step *step;
};

static inline fluid_vec fluid_add(fluid_vec a, fluid_vec b)
{
    return (fluid_vec){ a.x + b.x, a.y + b.y };
}
static inline fluid_vec fluid_sub(fluid_vec a, fluid_vec b)
{
    return (fluid_vec){ a.x - b.x, a.y - b.y };
}
static inline fluid_vec fluid_scale(fluid_vec a, float s)
{
    return (fluid_vec){ a.x * s, a.y * s };
}
static inline float fluid_dot(fluid_vec a, fluid_vec b)
{
    return a.x * b.x + a.y * b.y;
}
static inline unsigned fluid_bucket(const material_fluid *fluid, int x, int y)
{
    return ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u) & (fluid->buckets - 1);
}

/* Runs fn over [0, count) on the borrowed executor when it is worth it. */
void material_fluid_run(material_fluid *fluid, game_task_range *fn, int count, void *context, int grain);
/* Queues particle i to leave the world when the tick ends. */
void material_fluid_doom(material_fluid *fluid, int i);
/* Heat exchange, burning, phase changes and reactions for the active set. */
void material_fluid_react(material_fluid *fluid);
/* Corrosive and hot matter touching static solids, after motion. */
void material_fluid_touch_solids(material_fluid *fluid);
