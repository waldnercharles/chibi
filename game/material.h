#ifndef CBLUEPRINT_GAME_MATERIAL_H
#define CBLUEPRINT_GAME_MATERIAL_H

#include "components.h"

#include <stdbool.h>

typedef struct
{
    float scene[4];
    float emission[4];
    float transmittance[4];
    float albedo[4];
    float border_scene[4];
    float border_emission[4];
    float border_transmittance[4];
    float border_albedo[4];
} game_material_coefficients;

/* Unpremultiplied linear coefficients matching Neon-Swarm's entity raster.
 * Transmittance stores absorption, not 1-absorption. Packed colors are RGBA8;
 * emission RGB remains HDR. Invalid/overflowing scalars leave output untouched. */
bool game_material_extract(const GameMaterial *material, game_material_coefficients *out);

#endif
