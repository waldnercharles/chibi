#ifndef CHIBI_MATH_H
#define CHIBI_MATH_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* This is the editor/core math boundary. Cute owns runtime vectors, matrices,
 * and collision geometry; editor layout and graph presentation need a small
 * independent vec2 plus generic scalar/easing helpers, so they stay here. */

typedef struct vec2
{
    float x;
    float y;
} vec2;

static inline vec2 vec2_make(float x, float y)
{
    return (vec2){ x, y };
}

static inline vec2 vec2_splat(float value)
{
    return vec2_make(value, value);
}

static inline vec2 vec2_add(vec2 a, vec2 b)
{
    return vec2_make(a.x + b.x, a.y + b.y);
}

static inline vec2 vec2_sub(vec2 a, vec2 b)
{
    return vec2_make(a.x - b.x, a.y - b.y);
}

static inline vec2 vec2_mul(vec2 a, vec2 b)
{
    return vec2_make(a.x * b.x, a.y * b.y);
}

static inline vec2 vec2_scale(vec2 a, float scale)
{
    return vec2_make(a.x * scale, a.y * scale);
}

static inline vec2 vec2_div(vec2 a, vec2 b)
{
    return vec2_make(a.x / b.x, a.y / b.y);
}

static inline vec2 vec2_div_scalar(vec2 a, float divisor)
{
    return vec2_scale(a, 1.0f / divisor);
}

static inline vec2 vec2_neg(vec2 a)
{
    return vec2_make(-a.x, -a.y);
}

static inline float vec2_dot(vec2 a, vec2 b)
{
    return a.x * b.x + a.y * b.y;
}

static inline float vec2_cross(vec2 a, vec2 b)
{
    return a.x * b.y - a.y * b.x;
}

static inline float vec2_length_sq(vec2 a)
{
    return vec2_dot(a, a);
}

static inline float vec2_length(vec2 a)
{
    return sqrtf(vec2_length_sq(a));
}

static inline float vec2_distance_sq(vec2 a, vec2 b)
{
    return vec2_length_sq(vec2_sub(b, a));
}

static inline float vec2_distance(vec2 a, vec2 b)
{
    return sqrtf(vec2_distance_sq(a, b));
}

static inline vec2 vec2_safe_norm(vec2 a)
{
    float length = vec2_length(a);
    return length != 0.0f ? vec2_div_scalar(a, length) : vec2_splat(0.0f);
}

static inline vec2 vec2_lerp(vec2 a, vec2 b, float t)
{
    return vec2_add(a, vec2_scale(vec2_sub(b, a), t));
}

// clang-format off
#define MATH_REAL_TYPES(X)                                                     \
    X(float, f)                                                                \
    X(double, d)

#define MATH_SIGNED_TYPES(X)                                                   \
    X(int8_t, i8)                                                              \
    X(int16_t, i16)                                                            \
    X(int32_t, i32)                                                            \
    X(int64_t, i64)

#define MATH_UNSIGNED_TYPES(X)                                                 \
    X(uint8_t, u8)                                                             \
    X(uint16_t, u16)                                                           \
    X(uint32_t, u32)                                                           \
    X(uint64_t, u64)

#define MATH_SCALAR_TYPES(X)                                                   \
    MATH_REAL_TYPES(X)                                                         \
    MATH_SIGNED_TYPES(X)                                                       \
    MATH_UNSIGNED_TYPES(X)

#define MATH_DEFINE_MIN(type, suffix)                                          \
    static inline type math_min_##suffix(type a, type b)                       \
    {                                                                          \
        return a < b ? a : b;                                                  \
    }
MATH_SCALAR_TYPES(MATH_DEFINE_MIN)
#undef MATH_DEFINE_MIN

#define MATH_MIN_CASE(type, suffix) type: math_min_##suffix,
#define MATH_MIN_CASES \
    MATH_SCALAR_TYPES(MATH_MIN_CASE) \
    vec2: vec2_min,
#define math_min(a, b)                                                         \
    _Generic((a), MATH_MIN_CASES default: math_min_i32)((a), (b))

#define MATH_DEFINE_MAX(type, suffix)                                          \
    static inline type math_max_##suffix(type a, type b)                       \
    {                                                                          \
        return a > b ? a : b;                                                  \
    }
