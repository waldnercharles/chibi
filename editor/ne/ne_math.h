//------------------------------------------------------------------------------
// ne_math.h — C23 port of imgui_extra_math.h/.inl + imgui_bezier_math.h/.inl
// (imgui-node-editor, Michal Cichon).
//
// Upstream is dual-licensed to the public domain; this port keeps the same
// intent. Everything is specialized to ImVec2 (templates<T> and ImVec3 dropped;
// the editor only uses ImVec2). Operator overloads become named static inline
// functions, so a diff against upstream stays mechanical.
//
//   upstream operator==(ImVec2, ImVec2)        -> ne_vec2_eq
//   upstream operator!=(ImVec2, ImVec2)        -> ne_vec2_ne
//   upstream operator+(ImVec2, ImVec2)         -> ne_vec2_add
//   upstream operator-(ImVec2, ImVec2)         -> ne_vec2_sub
//   upstream operator-(ImVec2)                 -> ne_vec2_neg
//   upstream operator*(ImVec2, ImVec2)         -> ne_vec2_mul
//   upstream operator*(float, ImVec2)          -> ne_vec2_scale  (also vec * float)
//   upstream operator/(ImVec2, ImVec2)         -> ne_vec2_div
//   upstream operator/(ImVec2, float)          -> ne_vec2_div_scalar
//   upstream ImDot(ImVec2, ImVec2)             -> ne_vec2_dot
//   upstream ImLength(ImVec2)                  -> ne_vec2_length
//   upstream ImLengthSqr(ImVec2)               -> ne_vec2_length_sq
//   upstream ImNormalized(ImVec2)              -> ne_vec2_normalized
//   upstream ImMin(ImVec2, ImVec2)             -> ne_vec2_min
//   upstream ImMax(ImVec2, ImVec2)             -> ne_vec2_max
//------------------------------------------------------------------------------
#ifndef NE_MATH_H
#define NE_MATH_H

#include <dcimgui.h>
/* dcimgui_internal.h uses IMGUI_STB_NAMESPACE::stbrp_node (C++ '::') at
   ImFontAtlasBuilder; we don't touch that type, so undef the namespace to get
   the plain-C stbrp_node branch. */
#undef IMGUI_STB_NAMESPACE
#include <dcimgui_internal.h> /* ImRect */
#include <float.h>            /* FLT_MAX */
#include <math.h> /* sqrtf, fabsf, powf, acosf, cosf, fminf, fmaxf */

#define NE_PI 3.14159265358979323846f

//------------------------------------------------------------------------------
typedef struct ne_line
{
    ImVec2 A, B;
} ne_line;

//------------------------------------------------------------------------------
// ImVec2 operators (imgui.h IMGUI_DEFINE_MATH_OPERATORS + imgui_extra_math.h)

static inline bool ne_vec2_eq(ImVec2 a, ImVec2 b)
{ return a.x == b.x && a.y == b.y; }

static inline bool ne_vec2_ne(ImVec2 a, ImVec2 b)
{ return a.x != b.x || a.y != b.y; }

static inline ImVec2 ne_vec2_add(ImVec2 a, ImVec2 b)
{ return (ImVec2){ a.x + b.x, a.y + b.y }; }

static inline ImVec2 ne_vec2_sub(ImVec2 a, ImVec2 b)
{ return (ImVec2){ a.x - b.x, a.y - b.y }; }

static inline ImVec2 ne_vec2_neg(ImVec2 a) { return (ImVec2){ -a.x, -a.y }; }

static inline ImVec2 ne_vec2_mul(ImVec2 a, ImVec2 b)
{ return (ImVec2){ a.x * b.x, a.y * b.y }; }

static inline ImVec2 ne_vec2_scale(ImVec2 v, float s)
{ return (ImVec2){ v.x * s, v.y * s }; }

static inline ImVec2 ne_vec2_div(ImVec2 a, ImVec2 b)
{ return (ImVec2){ a.x / b.x, a.y / b.y }; }

static inline ImVec2 ne_vec2_div_scalar(ImVec2 v, float s)
{ return (ImVec2){ v.x / s, v.y / s }; }

static inline float ne_vec2_dot(ImVec2 a, ImVec2 b)
{ return a.x * b.x + a.y * b.y; }

static inline float ne_vec2_length_sq(ImVec2 v) { return ne_vec2_dot(v, v); }

static inline float ne_vec2_length(ImVec2 v)
{ return sqrtf(ne_vec2_length_sq(v)); }

static inline ImVec2 ne_vec2_normalized(ImVec2 v)
{
    float d = ne_vec2_length_sq(v);
    if (d > 0.0f)
        return ne_vec2_scale(v, 1.0f / sqrtf(d)); /* ImInvLength(v, 0.0f) */
    return (ImVec2){ 0.0f, 0.0f };
}

