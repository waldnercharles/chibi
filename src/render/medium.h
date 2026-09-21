#ifndef CHIBI_RENDER_MEDIUM_H
#define CHIBI_RENDER_MEDIUM_H

#include "render/material_raster.h"
#include "runtime/medium.h"

typedef struct render_medium render_medium;
render_medium *render_medium_new();
void render_medium_free(render_medium *medium);
/* Adds world-unit extinction to transport and returns per-pixel R16F optical depth.
 * Density storage remains owned by the simulation, independent of the camera. */
CF_Texture render_medium_draw(
    render_medium *medium,
    const game_medium *field,
    render_material_textures textures,
    render_material_view view,
    int width,
    int height
);

#endif