MATH_SCALAR_TYPES(MATH_DEFINE_MAX)
#undef MATH_DEFINE_MAX

#define MATH_MAX_CASE(type, suffix) type: math_max_##suffix,
#define MATH_MAX_CASES \
    MATH_SCALAR_TYPES(MATH_MAX_CASE) \
    vec2: vec2_max,
#define math_max(a, b)                                                         \
    _Generic((a), MATH_MAX_CASES default: math_max_i32)((a), (b))

#define MATH_DEFINE_ABS_REAL(type, suffix)                                     \
    static inline type math_abs_##suffix(type x)                               \
    {                                                                          \
        return x < (type)0 ? -x : x;                                           \
    }
MATH_REAL_TYPES(MATH_DEFINE_ABS_REAL)
#undef MATH_DEFINE_ABS_REAL

#define MATH_DEFINE_ABS_SIGNED(type, suffix)                                   \
    static inline type math_abs_##suffix(type x)                               \
    {                                                                          \
        return x < 0 ? (type) - x : x;                                         \
    }
MATH_SIGNED_TYPES(MATH_DEFINE_ABS_SIGNED)
#undef MATH_DEFINE_ABS_SIGNED

#define MATH_DEFINE_ABS_UNSIGNED(type, suffix)                                 \
    static inline type math_abs_##suffix(type x)                               \
    {                                                                          \
        return x;                                                              \
    }
MATH_UNSIGNED_TYPES(MATH_DEFINE_ABS_UNSIGNED)
#undef MATH_DEFINE_ABS_UNSIGNED

#define MATH_ABS_CASE(type, suffix) type: math_abs_##suffix,
#define MATH_ABS_CASES                                                         \
    MATH_REAL_TYPES(MATH_ABS_CASE)                                             \
    MATH_SIGNED_TYPES(MATH_ABS_CASE)                                           \
    MATH_UNSIGNED_TYPES(MATH_ABS_CASE)                                         \
    vec2: vec2_abs,
#define math_abs(x) _Generic((x), MATH_ABS_CASES default: math_abs_i32)(x)

#define MATH_DEFINE_CLAMP(type, suffix)                                        \
    static inline type math_clamp_##suffix(type x, type lo, type hi)           \
    {                                                                          \
        if (x < lo) {                                                          \
            return lo;                                                         \
        }                                                                      \
        if (x > hi) {                                                          \
            return hi;                                                         \
        }                                                                      \
        return x;                                                              \
    }
MATH_SCALAR_TYPES(MATH_DEFINE_CLAMP)
#undef MATH_DEFINE_CLAMP

#define MATH_CLAMP_CASE(type, suffix) type: math_clamp_##suffix,
#define MATH_CLAMP_CASES \
    MATH_SCALAR_TYPES(MATH_CLAMP_CASE) \
    vec2: vec2_clamp,
#define math_clamp(x, lo, hi)                                                  \
    _Generic((x), MATH_CLAMP_CASES default: math_clamp_i32)((x), (lo), (hi))

#define MATH_DEFINE_CLAMP01(type, suffix)                                      \
    static inline type math_clamp01_##suffix(type x)                           \
    {                                                                          \
        return math_clamp_##suffix(x, (type)0, (type)1);                       \
    }
MATH_SCALAR_TYPES(MATH_DEFINE_CLAMP01)
#undef MATH_DEFINE_CLAMP01

#define MATH_CLAMP01_CASE(type, suffix) type: math_clamp01_##suffix,
#define MATH_CLAMP01_CASES \
    MATH_SCALAR_TYPES(MATH_CLAMP01_CASE) \
    vec2: vec2_clamp01,
#define math_clamp01(x)                                                        \
    _Generic((x), MATH_CLAMP01_CASES default: math_clamp01_i32)(x)

#define MATH_DEFINE_SIGN_REAL(type, suffix)                                    \
    static inline type math_sign_##suffix(type x)                              \
    {                                                                          \
        return x < (type)0 ? (type) - 1 : (type)1;                             \
    }
MATH_REAL_TYPES(MATH_DEFINE_SIGN_REAL)
#undef MATH_DEFINE_SIGN_REAL

#define MATH_DEFINE_SIGN_SIGNED(type, suffix)                                  \
    static inline type math_sign_##suffix(type x)                              \
    {                                                                          \
        return x < 0 ? (type) - 1 : (type)1;                                   \
    }