static inline ImVec2 ne_vec2_min(ImVec2 a, ImVec2 b)
{ return (ImVec2){ fminf(a.x, b.x), fminf(a.y, b.y) }; }

static inline ImVec2 ne_vec2_max(ImVec2 a, ImVec2 b)
{ return (ImVec2){ fmaxf(a.x, b.x), fmaxf(a.y, b.y) }; }

//------------------------------------------------------------------------------
// ImRect helpers (imgui_extra_math.inl)

static inline bool ne_rect_is_empty(ImRect rect)
{ return rect.Min.x >= rect.Max.x || rect.Min.y >= rect.Max.y; }

static inline bool ne_rect_contains(ImRect rect, ImVec2 p)
{
    return p.x >= rect.Min.x && p.y >= rect.Min.y && p.x <= rect.Max.x &&
           p.y <= rect.Max.y;
}

static inline ImVec2 ne_rect_closest_point(ImRect rect, ImVec2 p, bool snap_to_edge)
{
    if (!snap_to_edge && ne_rect_contains(rect, p)) return p;

    return (ImVec2){
        (p.x > rect.Max.x) ? rect.Max.x : (p.x < rect.Min.x ? rect.Min.x : p.x),
        (p.y > rect.Max.y) ? rect.Max.y : (p.y < rect.Min.y ? rect.Min.y : p.y)
    };
}

static inline ImVec2 ne_rect_closest_point_radius(ImRect rect, ImVec2 p, bool snap_to_edge, float radius)
{
    ImVec2 point = ne_rect_closest_point(rect, p, snap_to_edge);

    ImVec2 offset = ne_vec2_sub(p, point);
    float distance_sq = offset.x * offset.x + offset.y * offset.y;
    if (distance_sq <= 0) return point;

    float distance = sqrtf(distance_sq);

    return ne_vec2_add(point, ne_vec2_scale(offset, fminf(distance, radius) * (1.0f / distance)));
}

static inline ImVec2 ne_rect_closest_point_rect(ImRect rect, ImRect other)
{
    ImVec2 result;
    if (other.Min.x >= rect.Max.x) result.x = rect.Max.x;
    else if (other.Max.x <= rect.Min.x) result.x = rect.Min.x;
    else
        result.x = (fmaxf(rect.Min.x, other.Min.x) + fminf(rect.Max.x, other.Max.x)) / 2;

    if (other.Min.y >= rect.Max.y) result.y = rect.Max.y;
    else if (other.Max.y <= rect.Min.y) result.y = rect.Min.y;
    else
        result.y = (fmaxf(rect.Min.y, other.Min.y) + fminf(rect.Max.y, other.Max.y)) / 2;

    return result;
}

/* upstream: local lambda `distribute(float& a, float& b, ...)` */
static inline void ne_distribute(float *a, float *b, float a0, float a1, float b0, float b1)
{
    if (a0 >= b1 || a1 <= b0) return;

    const float aw = a1 - a0;
    const float bw = b1 - b0;

    if (aw > bw) {
        *b = b0 + bw - bw * (*a - a0) / aw;
        *a = *b;
    } else if (aw < bw) {
        *a = a0 + aw - aw * (*b - b0) / bw;
        *b = *a;
    }
}

static inline ne_line ne_rect_closest_line(ImRect rect_a, ImRect rect_b)
{
    ne_line result;
    result.A = ne_rect_closest_point_rect(rect_a, rect_b);
    result.B = ne_rect_closest_point_rect(rect_b, rect_a);

    ne_distribute(
        &result.A.x,
        &result.B.x,
        rect_a.Min.x,
        rect_a.Max.x,
        rect_b.Min.x,
        rect_b.Max.x
    );
    ne_distribute(
        &result.A.y,
        &result.B.y,
        rect_a.Min.y,
        rect_a.Max.y,
        rect_b.Min.y,
        rect_b.Max.y
    );

    return result;
}

static inline ne_line ne_rect_closest_line_radius(ImRect rect_a, ImRect rect_b, float radius_a, float radius_b)
{
    ne_line line = ne_rect_closest_line(rect_a, rect_b);
    if (radius_a < 0) radius_a = 0;
    if (radius_b < 0) radius_b = 0;

    if (radius_a == 0 && radius_b == 0) return line;

    const ImVec2 offset = ne_vec2_sub(line.B, line.A);
    const float length_sq = offset.x * offset.x + offset.y * offset.y;
    const float radius_a_sq = radius_a * radius_a;
    const float radius_b_sq = radius_b * radius_b;

    if (length_sq <= 0) return line;

    const float length = sqrtf(length_sq);
    const ImVec2 direction = (ImVec2){ offset.x / length, offset.y / length };

    const float total_radius_sq = radius_a_sq + radius_b_sq;
    if (total_radius_sq > length_sq) {
        const float scale = length / (radius_a + radius_b);
        radius_a *= scale;
        radius_b *= scale;
    }

    line.A = ne_vec2_add(line.A, ne_vec2_scale(direction, radius_a));
    line.B = ne_vec2_sub(line.B, ne_vec2_scale(direction, radius_b));

    return line;
}

