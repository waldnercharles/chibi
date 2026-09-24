#ifndef CHIBI_GAME_PARTICLES_H
#define CHIBI_GAME_PARTICLES_H

#include "runtime/components.h"

#define GAME_PARTICLE_LIMIT 2048

typedef struct game_particles game_particles;

typedef struct
{
    float x;
    float y;
} game_particle_position;

typedef struct
{
    game_particle_position position;
    unsigned color; /* 0xRRGGBBAA */
    int count;
    float speed; /* maximum initial speed */
    float lifetime;
} game_particle_burst;

typedef struct
{
    game_particle_position position, direction;
    int count;
    unsigned color, end_color;
    float speed_min, speed_max, lifetime_min, lifetime_max;
    float size_min, size_max, spread, drag_min, drag_max;
    float emission_min, emission_max, size_exponent;
    float elevation; /* Zero rests on the ground; otherwise center height in 3D. */
    int shape;
} game_particle_spray;

typedef struct
{
    game_particle_position position;
    game_particle_position velocity;
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
} game_particle;

float game_particle_curve_value(float start, float end, float exponent, float age);
game_particles *game_particles_new();
void game_particles_free(game_particles *particles);
/* Retains capacity and resets the deterministic local random sequence. */
void game_particles_clear(game_particles *particles);
/* Returns the emitted count, limited by available capacity, or -1 for invalid
 * values. Rejection leaves storage and random state unchanged. */
int game_particles_emit(game_particles *particles, const game_particle_burst *burst);
/* Directed particles with authored shape, ranges and lifetime appearance.
 * Direction is normalized; spread is the full cone angle in radians. */
int game_particles_emit_directed(game_particles *particles, const game_particle_spray *spray);
/* Copies one fully specified particle. Returns 1, 0 at capacity, or -1 without mutation. */
int game_particles_emit_particle(game_particles *particles, const game_particle *particle);
/* Nonnegative finite simulation time only. Returns 0, or -1 without mutation. */
void game_particle_advance(game_particle *particle, double delta);
int game_particles_tick(game_particles *particles, double delta);
/* Borrowed until emit, tick, clear or free. age / lifetime is normalized age. */
const game_particle *game_particles_data(const game_particles *particles);
int game_particles_count(const game_particles *particles);

#endif
