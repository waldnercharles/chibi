#pragma once
#include "runtime/material_field.h"
#include "runtime/material_mask.h"

enum
{
    MATERIAL_TERRAIN_CHUNK_CELLS = 128
};
enum
{
    MATERIAL_CONNECTIVITY_ALL,
    MATERIAL_CONNECTIVITY_FIRST_AIR
};
typedef struct
{
    material_mask mask;
    float x, y;
} material_terrain_chunk;

/* Connectivity is discrete occupancy cleanup and does not change field semantics. */
void material_terrain_connect(float *field, field_grid grid, int connectivity, int border_cells);
/* Removes solid islands and fills air pockets of fewer than island_cells cells
 * that do not touch the grid edge. Such a component is enclosed within the
 * grid, so a streamed tile never removes rock connected across its edges. */
void material_terrain_tidy(float *field, field_grid grid, int island_cells);
material_terrain_chunk *material_terrain_chunks(const float *field, field_grid grid);
void material_terrain_chunks_free(material_terrain_chunk *chunks);

/* Source-scoped borrowed live masks, including known empty tiles. Caller keeps
 * masks alive for the call and invalidates its neighborhood cache on edits,
 * generation, residency or radius/LOD changes. Derived halos are not saved. */
typedef struct
{
    material_mask *mask;
    float x, y;
} material_terrain_view;
/* Captures owned neighboring samples. Call only when neighborhood inputs change;
 * one bounded scratch allocation is reused across views. Unknown neighbors use
 * MATERIAL_HALO_UNAVAILABLE; finite boundaries retain standalone silhouettes. */
void material_terrain_sync_views(const material_terrain_view *, int count, int radius, int lod_limit);