//------------------------------------------------------------------------------
// Bezier math (imgui_bezier_math.inl), specialized to ImVec2.

struct ne_cubic_bezier_points
{
    ImVec2 P0;
    ImVec2 P1;
    ImVec2 P2;
    ImVec2 P3;
};

struct ne_cubic_bezier_split_result
{
    struct ne_cubic_bezier_points Left;
    struct ne_cubic_bezier_points Right;
};

struct ne_project_result
{
    ImVec2 Point;   /* Point on curve */
    float Time;     /* [0 - 1] */
    float Distance; /* Distance to curve */
};

struct ne_cubic_bezier_intersect_result
{
    int Count;
    ImVec2 Points[3];
};

enum ne_cubic_bezier_subdivide_flags
{
    ne_cubic_bezier_subdivide_none = 0,
    ne_cubic_bezier_subdivide_skip_first = 1
};

struct ne_cubic_bezier_subdivide_sample
{
    ImVec2 Point;
    ImVec2 Tangent;
};

typedef void (*ne_cubic_bezier_subdivide_callback)(
    const struct ne_cubic_bezier_subdivide_sample *p,
    void *user_pointer
);

struct ne_cubic_bezier_fixed_step_sample
{
    float T;
    float Length;
    ImVec2 Point;
    bool BreakSearch;
};

typedef void (*ne_cubic_bezier_fixed_step_callback)(
    struct ne_cubic_bezier_fixed_step_sample *sample,
    void *user_pointer
);

//------------------------------------------------------------------------------

static inline ImVec2 ne_linear_bezier(ImVec2 p0, ImVec2 p1, float t)
{ return ne_vec2_add(p0, ne_vec2_scale(ne_vec2_sub(p1, p0), t)); }

static inline ImVec2 ne_linear_bezier_dt(ImVec2 p0, ImVec2 p1, float t)
{
    (void)t;
    return ne_vec2_sub(p1, p0);
}

static inline ImVec2 ne_quadratic_bezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, float t)
{
    const float a = 1 - t;

    return ne_vec2_add(
        ne_vec2_add(ne_vec2_scale(p0, a * a), ne_vec2_scale(p1, 2 * t * a)),
        ne_vec2_scale(p2, t * t)
    );
}

static inline ImVec2 ne_quadratic_bezier_dt(ImVec2 p0, ImVec2 p1, ImVec2 p2, float t)
{
    return ne_vec2_add(
        ne_vec2_scale(ne_vec2_sub(p1, p0), 2 * (1 - t)),
        ne_vec2_scale(ne_vec2_sub(p2, p1), 2 * t)
    );
}

static inline ImVec2 ne_cubic_bezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t)
{
    const float a = 1 - t;
    const float b = a * a * a;
    const float c = t * t * t;

    return ne_vec2_add(
        ne_vec2_add(
            ne_vec2_add(ne_vec2_scale(p0, b), ne_vec2_scale(p1, 3 * t * a * a)),
            ne_vec2_scale(p2, 3 * t * t * a)
        ),
        ne_vec2_scale(p3, c)
    );
}

static inline ImVec2 ne_cubic_bezier_dt(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t)
{
    const float a = 1 - t;
    const float b = a * a;
    const float c = t * t;
    const float d = 2 * t * a;

    return ne_vec2_add(
        ne_vec2_add(
            ne_vec2_add(ne_vec2_scale(ne_vec2_neg(p0), 3 * b), ne_vec2_scale(p1, 3 * (b - d))),
            ne_vec2_scale(p2, 3 * (d - c))
        ),
        ne_vec2_scale(p3, 3 * c)
    );
}

/* Scalar instantiation ImCubicBezier<float>, used by ne_cubic_bezier_bounding_rect. */
static inline float ne_cubic_bezier_1d(float p0, float p1, float p2, float p3, float t)
{
    const float a = 1 - t;
    const float b = a * a * a;
    const float c = t * t * t;

    return b * p0 + 3 * t * a * a * p1 + 3 * t * t * a * p2 + c * p3;
}

static inline ImVec2 ne_cubic_bezier_sample(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t)
{
    const bool cp0_zero = ne_vec2_length_sq(ne_vec2_sub(p1, p0)) < 1e-5f;
    const bool cp1_zero = ne_vec2_length_sq(ne_vec2_sub(p3, p2)) < 1e-5f;

    if (cp0_zero && cp1_zero) return ne_linear_bezier(p0, p3, t);
    else if (cp0_zero) return ne_quadratic_bezier(p0, p2, p3, t);
    else if (cp1_zero) return ne_quadratic_bezier(p0, p1, p3, t);
    else return ne_cubic_bezier(p0, p1, p2, p3, t);
}

