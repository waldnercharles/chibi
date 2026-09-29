#ifndef CHIBI_MATERIAL_FLUID_H
#define CHIBI_MATERIAL_FLUID_H

#include "runtime/tasks.h"

#include <stdbool.h>

typedef struct
{
    float x, y;
} fluid_vec;
typedef struct
{
    float x, y, width, height;
} fluid_region;

enum
{
    FLUID_GAS,
    FLUID_LIQUID
};

enum
{
    MATERIAL_FLUID_CONTACT_LIMIT = 128
};

/* Optional emitter presentation. A zero duration uses the volume's steady look. */
typedef struct
{
    float duration, size_end, size_exponent;
    float emission_end, emission_exponent;
    float drag, drag_end, drag_exponent;
    float blend;
    unsigned end_color;
} fluid_style;

typedef struct
{
    fluid_vec position, velocity;
    float mass, radius;
    unsigned color;
    int kind;
    float absorption, emission;
    /* Zero is persistent. Positive lifetimes advance only in active chunks. */
    float lifetime;
    float fuel, heat;
    float glow, visibility, refraction;
    bool burning, smoke;
    fluid_style style;
} fluid_particle;

typedef struct
{
    fluid_vec position, velocity;
    fluid_vec previous_position;
    float angle, previous_angle, angular_velocity, inverse_mass;
    float heat;
    uint64_t owner; /* Optional caller identity, never interpreted by the solver. */
    bool swept;     /* Previous pose is valid for this simulation tick. */
    /* Output: accumulated translation impulse on this body during the tick. */
    fluid_vec impulse;
} fluid_body;

typedef struct
{
    uint64_t body;
    fluid_vec point, normal;
    float closing_speed, impulse, mass, emission;
    unsigned color;
    int kind, contacts;
} fluid_contact;

typedef struct
{
    float gas_speed, liquid_speed;
} fluid_contact_settings;

typedef struct
{
    int body;
    fluid_vec center, half_size;
    /* Positive for a circle, zero for a box. Geometry is local to body. */
    float radius;
} fluid_obstacle;

typedef struct
{
    float ignition, cooling, burn_rate, heat_release, smoke_lifetime;
    unsigned smoke_color;
    float smoke_absorption;
    unsigned flame_color;
    float flame_emission;
} fluid_reaction;
typedef struct
{
    fluid_vec position;
    int ignitions;
} fluid_blast;

typedef struct material_fluid material_fluid;
typedef struct
{
    /* Zero cell size disables the optional gas flow. */
    float cell_size, vorticity, drag;
} fluid_flow_settings;

material_fluid *material_fluid_new(float kernel, int capacity);
void material_fluid_free(material_fluid *fluid);
void material_fluid_clear(material_fluid *fluid);
void material_fluid_set_flow(material_fluid *fluid, fluid_flow_settings settings);
void material_fluid_set_contact_settings(material_fluid *fluid, fluid_contact_settings settings);
/* Borrowed until the next tick; contacts aggregate by body, kind and surface patch. */
const fluid_contact *material_fluid_contacts(const material_fluid *fluid, int *count);
uint64_t material_fluid_flow_hash(const material_fluid *fluid);
void material_fluid_set_reaction(material_fluid *fluid, fluid_reaction reaction);
const fluid_blast *material_fluid_blasts(const material_fluid *fluid, int *count);
/* Shared obstacle test for ignition and blast occlusion. ignore_body may be -1. */
bool material_fluid_blocked(
    fluid_vec a,
    fluid_vec b,
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int count,
    int ignore_body
);
void material_fluid_push(
    material_fluid *fluid,
    fluid_vec point,
    float radius,
    float speed,
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int count
);
/* Optional borrowed executor; the caller owns its submission scope. */
void material_fluid_set_tasks(material_fluid *fluid, const game_task_system *tasks);
/* Returns false at the authored population limit. No existing matter is evicted. */
bool material_fluid_add(material_fluid *fluid, fluid_particle particle);
const fluid_particle *material_fluid_particles(const material_fluid *fluid, int *count);
int material_fluid_active_count(const material_fluid *fluid);
/* Regions select whole chunks. Sleeping particles keep position, velocity and
 * lifetime. Bodies/obstacles are snapshots; their impulses are returned here.
 * The caller supplies a fixed step no greater than 1/60 second. */
void material_fluid_tick(
    material_fluid *fluid,
    float delta,
    float chunk_size,
    const fluid_region *regions,
    int region_count,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
);

#endif
