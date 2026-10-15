#ifndef CHIBI_PARTICLES_H
#define CHIBI_PARTICLES_H

#include "runtime/components.h"

#define PARTICLE_LIMIT 2048

typedef struct particles particles;

typedef struct
{
    float x;
    float y;
} particle_position;

typedef struct
{
    particle_position position;
    unsigned color; /* 0xRRGGBBAA */
    int count;
    float speed; /* maximum initial speed */
    float lifetime;
} particle_burst;

typedef struct
{
    particle_position position, direction;
    int count;
    unsigned color, end_color;
    float speed_min, speed_max, lifetime_min, lifetime_max;
    float size_min, size_max, spread, drag_min, drag_max;
    float emission_min, emission_max, size_exponent;
    /* Positive values fade emission to zero over the lifetime; zero keeps it. */
    float emission_exponent;
    /* Streak length per unit of speed. */
    float stretch;
    /* Edge softness in world units; zero softens by one pixel. */
    float antialias;
    float elevation; /* Zero rests on the ground; otherwise center height in 3D. */
    int shape;
} particle_spray;

typedef struct
{
    particle_position position;
    particle_position velocity;
    unsigned color;
    float age;
    float lifetime;
    unsigned end_color;
    float size, size_exponent, drag, emission;
    float size_end, emission_end, emission_exponent, drag_end, drag_exponent;
    float antialias, antialias_end, antialias_exponent, absorption;
    float stretch, metaball_blend;
    int metaball_group;
    float elevation;
    float elevation_velocity, angle;
    int velocity_align;
    int shape;
} particle;

float particle_curve_value(float start, float end, float exponent, float age);
particles *particles_new();
void particles_free(particles *particles);
/* Retains capacity and resets the deterministic local random sequence. */
void particles_clear(particles *particles);
/* Returns the emitted count, limited by available capacity, or -1 for invalid
 * values. Rejection leaves storage and random state unchanged. */
int particles_emit(particles *particles, const particle_burst *burst);
/* Directed particles with authored shape, ranges and lifetime appearance.
 * Direction is normalized; spread is the full cone angle in radians. */
int particles_emit_directed(particles *particles, const particle_spray *spray);
/* Copies one fully specified particle. Returns 1, 0 at capacity, or -1 without mutation. */
int particles_emit_particle(particles *particles, const particle *particle);
/* Nonnegative finite simulation time only. Returns 0, or -1 without mutation. */
void particle_advance(particle *particle, double delta);
int particles_tick(particles *particles, double delta);
/* Borrowed until emit, tick, clear or free. age / lifetime is normalized age. */
const particle *particles_data(const particles *particles);
int particles_count(const particles *particles);

#endif