static inline ImVec2 ne_cubic_bezier_sample_pts(struct ne_cubic_bezier_points curve, float t)
{ return ne_cubic_bezier_sample(curve.P0, curve.P1, curve.P2, curve.P3, t); }

static inline ImVec2 ne_cubic_bezier_tangent(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t)
{
    const bool cp0_zero = ne_vec2_length_sq(ne_vec2_sub(p1, p0)) < 1e-5f;
    const bool cp1_zero = ne_vec2_length_sq(ne_vec2_sub(p3, p2)) < 1e-5f;

    if (cp0_zero && cp1_zero) return ne_linear_bezier_dt(p0, p3, t);
    else if (cp0_zero) return ne_quadratic_bezier_dt(p0, p2, p3, t);
    else if (cp1_zero) return ne_quadratic_bezier_dt(p0, p1, p3, t);
    else return ne_cubic_bezier_dt(p0, p1, p2, p3, t);
}

static inline ImVec2 ne_cubic_bezier_tangent_pts(struct ne_cubic_bezier_points curve, float t)
{ return ne_cubic_bezier_tangent(curve.P0, curve.P1, curve.P2, curve.P3, t); }

static inline float ne_cubic_bezier_length(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3)
{
    /* Legendre-Gauss abscissae with n=24 */
    static const float t_values[] = {
        -0.0640568928626056260850430826247450385909f,
        0.0640568928626056260850430826247450385909f,
        -0.1911188674736163091586398207570696318404f,
        0.1911188674736163091586398207570696318404f,
        -0.3150426796961633743867932913198102407864f,
        0.3150426796961633743867932913198102407864f,
        -0.4337935076260451384870842319133497124524f,
        0.4337935076260451384870842319133497124524f,
        -0.5454214713888395356583756172183723700107f,
        0.5454214713888395356583756172183723700107f,
        -0.6480936519369755692524957869107476266696f,
        0.6480936519369755692524957869107476266696f,
        -0.7401241915785543642438281030999784255232f,
        0.7401241915785543642438281030999784255232f,
        -0.8200019859739029219539498726697452080761f,
        0.8200019859739029219539498726697452080761f,
        -0.8864155270044010342131543419821967550873f,
        0.8864155270044010342131543419821967550873f,
        -0.9382745520027327585236490017087214496548f,
        0.9382745520027327585236490017087214496548f,
        -0.9747285559713094981983919930081690617411f,
        0.9747285559713094981983919930081690617411f,
        -0.9951872199970213601799974097007368118745f,
        0.9951872199970213601799974097007368118745f
    };

    /* Legendre-Gauss weights with n=24 */
    static const float c_values[] = {
        0.1279381953467521569740561652246953718517f,
        0.1279381953467521569740561652246953718517f,
        0.1258374563468282961213753825111836887264f,
        0.1258374563468282961213753825111836887264f,
        0.1216704729278033912044631534762624256070f,
        0.1216704729278033912044631534762624256070f,
        0.1155056680537256013533444839067835598622f,
        0.1155056680537256013533444839067835598622f,
        0.1074442701159656347825773424466062227946f,
        0.1074442701159656347825773424466062227946f,
        0.0976186521041138882698806644642471544279f,
        0.0976186521041138882698806644642471544279f,
        0.0861901615319532759171852029837426671850f,
        0.0861901615319532759171852029837426671850f,
        0.0733464814110803057340336152531165181193f,
        0.0733464814110803057340336152531165181193f,
        0.0592985849154367807463677585001085845412f,
        0.0592985849154367807463677585001085845412f,
        0.0442774388174198061686027482113382288593f,
        0.0442774388174198061686027482113382288593f,
        0.0285313886289336631813078159518782864491f,
        0.0285313886289336631813078159518782864491f,
        0.0123412297999871995468056670700372915759f,
        0.0123412297999871995468056670700372915759f
    };

    const float z = 0.5f;
    const size_t n = sizeof(t_values) / sizeof(*t_values);

    float accumulator = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float t = z * t_values[i] + z;
        const ImVec2 p = ne_cubic_bezier_dt(p0, p1, p2, p3, t);
        accumulator += c_values[i] * ne_vec2_length(p);
    }

    return z * accumulator;
}

static inline float ne_cubic_bezier_length_pts(struct ne_cubic_bezier_points curve)
{ return ne_cubic_bezier_length(curve.P0, curve.P1, curve.P2, curve.P3); }

