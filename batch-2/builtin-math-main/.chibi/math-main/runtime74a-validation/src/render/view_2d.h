#ifndef CHIBI_RENDER_VIEW_2D_H
#define CHIBI_RENDER_VIEW_2D_H

#include <math.h>
#include <stdbool.h>

/* A padded Y-up world rectangle shared by every 2D render pass. The crop is
 * the camera rectangle in normalized texture coordinates. */
typedef struct
{
    float min_x;
    float max_y;
    float width;
    float height;
    float crop_min_u;
    float crop_min_v;
    float crop_max_u;
    float crop_max_v;
    /* Mesh projection around the unpadded camera center. Zero is orthographic. */
    float focal_distance, center_x, center_y;
    /* Orbit angles in radians; target elevation is in world units. */
    float yaw, pitch, target_elevation, roll;
} render_view_2d;

typedef struct
{
    float min_x, max_y, width, height;
    int output_width, output_height;
} render_view_2d_camera;

typedef struct
{
    float margin;
    float work_scale;
    float min_work_scale;
    int max_raster_size;
} render_view_2d_settings;

typedef struct
{
    render_view_2d view;
    int raster_width;
    int raster_height;
    float margin_x;
    float margin_y;
    bool grid_aligned;
} render_view_2d_frame;

/* Rasters are whole multiples of this many pixels, so work grids at scales of
 * 1/2^n up to 1/8 have cells of whole pixels. */
#define RENDER_VIEW_2D_RASTER_STEP 8
/* The coarsest work grid, as a fraction of the raster: color at an eighth of
 * the lowest HRC resolution. */
#define RENDER_VIEW_2D_MIN_SCALE .01f

typedef struct
{
    float center, span, margin, scale, min_scale;
    int output_pixels, max_pixels;
    bool top_edge;
} render_view_2d_axis_input;

typedef struct
{
    int raster_pixels;
    float origin, margin;
    bool grid_aligned;
} render_view_2d_axis_frame;

static inline bool render_view_2d_axis(render_view_2d_axis_input input, render_view_2d_axis_frame *frame)
{
    if (!frame || !isfinite(input.center) || !isfinite(input.span) ||
        input.span <= 0 || input.output_pixels < 2 || !isfinite(input.margin) ||
        input.margin < 0 || !isfinite(input.scale) ||
        input.scale < RENDER_VIEW_2D_MIN_SCALE || input.scale > 1 ||
        !isfinite(input.min_scale) || input.min_scale < RENDER_VIEW_2D_MIN_SCALE ||
        input.min_scale > input.scale || input.max_pixels < input.output_pixels) {
        return false;
    }

    float pixels_per_unit = input.output_pixels / input.span;
    if (!isfinite(pixels_per_unit) || pixels_per_unit <= 0 ||
        !isfinite(input.center + input.span * .5f) ||
        !isfinite(input.center - input.span * .5f)) {
        return false;
    }

    /* Reserve one work cell at the lowest adaptive scale plus two pixels so
     * snapping never changes texture dimensions as the camera moves or scale
     * adapts. */
    int reserve = (int)ceilf(1.0f / input.min_scale) + 2;
    if (input.max_pixels - input.output_pixels < reserve) {
        frame->raster_pixels = input.output_pixels;
        frame->origin = input.center + (input.top_edge ? 1 : -1) * input.span * .5f;
        frame->margin = 0;
        frame->grid_aligned = false;
        return true;
    }

    float available_margin = (input.max_pixels - input.output_pixels - reserve) /
                             (2 * pixels_per_unit);
    float margin = fminf(input.margin, available_margin);
    double requested = (input.span + 2.0 * margin) * pixels_per_unit;
    if (!isfinite(requested) || requested > input.max_pixels) {
        return false;
    }
    int base_pixels = (int)ceil(requested - 1e-6);
    int step = RENDER_VIEW_2D_RASTER_STEP;
    int pixels = (base_pixels + reserve + step - 1) / step * step;
    int limit = input.max_pixels / step * step;
    if (limit - input.output_pixels < reserve) {
        limit = input.max_pixels;
    }
    if (pixels > limit) {
        pixels = limit;
        base_pixels = pixels - reserve;
        margin = fminf(margin, (base_pixels - input.output_pixels) / (2 * pixels_per_unit));
    }

    int work_pixels = (int)fmaxf(2, ceilf(pixels * input.scale));
    float span = pixels / pixels_per_unit;
    float cell = span / work_pixels;
    float target = input.center +
                   (input.top_edge ? 1 : -1) * (input.span * .5f + margin);
    if (!isfinite(target)) {
        return false;
    }
    float origin = input.top_edge ? ceilf(target / cell - 1e-6f) * cell
                                  : floorf(target / cell + 1e-6f) * cell;
    frame->raster_pixels = pixels;
    frame->origin = origin;
    frame->margin = margin;
    frame->grid_aligned = true;
    return true;
}

/* Keep work-grid sample positions fixed as the camera moves. The constant
 * reserve depends on min_work_scale, while origin snapping uses the live scale. */
static inline bool render_view_2d_world_margin(
    render_view_2d_camera camera,
    render_view_2d_settings settings,
    render_view_2d_frame *frame
)
{
    if (!frame || !isfinite(camera.min_x) || !isfinite(camera.max_y) ||
        !isfinite(camera.width) || !isfinite(camera.height) ||
        !isfinite(settings.margin) || settings.margin < 0 ||
        !isfinite(settings.work_scale) || !isfinite(settings.min_work_scale) ||
        settings.max_raster_size < 2) {
        return false;
    }

    float center_x = camera.min_x + camera.width * .5f;
    float center_y = camera.max_y - camera.height * .5f;
    render_view_2d_axis_frame x, y;
    if (!render_view_2d_axis(
            (render_view_2d_axis_input){
                .center = center_x,
                .span = camera.width,
                .margin = settings.margin,
                .scale = settings.work_scale,
                .min_scale = settings.min_work_scale,
                .output_pixels = camera.output_width,
                .max_pixels = settings.max_raster_size,
            },
            &x
        ) ||
        !render_view_2d_axis(
            (render_view_2d_axis_input){
                .center = center_y,
                .span = camera.height,
                .margin = settings.margin,
                .scale = settings.work_scale,
                .min_scale = settings.min_work_scale,
                .output_pixels = camera.output_height,
                .max_pixels = settings.max_raster_size,
                .top_edge = true,
            },
            &y
        )) {
        return false;
    }

    float width = x.grid_aligned
                    ? x.raster_pixels / (camera.output_width / camera.width)
                    : camera.width;
    float height = y.grid_aligned
                     ? y.raster_pixels / (camera.output_height / camera.height)
                     : camera.height;
    frame->raster_width = x.raster_pixels;
    frame->raster_height = y.raster_pixels;
    frame->margin_x = x.margin;
    frame->margin_y = y.margin;
    frame->grid_aligned = x.grid_aligned && y.grid_aligned;
    frame->view = (render_view_2d){
        .min_x = x.origin,
        .max_y = y.origin,
        .width = width,
        .height = height,
        .crop_min_u = (camera.min_x - x.origin) / width,
        .crop_min_v = (y.origin - camera.max_y) / height,
        .crop_max_u = (camera.min_x + camera.width - x.origin) / width,
        .crop_max_v = (y.origin - (camera.max_y - camera.height)) / height,
    };
    return true;
}

#endif