MATH_SIGNED_TYPES(MATH_DEFINE_SIGN_SIGNED)
#undef MATH_DEFINE_SIGN_SIGNED

#define MATH_DEFINE_SIGN_UNSIGNED(type, suffix)                                \
    static inline type math_sign_##suffix(type)                                \
    {                                                                          \
        return (type)1;                                                        \
    }
MATH_UNSIGNED_TYPES(MATH_DEFINE_SIGN_UNSIGNED)
#undef MATH_DEFINE_SIGN_UNSIGNED

#define MATH_SIGN_CASE(type, suffix) type: math_sign_##suffix,
#define MATH_SIGN_CASES                                                        \
    MATH_REAL_TYPES(MATH_SIGN_CASE)                                            \
    MATH_SIGNED_TYPES(MATH_SIGN_CASE)                                          \
    MATH_UNSIGNED_TYPES(MATH_SIGN_CASE)                                        \
    vec2: vec2_sign,
#define math_sign(x) _Generic((x), MATH_SIGN_CASES default: math_sign_i32)(x)

#define MATH_DEFINE_INTERSECT(type, suffix)                                    \
    static inline type math_intersect_##suffix(type da, type db)               \
    {                                                                          \
        return da / (da - db);                                                 \
    }
MATH_REAL_TYPES(MATH_DEFINE_INTERSECT)
#undef MATH_DEFINE_INTERSECT

#define MATH_INTERSECT_CASE(type, suffix) type: math_intersect_##suffix,
#define MATH_INTERSECT_CASES MATH_REAL_TYPES(MATH_INTERSECT_CASE)
#define math_intersect(a, b)                                                   \
    _Generic((a), MATH_INTERSECT_CASES default: math_intersect_f)((a), (b))

#define MATH_DEFINE_REAL_UNARY(name, float_fn, double_fn)                      \
    static inline float math_##name##_f(float x)                               \
    {                                                                          \
        return float_fn(x);                                                    \
    }                                                                          \
    static inline double math_##name##_d(double x)                             \
    {                                                                          \
        return double_fn(x);                                                   \
    }

#define MATH_REAL_UNARY_FUNCTIONS(X)                                           \
    X(floor, floorf, floor)                                                    \
    X(ceil, ceilf, ceil)                                                       \
    X(round, roundf, round)                                                    \
    X(sin, sinf, sin)                                                          \
    X(cos, cosf, cos)                                                          \
    X(asin, asinf, asin)                                                       \
    X(acos, acosf, acos)                                                       \
    X(sqrt, sqrtf, sqrt)                                                       \
    X(exp, expf, exp)

MATH_REAL_UNARY_FUNCTIONS(MATH_DEFINE_REAL_UNARY)
#undef MATH_DEFINE_REAL_UNARY

#define MATH_DEFINE_REAL_BINARY(name, float_fn, double_fn)                     \
    static inline float math_##name##_f(float a, float b)                      \
    {                                                                          \
        return float_fn(a, b);                                                 \
    }                                                                          \
    static inline double math_##name##_d(double a, double b)                   \
    {                                                                          \
        return double_fn(a, b);                                                \
    }

#define MATH_REAL_BINARY_FUNCTIONS(X)                                          \
    X(atan2, atan2f, atan2)                                                    \
    X(pow, powf, pow)                                                          \
    X(fmod, fmodf, fmod)

MATH_REAL_BINARY_FUNCTIONS(MATH_DEFINE_REAL_BINARY)
#undef MATH_DEFINE_REAL_BINARY

#define MATH_REAL_CASES(name) \
    float: math_##name##_f, \
    double: math_##name##_d,

#define MATH_REAL_VEC2_CASES(name) \
    MATH_REAL_CASES(name) \
    vec2: vec2_##name,

#define math_floor(x)                                                          \
    _Generic((x), MATH_REAL_VEC2_CASES(floor) default: math_floor_f)(x)

#define math_ceil(x)                                                           \
    _Generic((x), MATH_REAL_VEC2_CASES(ceil) default: math_ceil_f)(x)

#define math_round(x)                                                          \
    _Generic((x), MATH_REAL_VEC2_CASES(round) default: math_round_f)(x)

#define math_sin(x) _Generic((x), MATH_REAL_VEC2_CASES(sin) default: math_sin_f)(x)