static inline struct ne_cubic_bezier_split_result ne_cubic_bezier_split(
    ImVec2 p0,
    ImVec2 p1,
    ImVec2 p2,
    ImVec2 p3,
    float t
)
{
    const float z1 = t;
    const float z2 = z1 * z1;
    const float z3 = z1 * z1 * z1;
    const float s1 = z1 - 1;
    const float s2 = s1 * s1;
    const float s3 = s1 * s1 * s1;

    struct ne_cubic_bezier_split_result result;
    result.Left.P0 = p0;
    result.Left.P1 = ne_vec2_sub(ne_vec2_scale(p1, z1), ne_vec2_scale(p0, s1));
    result.Left.P2 = ne_vec2_add(
        ne_vec2_sub(ne_vec2_scale(p2, z2), ne_vec2_scale(p1, 2 * z1 * s1)),
        ne_vec2_scale(p0, s2)
    );
    result.Left.P3 = ne_vec2_add(
        ne_vec2_sub(
            ne_vec2_add(ne_vec2_scale(p3, z3), ne_vec2_scale(p1, 3 * z1 * s2)),
            ne_vec2_scale(p2, 3 * z2 * s1)
        ),
        ne_vec2_scale(ne_vec2_neg(p0), s3)
    );

    result.Right.P0 = ne_vec2_add(
        ne_vec2_sub(
            ne_vec2_add(ne_vec2_scale(p0, z3), ne_vec2_scale(p2, 3 * z1 * s2)),
            ne_vec2_scale(p1, 3 * z2 * s1)
        ),
        ne_vec2_scale(ne_vec2_neg(p3), s3)
    );
    result.Right.P1 = ne_vec2_add(
        ne_vec2_sub(ne_vec2_scale(p1, z2), ne_vec2_scale(p2, 2 * z1 * s1)),
        ne_vec2_scale(p3, s2)
    );
    result.Right.P2 = ne_vec2_sub(ne_vec2_scale(p2, z1), ne_vec2_scale(p3, s1));
    result.Right.P3 = p3;

    return result;
}

static inline struct ne_cubic_bezier_split_result ne_cubic_bezier_split_pts(
    struct ne_cubic_bezier_points curve,
    float t
)
{ return ne_cubic_bezier_split(curve.P0, curve.P1, curve.P2, curve.P3, t); }

#define NE_VEC2_INDEX(v, i) (*((float *)&(v) + (i)))

static inline ImRect ne_cubic_bezier_bounding_rect(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3)
{
    ImVec2 a = ne_vec2_sub(
        ne_vec2_add(ne_vec2_sub(ne_vec2_scale(p3, 3), ne_vec2_scale(p2, 9)), ne_vec2_scale(p1, 9)),
        ne_vec2_scale(p0, 3)
    );
    ImVec2 b = ne_vec2_sub(
        ne_vec2_add(ne_vec2_scale(p0, 6), ne_vec2_scale(p1, -12)),
        ne_vec2_scale(p2, -6)
    );
    ImVec2 c = ne_vec2_sub(ne_vec2_scale(p1, 3), ne_vec2_scale(p0, 3));
    ImVec2 delta_squared = ne_vec2_sub(ne_vec2_mul(b, b), ne_vec2_scale(ne_vec2_mul(a, c), 4));

    ImVec2 tl = ne_vec2_min(p0, p3);
    ImVec2 rb = ne_vec2_max(p0, p3);

    for (int i = 0; i < 2; ++i) {
        if (NE_VEC2_INDEX(a, i) == 0.0f) continue;

        if (NE_VEC2_INDEX(delta_squared, i) >= 0) {
            const float delta = sqrtf(NE_VEC2_INDEX(delta_squared, i));

            const float t0 = (-NE_VEC2_INDEX(b, i) + delta) /
                             (2 * NE_VEC2_INDEX(a, i));
            if (t0 > 0 && t0 < 1) {
                const float p = ne_cubic_bezier_1d(
                    NE_VEC2_INDEX(p0, i),
                    NE_VEC2_INDEX(p1, i),
                    NE_VEC2_INDEX(p2, i),
                    NE_VEC2_INDEX(p3, i),
                    t0
                );
                NE_VEC2_INDEX(tl, i) = fminf(NE_VEC2_INDEX(tl, i), p);
                NE_VEC2_INDEX(rb, i) = fmaxf(NE_VEC2_INDEX(rb, i), p);
            }

            const float t1 = (-NE_VEC2_INDEX(b, i) - delta) /
                             (2 * NE_VEC2_INDEX(a, i));
            if (t1 > 0 && t1 < 1) {
                const float p = ne_cubic_bezier_1d(
                    NE_VEC2_INDEX(p0, i),
                    NE_VEC2_INDEX(p1, i),
                    NE_VEC2_INDEX(p2, i),
                    NE_VEC2_INDEX(p3, i),
                    t1
                );
                NE_VEC2_INDEX(tl, i) = fminf(NE_VEC2_INDEX(tl, i), p);
                NE_VEC2_INDEX(rb, i) = fmaxf(NE_VEC2_INDEX(rb, i), p);
            }
        }
    }

    return (ImRect){ tl, rb };
}

