#pragma once
/* Lightweight rigid debris: small convex rock chunks that tumble, bounce off
 * solid material and get shoved by moving bodies and blasts. Hard knocks split
 * a chunk into smaller ones; every chunk shrinks away at the end of its life.
 * Chunks are visual matter; they do not push back on bodies or gameplay. */
#include "runtime/material_fluid.h"

#include <stdint.h>

enum
{
    MATERIAL_CHUNK_POINTS = 7
};

typedef struct
{
    fluid_vec position, velocity;
    float angle, spin;
    /* Circumradius; collision treats the chunk as this circle. */
    float radius;
    /* Local convex outline, gameplay axes (y down). */
    float points[MATERIAL_CHUNK_POINTS * 2];
    int count;
    /* Where the chunk's material was laid out, so its pattern travels with it. */
    fluid_vec origin;
    /* Caller identity of the material the chunk came from. */
    uint64_t source;
    /* Zero for a thrown chunk; each split adds one. */
    int generation;
    /* Largest velocity change since the last tick. */
    float shock;
    /* Incandescence from the strike, zero to one; it cools within a second. */
    float heat;
    float age, lifetime;
} material_chunk;

/* Signed distance to solid material at point: negative inside. Returns false
 * where nothing solid is known. */
typedef bool material_debris_distance(void *user, fluid_vec point, float *distance);

typedef struct
{
    fluid_vec position;
    float radius, speed;
} material_debris_blast;

typedef struct material_debris material_debris;

material_debris *material_debris_new(int capacity);
void material_debris_free(material_debris *debris);
void material_debris_clear(material_debris *debris);
int material_debris_capacity(const material_debris *debris);
/* An irregular convex outline of five to seven corners within the radius. */
void material_chunk_shape(material_chunk *chunk, unsigned seed);
/* At capacity the oldest chunk is replaced. */
void material_debris_add(material_debris *debris, material_chunk chunk);
/* Shoves chunks away from the blast center at once. */
void material_debris_push(material_debris *debris, material_debris_blast blast);
void material_debris_tick(
    material_debris *debris,
    float delta,
    material_debris_distance *distance,
    void *user,
    const fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    const material_debris_blast *blasts,
    int blast_count
);
const material_chunk *material_debris_chunks(const material_debris *debris, int *count);
/* Chunks whose lifetime ended during the last tick. */
const material_chunk *material_debris_expired(const material_debris *debris, int *count);
/* Scale in [0, 1]: chunks shrink away over the end of their life. */
float material_chunk_scale(const material_chunk *chunk);