#define math_cos(x) _Generic((x), MATH_REAL_VEC2_CASES(cos) default: math_cos_f)(x)

#define math_asin(x)                                                           \
    _Generic((x), MATH_REAL_VEC2_CASES(asin) default: math_asin_f)(x)

#define math_acos(x)                                                           \
    _Generic((x), MATH_REAL_VEC2_CASES(acos) default: math_acos_f)(x)

#define math_sqrt(x)                                                           \
    _Generic((x), MATH_REAL_VEC2_CASES(sqrt) default: math_sqrt_f)(x)

#define math_exp(x) _Generic((x), MATH_REAL_VEC2_CASES(exp) default: math_exp_f)(x)

#define math_atan2(y, x)                                                       \
    _Generic((y), MATH_REAL_CASES(atan2) default: math_atan2_f)((y), (x))

/* The base selects precision; cast it when mixed operands need a type. */
#define math_pow(base, exponent)                                               \
    _Generic((base), MATH_REAL_CASES(pow) default: math_pow_f)((base), (exponent))

#define math_fmod(a, b)                                                        \
    _Generic((a), MATH_REAL_VEC2_CASES(fmod) default: math_fmod_f)((a), (b))

#define MATH_DEFINE_FRACT(type, suffix)                                        \
    static inline type math_fract_##suffix(type x)                             \
    {                                                                          \
        return x - math_floor_##suffix(x);                                     \
    }
MATH_REAL_TYPES(MATH_DEFINE_FRACT)
#undef MATH_DEFINE_FRACT

#define MATH_DEFINE_REMAP(type, suffix)                                                        \
    static inline type math_remap_##suffix(type t, type old_lo, type old_hi, type lo, type hi) \
    {                                                                                          \
        return lo + ((old_hi - old_lo) != (type)0 ? (t - old_lo) / (old_hi - old_lo)           \
                                                  : (type)0) *                                 \
                        (hi - lo);                                                             \
    }
MATH_REAL_TYPES(MATH_DEFINE_REMAP)
#undef MATH_DEFINE_REMAP

#define MATH_DEFINE_REMAP01(type, suffix)                                      \
    static inline type math_remap01_##suffix(type t, type lo, type hi)         \
    {                                                                          \
        return lo + t * (hi - lo);                                             \
    }
MATH_REAL_TYPES(MATH_DEFINE_REMAP01)
#undef MATH_DEFINE_REMAP01

/* math_mod uses floor-based semantics; use fmod/fmodf for C remainder. */
#define MATH_DEFINE_MOD(type, suffix)                                          \
    static inline type math_mod_##suffix(type x, type m)                       \
    {                                                                          \
        return x - math_floor_##suffix(x / m) * m;                             \
    }
MATH_REAL_TYPES(MATH_DEFINE_MOD)
#undef MATH_DEFINE_MOD

#define MATH_DEFINE_LERP(type, suffix)                                         \
    static inline type math_lerp_##suffix(type a, type b, type t)              \
    {                                                                          \
        return a + (b - a) * t;                                                \
    }
MATH_REAL_TYPES(MATH_DEFINE_LERP)
#undef MATH_DEFINE_LERP

#define MATH_DEFINE_SAFE_INVERT(type, suffix)                                  \
    static inline type math_safe_invert_##suffix(type x)                       \
    {                                                                          \
        return x != (type)0 ? (type)1 / x : (type)0;                           \
    }
MATH_REAL_TYPES(MATH_DEFINE_SAFE_INVERT)
#undef MATH_DEFINE_SAFE_INVERT

#define math_fract(x)                                                          \
    _Generic((x), MATH_REAL_VEC2_CASES(fract) default: math_fract_f)(x)

#define math_remap(t, old_lo, old_hi, lo, hi)                                  \
    _Generic((t), MATH_REAL_VEC2_CASES(remap) default: math_remap_f)((t), (old_lo), (old_hi), (lo), (hi))

#define math_remap01(t, lo, hi)                                                \
    _Generic((t), MATH_REAL_VEC2_CASES(remap01) default: math_remap01_f)((t), (lo), (hi))

#define math_mod(x, m)                                                         \
    _Generic((x), MATH_REAL_VEC2_CASES(mod) default: math_mod_f)((x), (m))

