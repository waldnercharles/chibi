#ifndef CHIBI_FLUID_RASTER_H
#define CHIBI_FLUID_RASTER_H

#include "render/material_raster.h"
#include "runtime/material_fluid.h"

typedef struct render_fluid_raster render_fluid_raster;
render_fluid_raster *render_fluid_raster_new();
void render_fluid_raster_free(render_fluid_raster *raster);
/* Converts particle positions from game Y-down to the shared Y-up view.
 * Adds dense fluid channels to material, the material raster's canvas, in the
 * tiles fluid covers. */
bool render_fluid_raster_draw(
    render_fluid_raster *raster,
    const material_fluid *fluid,
    CF_Canvas material,
    render_material_view view,
    int width,
    int height
);

#endif
