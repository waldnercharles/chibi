#include "render/appearance_geometry.h"

#include <assert.h>
#include <math.h>

static bool valid(const GameAppearance *appearance)
{
    assert(appearance);
    if (!isfinite(appearance->width) || !isfinite(appearance->height) ||
        appearance->width <= 0 || appearance->height <= 0) {
        return false;
    }
    return appearance->shape != GAME_SHAPE_STROKED_CHEVRON ||
           (isfinite(appearance->stroke_width) && appearance->stroke_width > 0);
}

int game_appearance_points(const GameAppearance *appearance, CF_V2 points[4])
{
    assert(points);
    if (!valid(appearance)) {
        return 0;
    }
    float x = appearance->width * 0.5f;
    float y = appearance->height * 0.5f;
    switch (appearance->shape) {
        case GAME_SHAPE_DIAMOND:
            points[0] = (CF_V2){ x, 0 };
            points[1] = (CF_V2){ 0, y };
            points[2] = (CF_V2){ -x, 0 };
            points[3] = (CF_V2){ 0, -y };
            return 4;
        case GAME_SHAPE_CHEVRON:
            points[0] = (CF_V2){ x, 0 };
            points[1] = (CF_V2){ -x, y };
            points[2] = (CF_V2){ -x * 0.6f, 0 };
            points[3] = (CF_V2){ -x, -y };
            return 4;
        case GAME_SHAPE_STROKED_CHEVRON:
            points[0] = (CF_V2){ appearance->width * 0.8f, 0 };
            points[1] = (CF_V2){ -appearance->width * 0.2f, -y };
            points[2] = (CF_V2){ 0, 0 };
            points[3] = (CF_V2){ -appearance->width * 0.2f, y };
            return 4;
        default: return 0;
    }
}

bool game_appearance_bounds(const GameAppearance *appearance, CF_V2 *min, CF_V2 *max)
{
    assert(min && max);
    if (!valid(appearance)) {
        return false;
    }
    float x = appearance->width * 0.5f;
    float y = appearance->height * 0.5f;
    if (appearance->shape == GAME_SHAPE_CIRCLE) {
        x = y = fminf(x, y);
    }
    CF_V2 lower = { -x, -y };
    CF_V2 upper = { x, y };
    if (appearance->shape == GAME_SHAPE_STROKED_CHEVRON) {
        float radius = appearance->stroke_width * 0.5f;
        lower = (CF_V2){ -appearance->width * 0.2f - radius, -y - radius };
        upper = (CF_V2){ appearance->width * 0.8f + radius, y + radius };
    }
    if (!isfinite(lower.x) || !isfinite(lower.y) || !isfinite(upper.x) ||
        !isfinite(upper.y)) {
        return false;
    }
    *min = lower;
    *max = upper;
    return true;
}

static double segment_distance_squared(CF_V2 point, CF_V2 a, CF_V2 b)
{
    double x = (double)b.x - a.x;
    double y = (double)b.y - a.y;
    double px = (double)point.x - a.x;
    double py = (double)point.y - a.y;
    double length = x * x + y * y;
    double t = length > 0 ? (px * x + py * y) / length : 0;
    t = fmax(0, fmin(1, t));
    px -= t * x;
    py -= t * y;
    return px * px + py * py;
}