#define MATH_LERP_CASES                                                        \
    MATH_REAL_CASES(lerp)                                                      \
    vec2: vec2_lerp,

#define math_lerp(a, b, t)                                                     \
    _Generic((a), MATH_LERP_CASES default: math_lerp_f)((a), (b), (t))

#define math_safe_invert(x)                                                    \
    _Generic((x), MATH_REAL_VEC2_CASES(safe_invert) default: math_safe_invert_f)(x)
// clang-format on

static inline bool math_is_even(int x)
{
    return (x % 2) == 0;
}

static inline bool math_is_odd(int x)
{
    return !math_is_even(x);
}

#define MATH_PI 3.14159265358979323846
#define MATH_PI_F 3.14159265358979323846f
#define MATH_TAU (2.0 * MATH_PI)
#define MATH_TAU_F (2.0f * MATH_PI_F)

static inline float math_atan2_360_f(float y, float x)
{
    return math_atan2_f(-y, -x) + MATH_TAU_F * 0.5f;
}

static inline double math_atan2_360_d(double y, double x)
{
    return math_atan2_d(-y, -x) + MATH_TAU * 0.5;
}

#define math_atan2_360(y, x)                                                   \
    _Generic((y), float: math_atan2_360_f, double: math_atan2_360_d)((y), (x))

/* Easing functions from cute_framework's scalar math surface. */
static inline float ease_smoothstep(float x)
{
    return x * x * (3.0f - 2.0f * x);
}

static inline float ease_quad_in(float x)
{
    return x * x;
}

static inline float ease_quad_out(float x)
{
    return -(x * (x - 2.0f));
}

static inline float ease_quad_in_out(float x)
{
    if (x < 0.5f) {
        return 2.0f * x * x;
    }
    return (-2.0f * x * x) + (4.0f * x) - 1.0f;
}

static inline float ease_cube_in(float x)
{
    return x * x * x;
}

static inline float ease_cube_out(float x)
{
    float f = x - 1.0f;
    return f * f * f + 1.0f;
}

static inline float ease_cube_in_out(float x)
{
    if (x < 0.5f) {
        return 4.0f * x * x * x;
    }
    float f = 2.0f * x - 2.0f;
    return 0.5f * f * f * f + 1.0f;
}

static inline float ease_quart_in(float x)
{
    return x * x * x * x;
}

static inline float ease_quart_out(float x)
{
    float f = x - 1.0f;
    return f * f * f * (1.0f - x) + 1.0f;
}

static inline float ease_quart_in_out(float x)
{
    if (x < 0.5f) {
        return 8.0f * x * x * x * x;
    }
    float f = x - 1.0f;
    return -8.0f * f * f * f * f + 1.0f;
}

static inline float ease_quint_in(float x)
{
    return x * x * x * x * x;
}

static inline float ease_quint_out(float x)
{
    float f = x - 1.0f;
    return f * f * f * f * f + 1.0f;
}

static inline float ease_quint_in_out(float x)
{
    if (x < 0.5f) {
        return 16.0f * x * x * x * x * x;
    }
    float f = 2.0f * x - 2.0f;
    return 0.5f * f * f * f * f * f + 1.0f;
}

static inline float ease_sin_in(float x)
{
    return math_sin_f((x - 1.0f) * MATH_PI_F * 0.5f) + 1.0f;
}

static inline float ease_sin_out(float x)
{
    return math_sin_f(x * (MATH_PI_F * 0.5f));
}

static inline float ease_sin_in_out(float x)
{
    return 0.5f * (1.0f - math_cos_f(x * MATH_PI_F));
}

static inline float ease_circle_in(float x)
{
    return 1.0f - math_sqrt_f(1.0f - x * x);
}

static inline float ease_circle_out(float x)
{
    return math_sqrt_f((2.0f - x) * x);
}

static inline float ease_circle_in_out(float x)
{
    if (x < 0.5f) {
        return 0.5f * (1.0f - math_sqrt_f(1.0f - 4.0f * x * x));
    }
    return 0.5f * (math_sqrt_f(-((2.0f * x) - 3.0f) * ((2.0f * x) - 1.0f)) + 1.0f);
}

static inline float ease_back_in(float x)
{
    return x * x * x - x * math_sin_f(x * MATH_PI_F);
}

