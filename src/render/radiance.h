#ifndef CHIBI_RENDER_RADIANCE_H
#define CHIBI_RENDER_RADIANCE_H
#include "platform/cute.h"
#include "render/material_raster.h"
#include "render/view_2d.h"
#include "runtime/components.h"

#include <stdbool.h>
typedef struct render_radiance render_radiance;
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
 * render_radiance_set_lighting accepts every normalized value. */
GameHRC2D render_radiance_normalize(GameHRC2D lighting, char *notes, size_t size);
bool render_radiance_set_lighting(render_radiance *radiance, GameHRC2D lighting);
/* Feeds adaptive resolution one frame's scene GPU time, measured by its
 * owner; call it before laying out the next frame. */
void render_radiance_frame_time(render_radiance *radiance, double gpu_ms);
/* Feeds the whole frame's interval instead, when GPU time is unmeasured. */
void render_radiance_frame_interval(render_radiance *radiance, double ms);

typedef struct
{
    /* The finest grid and received-light image in use, and color's detail. */
    float resolution;
    GameHRCColor color;
    bool gather_half;
    /* The latest scene GPU time, or -1 without GPU timestamps, and the time
     * a frame at target_fps allows. */
    double gpu_ms, budget_ms;
    /* Adaptive's step: 0 is the authored lighting, rungs - 1 the cheapest. */
    bool adaptive;
    int rung, rungs;
} render_radiance_status;
render_radiance_status render_radiance_get_status(const render_radiance *radiance);
/* Inputs are borrowed RGBA16F textures at the configured padded size. */
bool render_radiance_draw(
    render_radiance *radiance,
    render_radiance_inputs inputs,
    render_view_2d view,
    CF_Canvas output
);
#endif
