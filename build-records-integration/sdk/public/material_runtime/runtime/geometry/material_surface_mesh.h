#ifndef CHIBI_MATERIAL_SURFACE_MESH_H
#define CHIBI_MATERIAL_SURFACE_MESH_H

#include "runtime/components.h"
#include "runtime/geometry/mesh_asset.h"
#include "runtime/material_mask.h"

/* Immutable during one build. Samples signed world-unit displacement from
 * options.elevation at retained MaterialFrame coordinates. Values must be finite
 * and strictly between -depth and depth. False rejects the entire snapshot.
 * The builder retains neither the descriptor nor context. No relief/noise gain
 * is applied to sampled values; the contour bevel/taper still applies. */
typedef struct
{
    bool (*sample)(void *context, CF_V2 material_point, float *height);
    void *context;
} material_surface_height;

bool material_surface_mesh_build_height(
    material_occupancy_view occupancy,
    MaterialSurface3D options,
    MaterialFrame frame,
    const material_surface_height *height,
    render_mesh_asset *output,
    char *error,
    size_t size
);

/* Derives an owned snapshot from published occupancy. Output starts zeroed;
 * free with render_mesh_asset_free. Positions remain mask-local X/gameplay Z,
 * elevation along Y. Frame selects stable relief/UV coordinates, not placement.
 * No input storage is retained. Failure leaves output unchanged. Identity is
 * assigned by the renderer, which owns cache invalidation and retirement.
 * Admission: at most 1,048,576 input cells and 3,000,000 output corners.
 * Spacing must be at least cell_size; contour support stays at source density.
 * It is a shallow open-backed surface, not a separate solid-volume model.
 * A recess also covers empty cells within recess_width of solid occupancy; it
 * needs masks of at most 256 cells per side and, when the mask has neighbors,
 * a halo of at least ceil((recess_width + recess_smoothing) / cell_size) + 2
 * cells. */
bool material_surface_mesh_build(
    material_occupancy_view occupancy,
    MaterialSurface3D options,
    MaterialFrame frame,
    render_mesh_asset *output,
    char *error,
    size_t size
);

#endif