static inline float ease_back_out(float x)
{
    float f = 1.0f - x;
    return 1.0f - (x * x * x - x * math_sin_f(f * MATH_PI_F));
}

static inline float ease_back_in_out(float x)
{
    if (x < 0.5f) {
        float f = 2.0f * x;
        return 0.5f * (f * f * f - f * math_sin_f(f * MATH_PI_F));
    }
    float f = 1.0f - (2.0f * x - 1.0f);
    return 0.5f * (1.0f - (f * f * f - f * math_sin_f(f * MATH_PI_F))) + 0.5f;
}

static inline vec2 vec2_min(vec2 a, vec2 b)
{
    return vec2_make(math_min_f(a.x, b.x), math_min_f(a.y, b.y));
}

static inline vec2 vec2_max(vec2 a, vec2 b)
{
    return vec2_make(math_max_f(a.x, b.x), math_max_f(a.y, b.y));
}

static inline vec2 vec2_abs(vec2 a)
{
    return vec2_make(math_abs_f(a.x), math_abs_f(a.y));
}

static inline vec2 vec2_clamp(vec2 a, vec2 lo, vec2 hi)
{
    return vec2_make(math_clamp_f(a.x, lo.x, hi.x), math_clamp_f(a.y, lo.y, hi.y));
}

static inline vec2 vec2_clamp01(vec2 a)
{
    return vec2_clamp(a, vec2_splat(0.0f), vec2_splat(1.0f));
}

static inline vec2 vec2_sign(vec2 a)
{
    return vec2_make(math_sign_f(a.x), math_sign_f(a.y));
}

static inline vec2 vec2_safe_invert(vec2 a)
{
    return vec2_make(math_safe_invert_f(a.x), math_safe_invert_f(a.y));
}

static inline vec2 vec2_floor(vec2 a)
{
    return vec2_make(math_floor_f(a.x), math_floor_f(a.y));
}

static inline vec2 vec2_ceil(vec2 a)
{
    return vec2_make(math_ceil_f(a.x), math_ceil_f(a.y));
}

static inline vec2 vec2_round(vec2 a)
{
    return vec2_make(math_round_f(a.x), math_round_f(a.y));
}

static inline vec2 vec2_fract(vec2 a)
{
    return vec2_make(math_fract_f(a.x), math_fract_f(a.y));
}

static inline vec2 vec2_remap(vec2 t, vec2 old_lo, vec2 old_hi, vec2 lo, vec2 hi)
{
    return vec2_make(
        math_remap_f(t.x, old_lo.x, old_hi.x, lo.x, hi.x),
        math_remap_f(t.y, old_lo.y, old_hi.y, lo.y, hi.y)
    );
}

static inline vec2 vec2_remap01(vec2 t, vec2 lo, vec2 hi)
{
    return vec2_make(math_remap01_f(t.x, lo.x, hi.x), math_remap01_f(t.y, lo.y, hi.y));
}

static inline vec2 vec2_mod(vec2 a, vec2 b)
{
    return vec2_make(math_mod_f(a.x, b.x), math_mod_f(a.y, b.y));
}

static inline vec2 vec2_sin(vec2 a)
{
    return vec2_make(math_sin_f(a.x), math_sin_f(a.y));
}

static inline vec2 vec2_cos(vec2 a)
{
    return vec2_make(math_cos_f(a.x), math_cos_f(a.y));
}

static inline vec2 vec2_asin(vec2 a)
{
    return vec2_make(math_asin_f(a.x), math_asin_f(a.y));
}

static inline vec2 vec2_acos(vec2 a)
{
    return vec2_make(math_acos_f(a.x), math_acos_f(a.y));
}

static inline vec2 vec2_exp(vec2 a)
{
    return vec2_make(math_exp_f(a.x), math_exp_f(a.y));
}

static inline vec2 vec2_fmod(vec2 a, vec2 b)
{
    return vec2_make(fmodf(a.x, b.x), fmodf(a.y, b.y));
}

static inline vec2 vec2_sqrt(vec2 a)
{
    return vec2_make(math_sqrt_f(a.x), math_sqrt_f(a.y));
}

static inline float vec2_hmin(vec2 a)
{
    return math_min_f(a.x, a.y);
}

static inline float vec2_hmax(vec2 a)
{
    return math_max_f(a.x, a.y);
}

#endif
