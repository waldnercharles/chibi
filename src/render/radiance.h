#ifndef CBLUEPRINT_RENDER_RADIANCE_H
#define CBLUEPRINT_RENDER_RADIANCE_H
#include "platform/cute.h"

#include <stdbool.h>
typedef struct render_radiance render_radiance;
typedef struct
{
    CF_Texture scene, emission, transmittance, albedo;
} render_radiance_inputs;
/* Requires a current Cute context and shader directory containing the hrc shader subdirectory. */
render_radiance *render_radiance_new();
void render_radiance_free(render_radiance *radiance);
/* Full-resolution materials/composite with a half-resolution radiance work grid,
 * capped at 640 pixels on its longer edge. Resize resets temporal history. */
bool render_radiance_resize(render_radiance *radiance, int width, int height);
void render_radiance_reset(render_radiance *radiance);
/* Inputs are borrowed RGBA16F textures at the configured size; emission and
 * transmittance allocate mipmaps. Uses current Cute camera, flushes composite to output. */
bool render_radiance_draw(render_radiance *radiance, render_radiance_inputs inputs, CF_Canvas output);
#endif