#undef NE_VEC2_INDEX

static inline ImRect ne_cubic_bezier_bounding_rect_pts(struct ne_cubic_bezier_points curve)
{
    return ne_cubic_bezier_bounding_rect(curve.P0, curve.P1, curve.P2, curve.P3);
}

static inline struct ne_project_result ne_project_on_cubic_bezier(
    ImVec2 point,
    ImVec2 p0,
    ImVec2 p1,
    ImVec2 p2,
    ImVec2 p3,
    int subdivisions
)
{
    /* http://pomax.github.io/bezierinfo/#projections */

    const float epsilon = 1e-5f;
    const float fixed_step = 1.0f / (float)(subdivisions - 1);

    struct ne_project_result result;
    result.Point = point;
    result.Time = 0.0f;
    result.Distance = FLT_MAX;

    /* Step 1: Coarse check */
    for (int i = 0; i < subdivisions; ++i) {
        const float t = i * fixed_step;
        const ImVec2 p = ne_cubic_bezier(p0, p1, p2, p3, t);
        const ImVec2 s = ne_vec2_sub(point, p);
        const float d = ne_vec2_dot(s, s);

        if (d < result.Distance) {
            result.Point = p;
            result.Time = t;
            result.Distance = d;
        }
    }

    if (result.Time == 0.0f || fabsf(result.Time - 1.0f) <= epsilon) {
        result.Distance = sqrtf(result.Distance);
        return result;
    }

    /* Step 2: Fine check */
    const float left = result.Time - fixed_step;
    const float right = result.Time + fixed_step;
    const float step = fixed_step * 0.1f;

    for (float t = left; t < right + step; t += step) {
        const ImVec2 p = ne_cubic_bezier(p0, p1, p2, p3, t);
        const ImVec2 s = ne_vec2_sub(point, p);
        const float d = ne_vec2_dot(s, s);

        if (d < result.Distance) {
            result.Point = p;
            result.Time = t;
            result.Distance = d;
        }
    }

    result.Distance = sqrtf(result.Distance);

    return result;
}

static inline struct ne_project_result ne_project_on_cubic_bezier_pts(
    ImVec2 p,
    struct ne_cubic_bezier_points curve,
    int subdivisions
)
{
    return ne_project_on_cubic_bezier(p, curve.P0, curve.P1, curve.P2, curve.P3, subdivisions);
}

/* upstream: local lambda `sign(float x)` inside cubic_roots */
static inline float ne_cubic_roots_sign(float x)
{ return x < 0 ? -1.0f : 1.0f; }

/* upstream: local lambda `cubic_roots(float a, float b, float c, float d, float* roots)` */
static inline int ne_cubic_roots(float a, float b, float c, float d, float *roots)
{
    int count = 0;

    const float A = b / a;
    const float B = c / a;
    const float C = d / a;

    const float Q = (3 * B - powf(A, 2)) / 9;
    const float R = (9 * A * B - 27 * C - 2 * powf(A, 3)) / 54;
    const float D = powf(Q, 3) + powf(R, 2); /* polynomial discriminant */

    if (D >= 0) /* complex or duplicate roots */
    {
        const float S = ne_cubic_roots_sign(R + sqrtf(D)) *
                        powf(fabsf(R + sqrtf(D)), (1.0f / 3.0f));
        const float T = ne_cubic_roots_sign(R - sqrtf(D)) *
                        powf(fabsf(R - sqrtf(D)), (1.0f / 3.0f));

        roots[0] = -A / 3 + (S + T);     /* real root */
        roots[1] = -A / 3 - (S + T) / 2; /* real part of complex root */
        roots[2] = -A / 3 - (S + T) / 2; /* real part of complex root */
        const float im = fabsf(sqrtf(3) * (S - T) / 2); /* complex part of root pair */

        /* discard complex roots */
        if (im != 0) count = 1;
        else count = 3;
    } else /* distinct real roots */
    {
        const float th = acosf(R / sqrtf(-powf(Q, 3)));

        roots[0] = 2 * sqrtf(-Q) * cosf(th / 3) - A / 3;
        roots[1] = 2 * sqrtf(-Q) * cosf((th + 2 * NE_PI) / 3) - A / 3;
        roots[2] = 2 * sqrtf(-Q) * cosf((th + 4 * NE_PI) / 3) - A / 3;

        count = 3;
    }

    return count;
}

