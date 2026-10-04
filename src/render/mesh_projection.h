#ifndef CHIBI_RENDER_MESH_PROJECTION_H
#define CHIBI_RENDER_MESH_PROJECTION_H

#include "platform/cute.h"
#include "render/view_2d.h"

#define RENDER_MESH_NEAR 1.0f

/* R right, U toward the eye, D screen-down. Orbit angles are radians. */
static inline void render_mesh_camera_basis(render_view_2d view, CF_V3 basis[3])
{
    float cy = cosf(view.yaw), sy = sinf(view.yaw);
    float cp = cosf(view.pitch), sp = sinf(view.pitch);
    basis[0] = (CF_V3){ cy, 0, -sy };
    basis[1] = (CF_V3){ sy * sp, cp, cy * sp };
    basis[2] = (CF_V3){ sy * cp, -sp, cy * cp };
    if (view.roll == 0) {
        return;
    }
    float cr = cosf(view.roll), sr = sinf(view.roll);
    CF_V3 right = basis[0], down = basis[2];
    basis[0] = cf_add_v3(cf_mul_v3_f(right, cr), cf_mul_v3_f(down, sr));
    basis[2] = cf_add_v3(cf_mul_v3_f(right, -sr), cf_mul_v3_f(down, cr));
}

static inline bool render_mesh_camera_orbit(render_view_2d view)
{
    return view.yaw != 0 || view.pitch != 0 || view.roll != 0 ||
           view.target_elevation != 0;
}

static inline CF_V3 render_mesh_camera_target(render_view_2d view)
{
    return (CF_V3){ view.center_x, view.target_elevation, -view.center_y };
}

static inline CF_V3 render_mesh_world_to_camera(render_view_2d view, CF_V3 world)
{
    if (!render_mesh_camera_orbit(view)) {
        return world;
    }
    CF_V3 basis[3];
    render_mesh_camera_basis(view, basis);
    CF_V3 delta = cf_sub_v3(world, render_mesh_camera_target(view));
    return (CF_V3){ view.center_x + cf_dot_v3(delta, basis[0]),
                    cf_dot_v3(delta, basis[1]),
                    -view.center_y + cf_dot_v3(delta, basis[2]) };
}

static inline CF_V3 render_mesh_camera_to_world(render_view_2d view, CF_V3 aligned)
{
    if (!render_mesh_camera_orbit(view)) {
        return aligned;
    }
    CF_V3 basis[3];
    render_mesh_camera_basis(view, basis);
    CF_V3 result = render_mesh_camera_target(view);
    result = cf_add_v3(result, cf_mul_v3_f(basis[0], aligned.x - view.center_x));
    result = cf_add_v3(result, cf_mul_v3_f(basis[1], aligned.y));
    return cf_add_v3(result, cf_mul_v3_f(basis[2], aligned.z + view.center_y));
}

static inline CF_V3 render_mesh_camera_eye(render_view_2d view)
{
    return render_mesh_camera_to_world(
        view,
        (CF_V3){ view.center_x, view.focal_distance, -view.center_y }
    );
}

static inline void render_mesh_camera_bounds(render_view_2d view, CF_V3 min, CF_V3 max, CF_V3 *lo, CF_V3 *hi)
{
    if (!render_mesh_camera_orbit(view)) {
        *lo = min;
        *hi = max;
        return;
    }
    CF_V3 basis[3], target = render_mesh_camera_target(view);
    render_mesh_camera_basis(view, basis);
    *lo = (CF_V3){ INFINITY, INFINITY, INFINITY };
    *hi = (CF_V3){ -INFINITY, -INFINITY, -INFINITY };
    for (int corner = 0; corner < 8; corner++) {
        CF_V3 world = { corner & 1 ? max.x : min.x,
                        corner & 2 ? max.y : min.y,
                        corner & 4 ? max.z : min.z };
        CF_V3 delta = cf_sub_v3(world, target);
        CF_V3 p = { view.center_x + cf_dot_v3(delta, basis[0]),
                    cf_dot_v3(delta, basis[1]),
                    -view.center_y + cf_dot_v3(delta, basis[2]) };
        *lo = cf_min_v3(*lo, p);
        *hi = cf_max_v3(*hi, p);
    }
}

/* Camera-aligned world plane: X right, gameplay Z down, elevation Y toward
 * the eye. Bounds remain conservative when a box crosses the near plane. */
static inline bool render_mesh_project_bounds(
    render_view_2d view,
    CF_V3 min,
    CF_V3 max,
    CF_V2 *lo,
    CF_V2 *hi
)
{
    render_mesh_camera_bounds(view, min, max, &min, &max);
    *lo = (CF_V2){ min.x, min.z };
    *hi = (CF_V2){ max.x, max.z };
    if (view.focal_distance == 0) {
        return true;
    }
    float distance = view.focal_distance;
    if (distance - min.y < RENDER_MESH_NEAR) {
        return false;
    }
    /* Clip the volume before projecting. Falling back to the full viewport
     * here made even off-screen boxes crossing the near plane draw, and made
     * small visible boxes allocate full-screen material intermediates. The
     * clipped box contains every surviving surface point; perspective extrema
     * occur at its corners while all denominators remain positive. */
    max.y = fminf(max.y, distance - RENDER_MESH_NEAR);
    *lo = (CF_V2){ INFINITY, INFINITY };
    *hi = (CF_V2){ -INFINITY, -INFINITY };
    for (int corner = 0; corner < 8; corner++) {
        float x = corner & 1 ? max.x : min.x;
        float y = corner & 2 ? max.y : min.y;
        float z = corner & 4 ? max.z : min.z;
        float scale = distance / (distance - y);
        CF_V2 p = { view.center_x + (x - view.center_x) * scale,
                    -view.center_y + (z + view.center_y) * scale };
        lo->x = fminf(lo->x, p.x);
        lo->y = fminf(lo->y, p.y);
        hi->x = fmaxf(hi->x, p.x);
        hi->y = fmaxf(hi->y, p.y);
    }
    return true;
}

#endif
