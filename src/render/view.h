#ifndef CHIBI_RENDER_VIEW_H
#define CHIBI_RENDER_VIEW_H

typedef struct engine_viewport_camera
{
    float center_x;
    float center_y;
    float span_width;
    float span_height;
    float focal_distance;
    float yaw, pitch, target_elevation, roll;
} engine_viewport_camera;

/* Logical camera framing, independent of viewport aspect ratio. */
typedef struct engine_viewport_view
{
    float center_x;
    float center_y;
    float width;
    float height;
    float focal_distance;
    float yaw, pitch, target_elevation, roll;
} engine_viewport_view;

#endif
