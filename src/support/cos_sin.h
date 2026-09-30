#ifndef CHIBI_COS_SIN_H
#define CHIBI_COS_SIN_H

#include <math.h>

/* Box2D's cosine and sine (b2ComputeCosSin, MIT, Erin Catto): a rational
 * approximation normalized to unit length, accurate to about 0.001. It uses
 * only exactly rounded IEEE operations, so every platform computes the same
 * bits, and it is much faster than libm's sinf and cosf in WASM. */
typedef struct
{
    float c, s;
} cos_sin;

static inline cos_sin cos_sin_compute(float radians)
{
    const float pi = 3.14159265359f, pi2 = pi * pi;
    float x = remainderf(radians, 2 * pi);
    /* Cosine needs the angle in [-pi/2, pi/2], sine in [0, pi]. */
    float y = x < -0.5f * pi ? x + pi : x > 0.5f * pi ? x - pi : x;
    float y2 = y * y;
    float c = (pi2 - 4 * y2) / (pi2 + y2);
    if (y != x) {
        c = -c;
    }
    float z = x < 0 ? x + pi : x;
    float s = 16 * z * (pi - z) / (5 * pi2 - 4 * z * (pi - z));
    if (x < 0) {
        s = -s;
    }
    float length = sqrtf(s * s + c * c);
    float inverse = length > 0 ? 1 / length : 0;
    return (cos_sin){ c * inverse, s * inverse };
}

#endif
