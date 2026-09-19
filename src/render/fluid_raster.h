#ifndef CHIBI_FLUID_RASTER_H
#define CHIBI_FLUID_RASTER_H

#include "render/material_raster.h"
#include "runtime/material_fluid.h"

typedef struct render_fluid_raster render_fluid_raster;
render_fluid_raster *render_fluid_raster_new();
void render_fluid_raster_free(render_fluid_raster *raster);
/* Borrowed camera-only visibility tint, excluded from all HRC feedback. */
/* RGB is the camera-only floor; alpha is fluid coverage for display exposure. */
CF_Texture render_fluid_raster_visibility(const render_fluid_raster *raster);
/* Converts particle positions from game Y-down to the shared Y-up view.
 * Adds dense fluid channels to the existing material images before HRC. */
bool render_fluid_raster_draw(
    render_fluid_raster *raster,
    const material_fluid *fluid,
    render_material_textures textures,
    render_material_view view,
    int width,
    int height
);

#endif
