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

/* Matches MaterialPhase. */
enum
{
    FLUID_GAS,
    FLUID_LIQUID,
    FLUID_POWDER,
    FLUID_ELASTIC,
    FLUID_PHASE_COUNT
};

enum
{
    MATERIAL_FLUID_CONTACT_LIMIT = 128
};

/* A product of a transition or reaction: a substance index, or one of these. */
enum
{
    /* Reactions only: the particle stays as it is. */
    FLUID_KEEP = -1,
    /* The particle leaves the world. */
    FLUID_GONE = -2,
    /* FLUID_DEPOSIT - k adds the particle to deposit target k. */
    FLUID_DEPOSIT = -3
};

/* A reaction partner that matches any burning particle. */
enum
{
    FLUID_BURNING = -1
};

/* A kind of matter. Heat is a gameplay temperature: zero is ambient and
 * everything cools toward it. Products are indices or the constants above. */
typedef struct
{
    int phase;
    /* Mass per area; one matches rigid bodies. */
    float density;
    float viscosity, cohesion;
    /* Gas only: the fraction of gravity that lifts it, times 1 + temperature. */
    float buoyancy;
    unsigned color;
    float emission, absorption, visibility, refraction;
    float temperature, cooling;
    /* Zero is persistent. */
    float lifetime;
    /* Positive ignite_at makes it flammable: it burns for 1 / burn_rate
     * seconds at no less than burn_heat, then becomes burns_to. Each ignition
     * adds blast to its neighborhood's explosion. */
    float ignite_at, burn_rate, burn_heat, blast;
    int burns_to;
    float boil_at;
    int boils_to;
    float freeze_at;
    int freezes_to;
    /* Damage per second to solids it touches. */
    float corrosion;
    bool conductive;
} fluid_substance;

/* Where a touches b, each pair turns into a_becomes and b_becomes with
 * probability rate per second, and both gain heat. */
typedef struct
{
    int a, b;
    float rate;
    int a_becomes, b_becomes;
    float heat;
} fluid_reaction;

/* Heat exchange and the incandescence of hot matter. Matter glows from half
 * glow_temperature and fully at it, toward glow_color and glow_emission. */
typedef struct
{
    float conduction, glow_temperature;
    unsigned glow_color;
    float glow_emission;
} fluid_heat;

/* Optional emitter presentation. A zero duration uses the substance's look. */
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
    int substance;
    /* Cached from the substance for solvers and rendering. */
    int phase;
    unsigned color;
    float absorption, emission, visibility, refraction;
    /* Zero is persistent. Positive lifetimes advance only in active chunks. */
    float lifetime;
    float temperature;
    /* What remains of a flammable or corrosive particle, from one to zero. */
    float fuel;
    /* Incandescence from zero to one, derived each step from temperature. */
    float glow;
    bool burning;
    /* Formed in the world: it grows and fades over its lifetime. */
    bool billow;
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
    int phase, contacts;
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

/* Snapshots of moving bodies and their geometry; impulses return in bodies. */
typedef struct
{
    fluid_body *bodies;
    int body_count;
    const fluid_obstacle *obstacles;
    int obstacle_count;
} fluid_colliders;

typedef struct material_solids material_solids;

/* One fixed simulation step, no longer than 1/60 second. Regions select whole
 * chunks. Gravity is an acceleration in world units per second squared.
 * Solids are optional static material. */
typedef struct
{
    float delta, chunk_size;
    fluid_vec gravity;
    const fluid_region *regions;
    int region_count;
    fluid_colliders colliders;
    const material_solids *solids;
} fluid_step;

typedef struct
{
    fluid_vec position;
    /* Summed blast weight of the ignitions near position this tick. */
    float ignitions;
} fluid_blast;

/* Matter that became a deposit this tick: the target and the area it fills. */
typedef struct
{
    fluid_vec position;
    int target;
    float area;
} fluid_deposit;

/* Matter touching a static solid this tick, merged per solid and patch: the
 * corrosion damage it deals and whether it is hot enough to melt it. */
typedef struct
{
    int solid;
    fluid_vec point;
    float corrosion;
    bool melts;
} fluid_touch;

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
/* Copies the tables. Particles keep their substance indices, so a new table
 * may only extend or edit the previous one. */
void material_fluid_set_substances(material_fluid *fluid, const fluid_substance *substances, int count);
void material_fluid_set_reactions(material_fluid *fluid, const fluid_reaction *reactions, int count);
void material_fluid_set_heat(material_fluid *fluid, fluid_heat heat);
fluid_heat material_fluid_heat(const material_fluid *fluid);
const fluid_substance *material_fluid_substances(const material_fluid *fluid, int *count);
/* A particle of substance at rest at position, with its look, heat and the
 * spacing its phase fills space at. */
fluid_particle material_fluid_particle(const material_fluid *fluid, int substance, fluid_vec position);
/* World units between neighboring particles when a phase fills space. */
float material_fluid_spacing(const material_fluid *fluid, int phase);
/* Borrowed until the next tick; contacts aggregate by body, phase and surface patch. */
const fluid_contact *material_fluid_contacts(const material_fluid *fluid, int *count);
uint64_t material_fluid_flow_hash(const material_fluid *fluid);
const fluid_blast *material_fluid_blasts(const material_fluid *fluid, int *count);
const fluid_deposit *material_fluid_deposits(const material_fluid *fluid, int *count);
const fluid_touch *material_fluid_touches(const material_fluid *fluid, int *count);
/* Shared occlusion test for heat and blasts: static solids and body
 * obstacles. Hot bodies never block; ignore_body may be -1. */
bool material_fluid_blocked(
    fluid_vec a,
    fluid_vec b,
    const fluid_colliders *colliders,
    const material_solids *solids,
    int ignore_body
);
void material_fluid_push(
    material_fluid *fluid,
    fluid_vec point,
    float radius,
    float speed,
    const fluid_step *step
);
/* Adds delta to the temperature of active matter within radius of point. */
void material_fluid_heat_area(material_fluid *fluid, fluid_vec point, float radius, float delta);
/* Optional borrowed executor; the caller owns its submission scope. */
void material_fluid_set_tasks(material_fluid *fluid, const game_task_system *tasks);
/* Returns false at the authored population limit. No existing matter is evicted. */
bool material_fluid_add(material_fluid *fluid, fluid_particle particle);
const fluid_particle *material_fluid_particles(const material_fluid *fluid, int *count);
int material_fluid_active_count(const material_fluid *fluid);
/* Sleeping particles keep position, velocity and lifetime. */
void material_fluid_tick(material_fluid *fluid, const fluid_step *step);

#endif
