#ifndef CHIBI_RENDER_TRAIL_2D_H
#define CHIBI_RENDER_TRAIL_2D_H

#include "render/arena_trail.h"
#include "render/material_raster.h"

/* Capsules per trail: enough for a smooth taper and fade, few enough that
 * hundreds of trails stay cheap to raster. */
#define RENDER_TRAIL_2D_PIECES 8

/* Appends each trail in segments, the GameTrail history, as a tapered,
 * fading ribbon of emissive capsules in the Y-up 2D view. A trail is a run
 * of segments that join end to start. aa is the capsules' antialiasing in
 * world units. */
void render_trail_2d_append(
    const arena_trail_segment *segments,
    int count,
    float aa,
    float time,
    render_material_entity **entities
);

#endif
