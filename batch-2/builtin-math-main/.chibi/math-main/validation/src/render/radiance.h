#ifndef CHIBI_RENDER_RADIANCE_H
#define CHIBI_RENDER_RADIANCE_H
#include "platform/cute.h"
#include "render/material_raster.h"
#include "render/spot_lights.h"
#include "render/view_2d.h"
#include "runtime/components.h"

#include <stdbool.h>
typedef struct render_radiance render_radiance;

/* Color detail relative to the light grid. */
typedef enum
{
    RENDER_RADIANCE_COLOR_FULL,
    RENDER_RADIANCE_COLOR_HALF,
    RENDER_RADIANCE_COLOR_QUARTER,
    RENDER_RADIANCE_COLOR_EIGHTH,
} render_radiance_color;

/* Resolution of the received-light image relative to the canvas. */
typedef enum
{
    RENDER_RADIANCE_GATHER_FULL,
    RENDER_RADIANCE_GATHER_HALF,
    RENDER_RADIANCE_GATHER_AUTO,
} render_radiance_gather;

typedef enum
{
    RENDER_RADIANCE_DEBUG_NORMAL,
    RENDER_RADIANCE_DEBUG_SCENE,
    RENDER_RADIANCE_DEBUG_EMISSION,
    RENDER_RADIANCE_DEBUG_ABSORPTION,
    RENDER_RADIANCE_DEBUG_ALBEDO,
    RENDER_RADIANCE_DEBUG_FLUENCE,
    RENDER_RADIANCE_DEBUG_NORMALS,
    RENDER_RADIANCE_DEBUG_SURFACE_LIGHT,
    RENDER_RADIANCE_DEBUG_LIGHT_DIRECTION,
    RENDER_RADIANCE_DEBUG_SHADING_NORMALS,
} render_radiance_debug_view;

/* HRC settings that HRC2D does not author: experiments and internal
 * tuning for tests, previews and the HRC lab. Games draw with
 * render_radiance_lab_defaults(). */
typedef struct
{
    /* Below Full, a brightness-only transport solves at the resolution and
     * color on a grid 2, 4 or 8 times coarser. */
    render_radiance_color color_resolution;
    /* Gather, temporal filtering and bounce at the full canvas or half of it;
     * Auto lets adaptive halve it before it coarsens color. */
    render_radiance_gather gather_resolution;
    /* Cascade levels traced through the scene, in [1, 5]. Higher levels are
     * extended from the ones below. */
    int trace_levels;
    /* Draws one of HRC's inputs or intermediate images instead of the lit
     * frame. */
    render_radiance_debug_view debug_view;
    /* Refine nearby Depth sources whose directions differ across a tile. */
    bool surface_depth_near_detail;
    /* Horizon reuse experiments; false keeps the reference submission. */
    bool horizon_fuse_relief, horizon_cache, horizon_reproject, horizon_hierarchy;
} render_radiance_lab;
render_radiance_lab render_radiance_lab_defaults();
typedef struct
{
    /* Color's grid as a fraction of the canvas. */
    float scale;
    /* Color's grid on adaptive's lowest step, which the view reserves room
     * for. Read-only. */
    float min_scale;
    int trace_levels;
    bool feedback;
    bool available;
} render_radiance_settings;

typedef struct
{
    render_material_textures material;
    /* Optional: the backdrop behind open space. */
    CF_Texture background;
    /* Borrowed for this draw; at most RENDER_SPOT_LIGHT_LIMIT direct lights. */
    const render_spot_light *spots;
    int spot_count;
} render_radiance_inputs;
/* Requires a current Cute context and shader directory containing the hrc shader subdirectory. */
render_radiance *render_radiance_new();
void render_radiance_free(render_radiance *radiance);
/* Full-resolution materials/composite with independently scaled transport and
 * diffuse reconstruction grids. Resize resets temporal history. */
bool render_radiance_resize(render_radiance *radiance, int width, int height);
void render_radiance_reset(render_radiance *radiance);
render_radiance_settings render_radiance_get_settings(const render_radiance *radiance);
bool render_radiance_set_settings(render_radiance *radiance, render_radiance_settings settings);
/* Lighting fitted to what HRC supports: each field clamped into its range.
 * Notes describes each change, or is empty.
 * render_radiance_set_lighting accepts every normalized value, and fails
 * without one or when its resources cannot be allocated. */
HRC2D render_radiance_normalize(HRC2D lighting, char *notes, size_t size);
bool render_radiance_set_lighting(render_radiance *radiance, HRC2D lighting, render_radiance_lab lab);
/* Feeds adaptive resolution one frame's scene GPU time, measured by its
 * owner; call it before laying out the next frame. */
void render_radiance_frame_time(render_radiance *radiance, double gpu_ms);
/* Feeds the whole frame's interval instead, when GPU time is unmeasured. */
void render_radiance_frame_interval(render_radiance *radiance, double ms);

typedef struct
{
    /* The finest grid and received-light image in use, and color's detail. */
    float resolution;
    render_radiance_color color;
    bool gather_half;
    /* The latest scene GPU time, or -1 without GPU timestamps, and the time
     * a frame at target_fps allows. */
    double gpu_ms, budget_ms;
    /* Adaptive's step: 0 is the authored lighting, rungs - 1 the cheapest. */
    bool adaptive;
    int rung, rungs;
} render_radiance_status;
render_radiance_status render_radiance_get_status(const render_radiance *radiance);
/* Deterministic submission bounds, not measured texture fetches or timings.
 * Gather skips closed probes and may end visibility tests early. Blur counts
 * the four-quadrant taps in its two axes, excluding seed/filter work.
 * Depth candidates count coarse tiles, excluding child and leaf refinement. */
typedef struct
{
    HRCSurfaceMode mode;
    int cells, block;
    HRCDepthVisibility visibility;
    size_t candidates, visibility_taps, blur_quadrant_fetches;
    /* Optional Depth history pass cells and allocated RGBA16F target bytes. */
    int temporal_cells;
    size_t temporal_bytes;
} render_surface_work;
render_surface_work render_radiance_surface_work(const render_radiance *radiance);

/* Work submitted by the latest draw. Bytes describes allocated horizon targets,
 * not driver overhead or bandwidth. Builds counts the directional bake pass. */
typedef struct
{
    int builds, pixels, directions;
    /* Submitted passes, not GPU cache-hit counts. */
    int pack_passes, fused_packs, dirty_passes, reuse_passes, reproject_passes;
    int hierarchy_passes, tiles_x, tiles_y;
    size_t canvas_bytes;
} render_horizon_work;
render_horizon_work render_radiance_horizon_work(const render_radiance *radiance);
/* Optional inspection: red is one for tiles whose horizons must be rebuilt.
 * Borrowed texture; reading it back is deliberately outside the frame path. */
CF_Texture render_radiance_horizon_dirty_texture(const render_radiance *radiance);
/* Inputs are borrowed RGBA16F textures at the configured padded size. */
bool render_radiance_draw(
    render_radiance *radiance,
    render_radiance_inputs inputs,
    render_view_2d view,
    CF_Canvas output
);
#endif
