#ifndef CHIBI_TERRAIN_VOLUME_H
#define CHIBI_TERRAIN_VOLUME_H

#include "runtime/fields/program.h"
#include "runtime/geometry/mesh_asset.h"

#include <stdbool.h>
#include <stddef.h>

#define TERRAIN_VOLUME_SAMPLE_LIMIT (1 << 24)
#define TERRAIN_VOLUME_REFINE_MAX 8

/* Derived rock density around a terrain's play plane. Samples sit on a lattice
 * over the plane, x across and y down the gameplay plane, at depth
 * z = (k - front) * plane.cell_size behind it; negative is solid. Every
 * refine-th sample along each axis is stored densely as the coarse lattice.
 * Coarse cells near a surface also store their fine samples; elsewhere the
 * density blends the cell's corners, which never changes its sign there.
 * Layer front keeps the signs of the field's planar bake, so it agrees with
 * the terrain mask before any edit. Layers before it are rock cut away in
 * front of the plane: it shades and blocks light but is never drawn. Unless
 * open_back, the deepest layer is solid, closing the volume. Cuts carve no
 * farther than slab from the plane, so rock beyond terrain_volume_reach
 * layers of it never changes. Never saved state. */
typedef struct
{
    field_grid plane;
    int front, layers, refine;
    bool open_back;
    float slab;
    float *coarse; /* coarse layers of coarse width * height samples */
    int *bricks;   /* per coarse cell, its index in detail or -1; refine > 1 */
    float *detail; /* refine^3 samples per baked coarse cell, from its low corner */
    int detail_count;
} terrain_volume;

/* What to bake: the coarse lattice plane, front of its layers ahead of the
 * plane, refine fine samples per coarse cell near surfaces, whether air in
 * the deepest layer stays open, and how far cuts carve from the plane. */
typedef struct
{
    field_grid plane;
    int front, layers, refine;
    bool open_back;
    float slab;
} terrain_volume_shape;

/* An axis-aligned solid in the mesh frame of terrain_volume_mesh. */
typedef struct
{
    CF_V3 min, max;
} terrain_volume_box;

/* Coarse cells [x0, x1) x [y0, y1) x [z0, z1), z counted from the frontmost
 * layer. */
typedef struct
{
    int x0, y0, z0, x1, y1, z1;
} terrain_volume_bricks;

/* Bakes a 3D field through shape. Output starts zeroed; failure leaves it
 * unchanged. At most TERRAIN_VOLUME_SAMPLE_LIMIT samples stored, at least
 * two coarse layers behind the plane and a positive slab. */
bool terrain_volume_bake(
    const field_program *program,
    field_context context,
    terrain_volume_shape shape,
    const task_system *tasks,
    terrain_volume *out,
    char *error,
    size_t size
);
void terrain_volume_free(terrain_volume *volume);

/* Signed distance into the plane region where the plane layer is solid but
 * the live mask is open (open holds one nonzero byte per open sample):
 * positive inside, negative outside, held between a few samples below zero
 * and slab. The volume carves max(density, removed - |z|), a double cone
 * capped at slab whose section on the plane equals the removed region.
 * [x0, x1) x [y0, y1) holds every plane sample whose open byte changed since
 * removed was last written; this rewrites that window widened by the carve's
 * reach, clipped to the plane, each sample exactly as a whole plane pass
 * would, and nothing else. */
void terrain_volume_removed(
    const terrain_volume *volume,
    const unsigned char *open,
    int x0,
    int y0,
    int x1,
    int y1,
    float *removed
);

/* The carved density at fine sample (i, j, k); negative is solid. */
float terrain_volume_density(const terrain_volume *volume, const float *removed, int i, int j, int k);

/* Layers either side of the plane, front included, that a change of removed
 * values can reshape: the carve's slab plus the margin its surfaces read. */
int terrain_volume_reach(const terrain_volume *volume);

/* Layers [*k0, *k1) cover every surface over plane samples [x0, x1) x
 * [y0, y1) that differs between removed values before and after. False when
 * none can. */
bool terrain_volume_changed_layers(
    const terrain_volume *volume,
    const float *before,
    const float *after,
    int x0,
    int y0,
    int x1,
    int y1,
    int *k0,
    int *k1
);

/* Coarse lattice points [x0, x1) x [y0, y1) x [z0, z1), coarse indices from
 * the frontmost layer, hold every point whose carved density can change sign
 * between removed values before and after. False when none can. */
bool terrain_volume_changed_lattice(
    const terrain_volume *volume,
    const float *before,
    const float *after,
    terrain_volume_bricks *points
);

/* Dual contouring over plane samples [x0, x1) x [y0, y1) and layers
 * [z0, z1), either all behind the play plane or, for shadow casting only, all
 * in front of it. Mesh position is (x, elevation - z, y) in the terrain frame;
 * part coordinates add origin so materials stay attached to the world.
 * Surfaces stop at the play plane, where the mask's own cap is the section.
 * Neighboring ranges share their seam vertices exactly. Output starts
 * zeroed. */
void terrain_volume_mesh(
    const terrain_volume *volume,
    const float *removed,
    int x0,
    int y0,
    int x1,
    int y1,
    int z0,
    int z1,
    float elevation,
    CF_V2 origin,
    render_mesh_asset *out
);

/* Appends at most limit boxes covering solid rock of bricks and no air:
 * groups of span coarse cells per side, narrower at the range's far faces,
 * whose cells are all solid, merged across the plane and then through depth.
 * The span doubles from one until at most limit boxes remain; when the first
 * span that fits leaves none, the largest limit boxes of the span before it
 * remain instead. Returns the box count. */
int terrain_volume_boxes(
    const terrain_volume *volume,
    const float *removed,
    terrain_volume_bricks bricks,
    int limit,
    float elevation,
    terrain_volume_box **boxes
);

#endif
