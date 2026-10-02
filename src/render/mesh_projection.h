#ifndef CHIBI_RENDER_MESH_PROJECTION_H
#define CHIBI_RENDER_MESH_PROJECTION_H

#include "platform/cute.h"
#include "render/view_2d.h"

#define RENDER_MESH_NEAR 1.0f

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
    *lo = (CF_V2){ min.x, min.z };
    *hi = (CF_V2){ max.x, max.z };
    if (view.focal_distance == 0) {
        return true;
    }
    float distance = view.focal_distance;
    if (distance - min.y < RENDER_MESH_NEAR) {
        return false;
    }
    if (distance - max.y <= RENDER_MESH_NEAR) {
        *lo = (CF_V2){ view.min_x, -view.max_y };
        *hi = (CF_V2){ view.min_x + view.width, view.height - view.max_y };
        return true;
    }
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
