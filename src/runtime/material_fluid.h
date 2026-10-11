#ifndef CHIBI_MATERIAL_FLUID_H
#define CHIBI_MATERIAL_FLUID_H

#include "runtime/tasks.h"

#include <stdbool.h>
#include <stdint.h>

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
    FLUID_SOLVER_DDR,
    FLUID_SOLVER_MPM
};

enum
{
    FLUID_VIEW_MATERIAL,
    FLUID_VIEW_HEAT,
    FLUID_VIEW_SPEED,
    FLUID_VIEW_STRAIN
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
    /* Specific heat capacity per unit mass; conductivity is an exchange rate.
     * Zero capacity disables exchange; zero conductivity insulates. */
    float capacity, conductivity;
} fluid_thermal;

bool material_fluid_thermal_valid(fluid_thermal value);

typedef struct
{
    float density, bulk_modulus, shear_modulus;
} fluid_constitutive;

typedef struct
{
    /* Row-major deformation and affine velocity matrices. */
    float f[4], affine[4], volume;
} fluid_deformation;

typedef struct
{
    float cell_size;
    int max_substeps;
    fluid_vec gravity;
} fluid_mpm_settings;

typedef struct
{
    int substeps, nodes;
    float advanced_time;
    bool limited;
} fluid_mpm_state;

bool material_fluid_constitutive_valid(fluid_constitutive value);

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
    /* Optional individual-grain presentation, independent of physical radius. */
    float grain_radius;
    bool burning, smoke;
    fluid_style style;
    fluid_thermal thermal;
    fluid_constitutive constitutive;
    fluid_deformation deformation;
    /* Saved on ignition and restored when combustion is quenched. */
    unsigned base_color;
    float base_emission, base_glow;
} fluid_particle;

typedef struct
{
    fluid_vec position, velocity;
    fluid_vec previous_position;
    fluid_vec center_offset;
    float angle, previous_angle, angular_velocity, inverse_mass;
    float heat;
    uint64_t owner; /* Optional caller identity, never interpreted by the solver. */
    bool swept;     /* Previous pose is valid for this simulation tick. */
    /* Output: accumulated translation impulse on this body during the tick. */
    fluid_vec impulse;
    float inverse_inertia, angular_impulse;
    /* 1 + the index of an earlier body it moves with as one, whose impulse
     * its contacts change and which a blast moves once; 0 for its own. */
    int part_of;
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
    double reactions_ms, gas_flow_ms, index_ms, density_ms, relaxation_ms;
    double collisions_ms;
    /* Collision-index construction is included in collisions_ms. */
    double collision_index_ms;
    double mpm_ms;
    uint64_t index_builds, mpm_substeps, mpm_limited_steps;
} material_fluid_profile;

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
    /* Zero disables quenching; otherwise this must be below ignition. */
    float extinction;
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
    fluid_vec gravity;
    float buoyancy, ambient_heat;
} fluid_flow_settings;

material_fluid *material_fluid_new(float kernel, int capacity);
void material_fluid_free(material_fluid *fluid);
void material_fluid_clear(material_fluid *fluid);
void material_fluid_set_flow(material_fluid *fluid, fluid_flow_settings settings);
void material_fluid_set_solver(material_fluid *fluid, int solver, fluid_mpm_settings settings);
int material_fluid_solver(const material_fluid *fluid);
void material_fluid_set_debug_view(material_fluid *fluid, int view);
int material_fluid_debug_view(const material_fluid *fluid);
float material_fluid_visual_support(const material_fluid *fluid);
fluid_mpm_state material_fluid_mpm_state(const material_fluid *fluid);
/* Caller owns the support/array result. Returns conservative chunk candidates. */
void material_fluid_query(const material_fluid *fluid, fluid_region region, int **indices);
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
void material_fluid_set_tasks(material_fluid *fluid, const task_system *tasks);
/* Returns false at the authored population limit. No existing matter is evicted. */
bool material_fluid_add(material_fluid *fluid, fluid_particle particle);
/* The index of the body whose impulse contacts with bodies[index] change. */
int material_fluid_response(const fluid_body *bodies, int index);
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
/* Optional timings for the six hot fluid solver phases. */
void material_fluid_tick_profiled(
    material_fluid *fluid,
    float delta,
    float chunk_size,
    const fluid_region *regions,
    int region_count,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    material_fluid_profile *profile
);

#endif
