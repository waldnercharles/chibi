#include "render/scene3d.h"

#include <assert.h>
#include <float.h>
#include <math.h>

static const float camera_fov = ENGINE_VIEWPORT_FOV_DEFAULT;
static const float camera_tilt = 0.1745329252f; /* 10 degrees from overhead. */

static CF_V3 camera_direction()
{
    return (CF_V3){ 0, cosf(camera_tilt), sinf(camera_tilt) };
}

render_scene3d_camera render_scene3d_camera_frame(const engine_viewport_camera *frame)
{
    assert(isfinite(frame->center_x) && isfinite(frame->center_y));
    assert(isfinite(frame->span_width) && isfinite(frame->span_height));
    assert(frame->span_width > 0 && frame->span_height > 0);
    engine_viewport_lens lens = frame->lens;
    float fov = lens.fov > 0 ? lens.fov : camera_fov;
    bool clipped = lens.near_clip > 0;
    assert(fov < CF_PI && clipped == (lens.far_clip > 0));
    assert(!clipped || lens.far_clip > lens.near_clip);
    float distance = lens.orthographic
                       ? frame->span_height
                       : frame->span_height * 0.5f / tanf(fov * 0.5f);
    if (cf_dot_v3(lens.forward, lens.forward) == 0) {
        CF_V3 target = { frame->center_x, 0, -frame->center_y };
        lens.eye = cf_add_v3(target, cf_mul_v3_f(camera_direction(), distance));
        lens.forward = cf_neg_v3(camera_direction());
        lens.up = (CF_V3){ 0, 1, 0 };
    }
    /* Default clips scale with the framing distance, floored at 10 so close
     * framings still see 200 ahead. Near stays in front of the framed target,
     * and far within the widest ratio a camera may author. */
    float reach = fmaxf(distance, 10);
    float closest = fminf(reach * 0.001f, distance * 0.5f);
    return (render_scene3d_camera){
        /* Turning at the origin keeps a short forward from vanishing into a
         * distant eye's rounding. */
        .view = cf_mul_m4(
            cf_look_at((CF_V3){ 0 }, lens.forward, lens.up),
            cf_m4_translate(cf_neg_v3(lens.eye))
        ),
        .projection = lens.orthographic
                        ? cf_ortho(
                              -frame->span_width * .5f,
                              frame->span_width * .5f,
                              -frame->span_height * .5f,
                              frame->span_height * .5f,
                              clipped ? lens.near_clip : closest,
                              clipped ? lens.far_clip : fminf(reach * 20, closest * ENGINE_VIEWPORT_CLIP_RATIO_MAX)
                          )
                        : cf_perspective(
                              fov,
                              frame->span_width / frame->span_height,
                              clipped ? lens.near_clip : closest,
                              clipped ? lens.far_clip : fminf(reach * 20, closest * ENGINE_VIEWPORT_CLIP_RATIO_MAX)
                          ),
        .eye = lens.eye,
        .orthographic = lens.orthographic,
    };
}

CF_V4 render_scene3d_viewer(const render_scene3d_camera *camera)
{
    if (!camera->orthographic) {
        return cf_v4_from_v3(camera->eye, 1);
    }
    const float *view = camera->view.elements;
    return (CF_V4){ view[2], view[6], view[10], 0 };
}

render_scene3d_camera render_scene3d_camera_make(float center_x, float center_y, float span_width, float span_height)
{
    return render_scene3d_camera_frame(&(engine_viewport_camera){
        .center_x = center_x,
        .center_y = center_y,
        .span_width = span_width,
        .span_height = span_height });
}

CF_Aabb render_scene3d_fit(const CF_V3 *points, int count, float aspect)
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

bool render_scene3d_screen_bounds(
    const render_scene3d_camera *camera,
    const CF_V3 corners[8],
    CF_Aabb *bounds
)
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

CF_V3 render_scene3d_project(const render_scene3d_camera *camera, CF_V3 world)
{
    return cf_m4_transform_point(cf_mul_m4(camera->projection, camera->view), world);
}

CF_M4x4 render_scene3d_eye_rays(const render_scene3d_camera *camera)
{
    CF_M4x4 turn = camera->view;
    turn.elements[12] = turn.elements[13] = turn.elements[14] = 0;
    return cf_m4_invert(cf_mul_m4(camera->projection, turn));
}

CF_Ray3 render_scene3d_ray(const render_scene3d_camera *camera, CF_V2 ndc)
{
    CF_M4x4 rays = render_scene3d_eye_rays(camera);
    CF_V3 near = cf_m4_transform_point(rays, (CF_V3){ ndc.x, ndc.y, 0 });
    CF_V3 far = cf_m4_transform_point(rays, (CF_V3){ ndc.x, ndc.y, 1 });
    CF_Ray3 ray = cf_make_ray3(near, far);
    ray.p = cf_add_v3(camera->eye, near);
    return ray;
}

bool render_scene3d_ground(const render_scene3d_camera *camera, CF_V2 ndc, CF_V2 *game)
{
    CF_Ray3 ray = render_scene3d_ray(camera, ndc);
    CF_Raycast3 hit = cf_ray3_to_plane3(ray, (CF_Plane3){ .n = { 0, 1, 0 }, .d = 0 });
    if (!hit.hit) {
        return false;
    }
    *game = (CF_V2){ ray.p.x + ray.d.x * hit.t, ray.p.z + ray.d.z * hit.t };
    return true;
}
