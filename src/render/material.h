#ifndef CHIBI_GAME_MATERIAL_H
#define CHIBI_GAME_MATERIAL_H

#include "runtime/components.h"
#include "runtime/material_mask.h"

#include <stdbool.h>

typedef struct
{
    float scene[4];
    float emission[4];
    float absorption[4];
    float albedo[4];
    float border_scene[4];
    float border_emission[4];
    float border_absorption[4];
    float border_albedo[4];
} game_material_coefficients;

/* Unpremultiplied linear coefficients matching Neon-Swarm's entity raster.
 * Absorption stores optical depth, not 1-absorption. Packed colors are RGBA8;
 * emission RGB remains HDR. Invalid/overflowing scalars leave output untouched. */
bool game_material_extract(const GameMaterial *material, game_material_coefficients *out);

typedef struct
{
    int x, y, width, height;
} render_material_border_rect;

/* Renderer-owned scratch. Rectangles exactly cover border cells without overlap;
 * the next build invalidates the borrowed output. Capacities survive builds. */
typedef struct
{
    render_material_border_rect *rects;
    int *row;
} render_material_border;

void render_material_border_build(render_material_border *border, material_occupancy_view mask, int reach);
void render_material_border_free(render_material_border *border);

#endif