bool game_appearance_contains(const GameAppearance *appearance, CF_V2 point)
{
    CF_V2 min;
    CF_V2 max;
    if (!isfinite(point.x) || !isfinite(point.y) ||
        !game_appearance_bounds(appearance, &min, &max) || point.x < min.x ||
        point.x > max.x || point.y < min.y || point.y > max.y) {
        return false;
    }
    double x = point.x / ((double)appearance->width * 0.5);
    double y = point.y / ((double)appearance->height * 0.5);
    switch (appearance->shape) {
        case GAME_SHAPE_CIRCLE: {
            double radius = fmin(appearance->width, appearance->height) * 0.5;
            return (double)point.x * point.x + (double)point.y * point.y <=
                   radius * radius;
        }
        case GAME_SHAPE_DIAMOND: return fabs(x) + fabs(y) <= 1;
        case GAME_SHAPE_CHEVRON:
            return x <= 1 - 2 * fabs(y) && x >= -0.6 - 0.4 * fabs(y);
        case GAME_SHAPE_STROKED_CHEVRON: {
            CF_V2 points[4];
            game_appearance_points(appearance, points);
            double radius = appearance->stroke_width * 0.5;
            for (int i = 0; i < 4; i++) {
                if (segment_distance_squared(point, points[i], points[(i + 1) % 4]) <=
                    radius * radius) {
                    return true;
                }
            }
            return false;
        }
        case GAME_SHAPE_CAPSULE: {
            double radius = fmin(appearance->width, appearance->height) * 0.5;
            double dx = fabs(point.x);
            double dy = fabs(point.y);
            if (appearance->width >= appearance->height) {
                dx = fmax(0, dx - ((double)appearance->width - appearance->height) * 0.5);
            } else {
                dy = fmax(0, dy - ((double)appearance->height - appearance->width) * 0.5);
            }
            return dx * dx + dy * dy <= radius * radius;
        }
        default: return true;
    }
}

bool game_appearance_rotate_point(CF_V2 point, float radians, CF_V2 *out)
{
    assert(out);
    if (!isfinite(radians) || !isfinite(point.x) || !isfinite(point.y)) {
        return false;
    }
    double c = cos((double)radians);
    double s = sin((double)radians);
    CF_V2 rotated = { (float)(point.x * c - point.y * s),
                      (float)(point.x * s + point.y * c) };
    if (!isfinite(rotated.x) || !isfinite(rotated.y)) {
        return false;
    }
    *out = rotated;
    return true;
}

bool game_appearance_rotated_bounds(const GameAppearance *appearance, float radians, CF_V2 *min, CF_V2 *max)
{
    assert(min && max);
    if (!isfinite(radians) || !valid(appearance)) {
        return false;
    }
    if (appearance->shape == GAME_SHAPE_CIRCLE) {
        return game_appearance_bounds(appearance, min, max);
    }
    CF_V2 points[4];
    int count = game_appearance_points(appearance, points);
    float radius = 0;
    if (appearance->shape == GAME_SHAPE_STROKED_CHEVRON) {
        radius = appearance->stroke_width * 0.5f;
    }
    if (appearance->shape == GAME_SHAPE_CAPSULE) {
        radius = fminf(appearance->width, appearance->height) * 0.5f;
        CF_V2 end = { appearance->width * 0.5f - radius,
                      appearance->height * 0.5f - radius };
        points[0] = end;
        points[1] = (CF_V2){ -end.x, -end.y };
        count = 2;
    }
    if (!count) {
        float x = appearance->width * 0.5f;
        float y = appearance->height * 0.5f;
        points[0] = (CF_V2){ -x, -y };
        points[1] = (CF_V2){ x, -y };
        points[2] = (CF_V2){ x, y };
        points[3] = (CF_V2){ -x, y };
        count = 4;
    }
    CF_V2 lower;
    if (!game_appearance_rotate_point(points[0], radians, &lower)) {
        return false;
    }
    CF_V2 upper = lower;
    for (int i = 1; i < count; i++) {
        CF_V2 point;
        if (!game_appearance_rotate_point(points[i], radians, &point)) {
            return false;
        }
        lower.x = fminf(lower.x, point.x);
        lower.y = fminf(lower.y, point.y);
        upper.x = fmaxf(upper.x, point.x);
        upper.y = fmaxf(upper.y, point.y);
    }
    lower.x -= radius;
    lower.y -= radius;
    upper.x += radius;
    upper.y += radius;
    if (!isfinite(lower.x) || !isfinite(lower.y) || !isfinite(upper.x) ||
        !isfinite(upper.y)) {
        return false;
    }
    *min = lower;
    *max = upper;
    return true;
}

bool game_appearance_contains_rotated(const GameAppearance *appearance, float radians, CF_V2 point)
{
    CF_V2 local;
    return game_appearance_rotate_point(point, -radians, &local) &&
           game_appearance_contains(appearance, local);
}