static inline struct ne_cubic_bezier_intersect_result ne_cubic_bezier_line_intersect(
    ImVec2 p0,
    ImVec2 p1,
    ImVec2 p2,
    ImVec2 p3,
    ImVec2 a0,
    ImVec2 a1
)
{
    /* https://github.com/kaishiqi/Geometric-Bezier/blob/master/GeometricBezier/src/kaishiqi/geometric/intersection/Intersection.as */

    const ImVec2 c3 = ne_vec2_add(
        ne_vec2_sub(ne_vec2_neg(p0), ne_vec2_scale(p2, 3)),
        ne_vec2_add(ne_vec2_scale(p1, 3), p3)
    );
    const ImVec2 c2 = ne_vec2_add(
        ne_vec2_sub(ne_vec2_scale(p0, 3), ne_vec2_scale(p1, 6)),
        ne_vec2_scale(p2, 3)
    );
    const ImVec2 c1 = ne_vec2_add(ne_vec2_scale(p0, -3), ne_vec2_scale(p1, 3));
    const ImVec2 c0 = p0;

    /* Convert line to normal form: ax + by + c = 0 */
    const float a = a1.y - a0.y;
    const float b = a0.x - a1.x;
    const float c = a0.x * (a0.y - a1.y) + a0.y * (a1.x - a0.x);

    /* Find roots of rotated cubic */
    float roots[3];
    const int rootCount = ne_cubic_roots(
        a * c3.x + b * c3.y,
        a * c2.x + b * c2.y,
        a * c1.x + b * c1.y,
        a * c0.x + b * c0.y + c,
        roots
    );

    const ImVec2 min = ne_vec2_min(a0, a1);
    const ImVec2 max = ne_vec2_max(a0, a1);

    struct ne_cubic_bezier_intersect_result result;
    int points = 0;

    for (int i = 0; i < rootCount; ++i) {
        const float root = roots[i];

        if (0 <= root && root <= 1) {
            const ImVec2 p = ne_cubic_bezier(p0, p1, p2, p3, root);

            if (a0.x == a1.x) {
                if (min.y <= p.y && p.y <= max.y) result.Points[points++] = p;
            } else if (a0.y == a1.y) {
                if (min.x <= p.x && p.x <= max.x) result.Points[points++] = p;
            } else if (p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y) {
                result.Points[points++] = p;
            }
        }
    }

    result.Count = points;

    return result;
}

static inline struct ne_cubic_bezier_intersect_result ne_cubic_bezier_line_intersect_pts(
    struct ne_cubic_bezier_points curve,
    struct ne_line line
)
{
    return ne_cubic_bezier_line_intersect(curve.P0, curve.P1, curve.P2, curve.P3, line.A, line.B);
}

/* upstream: Tesselator::Commit */
static inline void ne_tesselator_commit(
    ne_cubic_bezier_subdivide_callback callback,
    void *user_pointer,
    ImVec2 p,
    ImVec2 t
)
{
    struct ne_cubic_bezier_subdivide_sample sample;
    sample.Point = p;
    sample.Tangent = t;
    callback(&sample, user_pointer);
}

/* upstream: Tesselator::Subdivide */
static inline void ne_tesselator_subdivide(
    ne_cubic_bezier_subdivide_callback callback,
    void *user_pointer,
    float tolerance,
    struct ne_cubic_bezier_points curve,
    int level
)
{
    const float dx = curve.P3.x - curve.P0.x;
    const float dy = curve.P3.y - curve.P0.y;
    float d2 = ((curve.P1.x - curve.P3.x) * dy - (curve.P1.y - curve.P3.y) * dx);
    float d3 = ((curve.P2.x - curve.P3.x) * dy - (curve.P2.y - curve.P3.y) * dx);
    d2 = (d2 >= 0) ? d2 : -d2;
    d3 = (d3 >= 0) ? d3 : -d3;
    if ((d2 + d3) * (d2 + d3) < tolerance * (dx * dx + dy * dy)) {
        ne_tesselator_commit(callback, user_pointer, curve.P3, ne_cubic_bezier_tangent_pts(curve, 1.0f));
    } else if (level < 10) {
        const ImVec2 p12 = ne_vec2_scale(ne_vec2_add(curve.P0, curve.P1), 0.5f);
        const ImVec2 p23 = ne_vec2_scale(ne_vec2_add(curve.P1, curve.P2), 0.5f);
        const ImVec2 p34 = ne_vec2_scale(ne_vec2_add(curve.P2, curve.P3), 0.5f);
        const ImVec2 p123 = ne_vec2_scale(ne_vec2_add(p12, p23), 0.5f);
        const ImVec2 p234 = ne_vec2_scale(ne_vec2_add(p23, p34), 0.5f);
        const ImVec2 p1234 = ne_vec2_scale(ne_vec2_add(p123, p234), 0.5f);

        const struct ne_cubic_bezier_points left = { curve.P0, p12, p123, p1234 };
        const struct ne_cubic_bezier_points right = { p1234, p234, p34, curve.P3 };
        ne_tesselator_subdivide(callback, user_pointer, tolerance, left, level + 1);
        ne_tesselator_subdivide(callback, user_pointer, tolerance, right, level + 1);
    }
}

