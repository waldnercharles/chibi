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

/* Box2D's deterministic atan2 approximation (MIT, Erin Catto). */
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("fp-contract=off")))
#endif
static inline float cos_sin_atan2(float y, float x)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    if (x == 0 && y == 0) {
        return 0;
    }
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    float mx = ay > ax ? ay : ax, mn = ay < ax ? ay : ax;
    float a = mn / mx;
    float s = a * a, c = s * a, q = s * s;
    float r = 0.024840285f * q + 0.18681418f;
    float t = -0.094097948f * q - 0.33213072f;
    r = r * s + t;
    r = r * c + a;
    if (ay > ax) {
        r = 1.57079637f - r;
    }
    if (x < 0) {
        r = 3.14159274f - r;
    }
    if (y < 0) {
        r = -r;
    }
    return r;
}

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("fp-contract=off")))
#endif
static inline cos_sin cos_sin_compute(float radians)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
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

/* Behavior math exposes double values, while the deterministic engine path
 * intentionally shares the field VM's float approximation. */
static inline double cos_sin_sine(double radians)
{
    return cos_sin_compute((float)radians).s;
}

static inline double cos_sin_cosine(double radians)
{
    return cos_sin_compute((float)radians).c;
}

#endif
