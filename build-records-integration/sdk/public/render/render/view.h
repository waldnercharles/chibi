#ifndef CHIBI_RENDER_VIEW_H
#define CHIBI_RENDER_VIEW_H

#include <cute_math3d.h>
#include <stdbool.h>

/* The farthest far clip, as a multiple of the near clip. Float depth rounds
 * far planes much past 1e7 near clips toward infinity, so rays unprojected
 * through them collapse. */
#define ENGINE_VIEWPORT_CLIP_RATIO_MAX 1e6f
#define ENGINE_VIEWPORT_FOV_DEFAULT 0.6108652382f /* 35 degrees vertically. */

/* How the 3D path sees a camera. A nonzero forward looks along it from eye
 * with up toward the image top; a zero forward frames the center and span from
 * a fixed overhead angle. Field of view is vertical radians and the clips are
 * distances; zeros pick 35 degrees and clips that scale with the span. */
typedef struct engine_viewport_lens
{
    CF_V3 eye, forward, up;
    float fov, near_clip, far_clip;
    /* Explicit full-3D orthographic projection; spans define its extents. */
    bool orthographic;
} engine_viewport_lens;

typedef struct engine_viewport_camera
{
    float center_x;
    float center_y;
    float span_width;
    float span_height;
    float focal_distance;
    float yaw, pitch, target_elevation, roll;
    engine_viewport_lens lens;
} engine_viewport_camera;

/* Logical camera framing, independent of viewport aspect ratio. An explicit
 * lens's vertical field of view frames this height; narrower viewports expand
 * that angle to keep the authored width visible without moving its eye. */
typedef struct engine_viewport_view
{
    float center_x;
    float center_y;
    float width;
    float height;
    float focal_distance;
    float yaw, pitch, target_elevation, roll;
    engine_viewport_lens lens;
} engine_viewport_view;

#endif
