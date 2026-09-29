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
/* Optics for the next draws: simulation time animates caustic ripples, a
 * zero caustics strength disables them, and heat writes the heat haze weight
 * to the cover texture. */
void render_fluid_raster_set_optics(render_fluid_raster *raster, float time, float caustics, bool heat);
/* What the last draw saw: burning or emissive fluid, which shimmers, and
 * liquid, which reflects. */
typedef struct
{
    bool hot, liquid;
} render_fluid_seen;
render_fluid_seen render_fluid_raster_seen(const render_fluid_raster *raster);

#endif
