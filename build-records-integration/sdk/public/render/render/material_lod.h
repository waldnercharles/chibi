#ifndef CHIBI_RENDER_MATERIAL_LOD_H
#define CHIBI_RENDER_MATERIAL_LOD_H

#include "runtime/material_mask.h"

/* Levels of detail for material masks, as mipmaps are for textures: level k
 * merges blocks of 2^k by 2^k cells (material_mask_reduce). A mask draws a
 * piece per cell along its silhouette, so a distant one would draw many
 * pieces per pixel. Drawn at the finest level whose cells span at least
 * RENDER_MATERIAL_LOD_PIXELS, as a mipmap is picked so its texels cover the
 * screen's pixels, the pieces follow the screen instead of the world's area,
 * as terrain clipmaps and Unreal's HLODs do. Cells that span a pixel draw
 * exactly as authored. */
#define RENDER_MATERIAL_LOD_PIXELS 1.0f
/* A level is kept until its cells leave its range by this many octaves, so a
 * view that zooms around a boundary does not alternate between levels. */
#define RENDER_MATERIAL_LOD_HYSTERESIS 0.25f
enum
{
    RENDER_MATERIAL_LOD_LEVELS = 6
};

typedef struct render_material_lod render_material_lod;

/* The level for cells that span cell_pixels, keeping previous within the
 * hysteresis. */
int render_material_lod_pick(float cell_pixels, int previous);

/* Renderer-owned. It keeps the levels of the masks drawn in the last frame,
 * so its memory follows what the view shows. */
render_material_lod *render_material_lod_new();
void render_material_lod_free(render_material_lod *lod);
/* Starts a frame, releasing the levels of masks the last frame did not draw. */
void render_material_lod_begin(render_material_lod *lod);
/* The level to draw masks of cell_size at pixels_per_unit, picked with the
 * last level chosen for that cell size so they all change together. */
int render_material_lod_level(render_material_lod *lod, float cell_size, float pixels_per_unit);
/* The mask at level, or at the coarsest level below it that its dimensions
 * still halve into evenly and a terrain source's shared halo lod_limit.
 * Halo-aware reduction preserves neighbor samples. A changed revision, such as
 * terrain cut at runtime, rebuilds its levels. Level 0 is mask itself; the
 * others are borrowed until the next frame begins. */
const material_mask *render_material_lod_get(
    render_material_lod *lod,
    const material_mask *mask,
    int level
);

#endif