static inline void ne_cubic_bezier_subdivide_pts(
    ne_cubic_bezier_subdivide_callback callback,
    void *user_pointer,
    struct ne_cubic_bezier_points curve,
    float tess_tol,
    int flags
);

static inline void ne_cubic_bezier_subdivide(
    ne_cubic_bezier_subdivide_callback callback,
    void *user_pointer,
    ImVec2 p0,
    ImVec2 p1,
    ImVec2 p2,
    ImVec2 p3,
    float tess_tol,
    int flags
)
{
    struct ne_cubic_bezier_points curve = { p0, p1, p2, p3 };
    ne_cubic_bezier_subdivide_pts(callback, user_pointer, curve, tess_tol, flags);
}

static inline void ne_cubic_bezier_subdivide_pts(
    ne_cubic_bezier_subdivide_callback callback,
    void *user_pointer,
    struct ne_cubic_bezier_points curve,
    float tess_tol,
    int flags
)
{
    if (tess_tol < 0) tess_tol = 1.118f; /* sqrtf(1.25f) */

    const float tolerance = tess_tol * tess_tol;

    if (!(flags & ne_cubic_bezier_subdivide_skip_first))
        ne_tesselator_commit(callback, user_pointer, curve.P0, ne_cubic_bezier_tangent_pts(curve, 0.0f));

    ne_tesselator_subdivide(callback, user_pointer, tolerance, curve, 0);
}

static inline void ne_cubic_bezier_fixed_step(
    ne_cubic_bezier_fixed_step_callback callback,
    void *user_pointer,
    ImVec2 p0,
    ImVec2 p1,
    ImVec2 p2,
    ImVec2 p3,
    float step,
    bool overshoot,
    float max_value_error,
    float max_t_error
)
{
    if (step <= 0.0f || !callback || max_value_error <= 0 || max_t_error <= 0)
        return;

    struct ne_cubic_bezier_fixed_step_sample sample;
    sample.T = 0.0f;
    sample.Length = 0.0f;
    sample.Point = p0;
    sample.BreakSearch = false;

    callback(&sample, user_pointer);
    if (sample.BreakSearch) return;

    const float total_length = ne_cubic_bezier_length(p0, p1, p2, p3);
    const int point_count = (int)(total_length / step) + (overshoot ? 2 : 1);
    const float t_min = 0.0f;
    const float t_max = step * point_count / total_length;
    const float t_0 = (t_min + t_max) * 0.5f;

    /* ponytail: upstream memoized split lengths in std::map<float,float>; we
       recompute ne_cubic_bezier_split + length each bisection step instead.
       Ceiling: O(bisections) split evaluations per point, instead of one map
       lookup. Reintroduce a small fixed (t,length) array if point_count grows
       large enough to matter. */
    for (int point_index = 1; point_index < point_count; ++point_index) {
        const float targetLength = point_index * step;

        float t_start = t_min;
        float t_end = t_max;
        float t = t_0;

        float t_best = t;
        float error_best = total_length;

        while (true) {
            const struct ne_cubic_bezier_split_result
                front = ne_cubic_bezier_split(p0, p1, p2, p3, t);
            const float split_length = ne_cubic_bezier_length_pts(front.Left);

            const float length = split_length;
            const float error = targetLength - length;

            if (error < error_best) {
                error_best = error;
                t_best = t;
            }

            if (fabsf(error) <= max_value_error || fabsf(t_start - t_end) <= max_t_error) {
                sample.T = t;
                sample.Length = length;
                sample.Point = ne_cubic_bezier(p0, p1, p2, p3, t);

                callback(&sample, user_pointer);
                if (sample.BreakSearch) return;

                break;
            } else if (error < 0.0f) t_end = t;
            else /* if (error > 0.0f) */ t_start = t;

            t = (t_start + t_end) * 0.5f;
        }
    }
}

static inline void ne_cubic_bezier_fixed_step_pts(
    ne_cubic_bezier_fixed_step_callback callback,
    void *user_pointer,
    struct ne_cubic_bezier_points curve,
    float step,
    bool overshoot,
    float max_value_error,
    float max_t_error
)
{
    ne_cubic_bezier_fixed_step(
        callback,
        user_pointer,
        curve.P0,
        curve.P1,
        curve.P2,
        curve.P3,
        step,
        overshoot,
        max_value_error,
        max_t_error
    );
}

#endif /* NE_MATH_H */
