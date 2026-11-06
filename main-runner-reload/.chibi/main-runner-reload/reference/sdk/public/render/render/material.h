#ifndef CHIBI_MATERIAL_H
#define CHIBI_MATERIAL_H

#include "runtime/components.h"
#include "runtime/material_mask.h"

#include <stdbool.h>
#include <stddef.h>

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
} material_coefficients;

/* Unpremultiplied linear coefficients matching Neon-Swarm's entity raster.
 * Absorption stores optical depth, not 1-absorption. Packed colors are RGBA8;
 * emission RGB remains HDR. Invalid/overflowing scalars leave output untouched. */
bool material_extract(const Material *material, material_coefficients *out);

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

/* Known halo occupancy continues across chunk seams. Unavailable neighbors
 * preserve the isolated-mask outside-air rule; terrain snapshots cover reach. */
void render_material_border_build(render_material_border *border, material_occupancy_view mask, int reach);
void render_material_border_free(render_material_border *border);

typedef struct render_material_border_cache render_material_border_cache;
typedef struct
{
    const render_material_border_rect *rects;
    int count;
} render_material_border_view;
typedef struct
{
    int entries;
    size_t bytes;
    uint64_t hits, builds;
} render_material_border_cache_stats;

/* Renderer-local, single-owner cache of cell-space geometry only. Limits bound
 * retained entry/bucket arrays and rectangles, excluding the build scratch.
 * Zero limits or an unowned view (incarnation == 0) use the scratch path.
 * Returned geometry is borrowed until the next get/clear/free; no mask storage
 * is retained. Rebuild a mutated mask before requesting its geometry. */
render_material_border_cache *render_material_border_cache_new(int entries, size_t bytes);
/* Begin one collection. Entries used in this collection stay resident: a larger
 * working set uses build scratch instead of cycling cached allocations. */
void render_material_border_cache_begin(render_material_border_cache *cache);
render_material_border_view render_material_border_cache_get(
    render_material_border_cache *cache,
    material_occupancy_view mask,
    int reach
);
render_material_border_cache_stats render_material_border_cache_get_stats(
    const render_material_border_cache *cache
);
void render_material_border_cache_clear(render_material_border_cache *cache);
void render_material_border_cache_free(render_material_border_cache *cache);

#endif
