#ifndef CHIBI_RENDER_SCENE3D_TERRAIN_VOLUME_H
#define CHIBI_RENDER_SCENE3D_TERRAIN_VOLUME_H

#include "runtime/materials.h"
#include "runtime/task_pool.h"

/* Samples per chunk side, across the plane and through depth; a cut
 * re-meshes only the chunks it can reshape. */
#define RENDER_TERRAIN_VOLUME_CHUNK 32

typedef struct render_scene3d_terrain_volume render_scene3d_terrain_volume;
typedef struct
{
    const render_mesh_asset *mesh;
    uint64_t identity, revision;
} render_scene3d_volume_chunk;
typedef struct
{
    const render_scene3d_volume_chunk *chunks;
    int chunk_count;
    /* Rock ahead of the play plane: it casts shadows, and draws only with
     * the volume's front material. */
    const render_scene3d_volume_chunk *casters;
    int caster_count;
    /* Fully solid bricks in the chunks' frame, coarsened until they fit
     * half the scene's indirect proxy budget; openings stay open. Half of
     * that is for the slab the carve can reach, fitted per tile of chunk
     * columns, with tiles of more columns when there are too many columns
     * for one box each; rock beyond it is fitted once into the rest. */
    const terrain_volume_box *boxes;
    int box_count;
} render_scene3d_volume_surface;

render_scene3d_terrain_volume *render_scene3d_terrain_volume_new();
void render_scene3d_terrain_volume_free(render_scene3d_terrain_volume *);
void render_scene3d_terrain_volume_reset(render_scene3d_terrain_volume *);
/* Volumes not requested between begin and end are released at end. */
void render_scene3d_terrain_volume_begin(render_scene3d_terrain_volume *);
void render_scene3d_terrain_volume_end(render_scene3d_terrain_volume *);
/* Chunks meshed since begin, including first builds. */
int render_scene3d_terrain_volume_rebuilds(const render_scene3d_terrain_volume *);
/* Borrowed until the next call for the same volume or end. Chunks no carve
 * change can reshape keep their mesh, identity and revision. Changed chunks
 * mesh on pool's workers, or serially without one. */
render_scene3d_volume_surface render_scene3d_terrain_volume_get(
    render_scene3d_terrain_volume *,
    materials_depth_volume,
    float elevation,
    CF_V2 origin,
    task_pool *pool
);

#endif
