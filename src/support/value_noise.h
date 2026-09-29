#ifndef CHIBI_VALUE_NOISE_H
#define CHIBI_VALUE_NOISE_H

#include <math.h>

/* Lattice hash and value noise shared by procedural fields and behaviors, so
 * one seed and point give the same value in both. */

/* Coordinates at or beyond this magnitude have no integer lattice cell. */
#define VALUE_NOISE_LIMIT 2147483520.f

/* From -1 to 1, constant across the unit cell (x, y). */
static inline float value_hash(unsigned seed, int x, int y)
{
    unsigned hash = seed ^ (unsigned)x * 0x9e3779b9u ^ (unsigned)y * 0x85ebca6bu;
    hash = (hash ^ (hash >> 16)) * 0x7feb352du;
    hash = (hash ^ (hash >> 15)) * 0x846ca68bu;
    return ((hash ^ (hash >> 16)) & 65535) / 32767.5f - 1;
}

static inline float value_noise_mix(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* Smooth noise from -1 to 1, one lattice cell per unit; NaN outside the
 * lattice. */
static inline float value_noise(unsigned seed, float x, float y)
{
    if (!isfinite(x) || !isfinite(y) || fabsf(x) >= VALUE_NOISE_LIMIT ||
        fabsf(y) >= VALUE_NOISE_LIMIT) {
        return NAN;
    }
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float tx = x - ix, ty = y - iy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    return value_noise_mix(
        value_noise_mix(value_hash(seed, ix, iy), value_hash(seed, ix + 1, iy), tx),
        value_noise_mix(value_hash(seed, ix, iy + 1), value_hash(seed, ix + 1, iy + 1), tx),
        ty
    );
}

#endif
