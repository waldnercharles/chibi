#include "render/arena3d.h"

#include <assert.h>
#include <float.h>
#include <math.h>

static const float camera_fov = 0.6108652382f;  /* 35 degrees vertically. */
static const float camera_tilt = 0.1745329252f; /* 10 degrees from overhead. */

static CF_V3 camera_direction()
{
    return (CF_V3){ 0, cosf(camera_tilt), sinf(camera_tilt) };
}

static render_arena_camera camera_make(
    float center_x,
    float center_y,
    float span_width,
    float span_height,
    float pitch,
    float roll,
    CF_V3 up
)
{
    assert(isfinite(center_x) && isfinite(center_y));
    assert(isfinite(span_width) && isfinite(span_height));
    assert(span_width > 0 && span_height > 0);
    assert(isfinite(pitch) && isfinite(roll));
    float distance = span_height * 0.5f / tanf(camera_fov * 0.5f);
    CF_V3 target = { center_x, 0, -center_y };
    CF_V3 direction = { sinf(roll) * cosf(pitch), cosf(roll) * cosf(pitch), sinf(pitch) };
    CF_V3 eye = cf_add_v3(target, cf_mul_v3_f(direction, distance));
    return (render_arena_camera){ .view = cf_look_at(eye, target, up),
                                  .projection = cf_perspective(
                                      camera_fov,
                                      span_width / span_height,
                                      fmaxf(distance * 0.001f, 0.01f),
                                      distance * 20
                                  ) };
}

render_arena_camera render_arena_camera_make(float center_x, float center_y, float span_width, float span_height)
{
    return camera_make(center_x, center_y, span_width, span_height, camera_tilt, 0, (CF_V3){ 0, 1, 0 });
}

render_arena_camera render_arena_camera_make_showcase(
    float center_x,
    float center_y,
    float span_width,
    float span_height,
    float time
)
{
    assert(isfinite(time));
    float fade = fminf(time / 2.0f, 1.0f);
    fade = fade * fade * (3.0f - 2.0f * fade);
    float phase = time * (2.0f * 3.141592654f / 24.0f);
    float amount = 18.0f * (3.141592654f / 180.0f) * fade;
    float pitch = camera_tilt + amount * cosf(phase);
    float roll = amount * sinf(phase);
    CF_V3 up = { sinf(roll) * sinf(pitch), cosf(roll) * sinf(pitch), -cosf(pitch) };
    return camera_make(center_x, center_y, span_width, span_height, pitch, roll, up);
}

CF_Aabb render_arena_fit(const CF_V3 *points, int count, float aspect)
{
    assert(count > 0 && isfinite(aspect) && aspect > 0);
    CF_V3 direction = camera_direction();
    CF_V3 up = { 0, direction.z, -direction.y };
    CF_Aabb plane = { { FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX } };
    for (int i = 0; i < count; i++) {
        float y = cf_dot_v3(points[i], up);
        plane.min.x = fminf(plane.min.x, points[i].x);
        plane.max.x = fmaxf(plane.max.x, points[i].x);
        plane.min.y = fminf(plane.min.y, y);
        plane.max.y = fmaxf(plane.max.y, y);
    }
    CF_V3 target = { (plane.min.x + plane.max.x) * 0.5f,
                     0,
                     -(plane.min.y + plane.max.y) * 0.5f / direction.y };
    float tangent = tanf(camera_fov * 0.5f);
    float distance = 1;
    for (int i = 0; i < count; i++) {
        CF_V3 relative = cf_sub_v3(points[i], target);
        float extent = fmaxf(fabsf(relative.x) / aspect, fabsf(cf_dot_v3(relative, up)));
        distance = fmaxf(distance, cf_dot_v3(relative, direction) + extent * 1.2f / tangent);
    }
    float height = distance * tangent;
    float width = height * aspect;
    return (CF_Aabb){ { target.x - width, -target.z - height },
                      { target.x + width, -target.z + height } };
}

static void expand_projected(CF_Aabb *bounds, CF_V4 point)
{
    float x = point.x / point.w, y = point.y / point.w;
    bounds->min.x = fminf(bounds->min.x, x);
    bounds->min.y = fminf(bounds->min.y, y);
    bounds->max.x = fmaxf(bounds->max.x, x);
    bounds->max.y = fmaxf(bounds->max.y, y);
}

bool render_arena_screen_bounds(const render_arena_camera *camera, const CF_V3 corners[8], CF_Aabb *bounds)
{
    CF_M4x4 matrix = cf_mul_m4(camera->projection, camera->view);
    CF_V4 clip[8];
    *bounds = (CF_Aabb){ { FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX } };
    for (int i = 0; i < 8; i++) {
        clip[i] = cf_mul_m4_v4(matrix, cf_v4_from_v3(corners[i], 1));
    }
    for (int i = 0; i < 8; i++) {
        for (int bit = 1; bit <= 4; bit *= 2) {
            if (i & bit) {
                continue;
            }
            CF_V4 a = clip[i], b = clip[i | bit];
            float start = 0, end = 1;
            float a_planes[] = { a.z, a.w - a.z };
            float b_planes[] = { b.z, b.w - b.z };
            for (int plane = 0; plane < 2; plane++) {
                float av = a_planes[plane], bv = b_planes[plane];
                if (av < 0 && bv < 0) {
                    end = -1;
                    break;
                }
                if (av < 0) {
                    start = fmaxf(start, av / (av - bv));
                }
                if (bv < 0) {
                    end = fminf(end, av / (av - bv));
                }
            }
            if (start <= end) {
                expand_projected(bounds, cf_lerp_v4(a, b, start));
                expand_projected(bounds, cf_lerp_v4(a, b, end));
            }
        }
    }
    return bounds->min.x <= bounds->max.x;
}

CF_V3 render_arena_project(const render_arena_camera *camera, CF_V3 world)
{
    return cf_m4_transform_point(cf_mul_m4(camera->projection, camera->view), world);
}

CF_Ray3 render_arena_ray(const render_arena_camera *camera, CF_V2 ndc)
{
    CF_M4x4 inverse = cf_m4_invert(cf_mul_m4(camera->projection, camera->view));
    CF_V3 near = cf_m4_transform_point(inverse, (CF_V3){ ndc.x, ndc.y, 0 });
    CF_V3 far = cf_m4_transform_point(inverse, (CF_V3){ ndc.x, ndc.y, 1 });
    return cf_make_ray3(near, far);
}

bool render_arena_ground(const render_arena_camera *camera, CF_V2 ndc, CF_V2 *game)
{
    CF_Ray3 ray = render_arena_ray(camera, ndc);
    CF_Raycast3 hit = cf_ray3_to_plane3(ray, (CF_Plane3){ .n = { 0, 1, 0 }, .d = 0 });
    if (!hit.hit) {
        return false;
    }
    *game = (CF_V2){ ray.p.x + ray.d.x * hit.t, ray.p.z + ray.d.z * hit.t };
    return true;
}
