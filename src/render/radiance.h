#ifndef CHIBI_RENDER_RADIANCE_H
#define CHIBI_RENDER_RADIANCE_H
#include "platform/cute.h"
#include "render/view_2d.h"
#include "runtime/components.h"

#include <stdbool.h>
typedef struct render_radiance render_radiance;
typedef struct
{
    float scale;
    int trace_levels;
    int max_light_size;
    bool feedback;
    bool available;
} render_radiance_settings;

typedef struct
{
    int canvas_width, canvas_height, work_width, work_height, light_width, light_height;
} render_radiance_dimensions;
render_radiance_dimensions render_radiance_get_dimensions(const render_radiance *radiance);

typedef struct
{
    CF_Texture scene, emission, absorption, albedo, normal, background,
        visibility, medium;
    CF_Texture optical_direct, optical_source, optical_surface;
    CF_Color medium_scattering;
    float medium_depth; /* Camera path length in canvas pixels. */
} render_radiance_inputs;
/* Requires a current Cute context and shader directory containing the hrc shader subdirectory. */
render_radiance *render_radiance_new();
void render_radiance_free(render_radiance *radiance);
/* Full-resolution materials/composite; fractional transport and optionally capped light history.
 * Resize resets temporal history. */
bool render_radiance_resize(render_radiance *radiance, int width, int height);
void render_radiance_reset(render_radiance *radiance);
render_radiance_settings render_radiance_get_settings(const render_radiance *radiance);
bool render_radiance_set_settings(render_radiance *radiance, render_radiance_settings settings);
void render_radiance_frame_time(render_radiance *radiance, double seconds);
bool render_radiance_set_lighting(render_radiance *radiance, GameHRC2D lighting);
/* Inputs are borrowed RGBA16F textures at the configured padded size. */
bool render_radiance_draw(
    render_radiance *radiance,
    render_radiance_inputs inputs,
    render_view_2d view,
    CF_Canvas output
);
#endif
