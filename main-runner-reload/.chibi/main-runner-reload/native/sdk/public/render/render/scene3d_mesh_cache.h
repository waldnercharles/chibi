#ifndef CHIBI_RENDER_SCENE3D_MESH_CACHE_H
#define CHIBI_RENDER_SCENE3D_MESH_CACHE_H

#include "platform/cute.h"
#include "runtime/geometry/mesh_asset.h"

/* Imported meshes on the GPU for the lit 3D pass: each catalog mesh's
 * vertices and 32-bit indices in one CF_Mesh, kept while the catalog holds
 * that mesh. Main thread only. */
typedef struct render_scene3d_mesh_cache render_scene3d_mesh_cache;
render_scene3d_mesh_cache *render_scene3d_mesh_cache_new();
void render_scene3d_mesh_cache_free(render_scene3d_mesh_cache *cache);
/* Uploads the mesh's GPU copy on first use; false when it cannot be made. */
bool render_scene3d_mesh_cache_upload(render_scene3d_mesh_cache *cache, const render_mesh_asset *mesh);
/* Draws `count` indices from `first` of an uploaded mesh under the current
 * transform. The draw's own translation is the center of the mesh's bounds,
 * which Cute sorts translucent draws by. */
void render_scene3d_mesh_cache_draw(
    const render_scene3d_mesh_cache *cache,
    const render_mesh_asset *mesh,
    int first,
    int count
);
/* How many meshes have a GPU copy. */
int render_scene3d_mesh_cache_count(const render_scene3d_mesh_cache *cache);
/* Frees the copies of meshes the catalog no longer holds, such as ones a
 * reload replaced. Does nothing while the catalog is unchanged. */
void render_scene3d_mesh_cache_prune(render_scene3d_mesh_cache *cache);
/* Revision-owned live geometry uses a namespace separate from catalog IDs. */
void render_scene3d_mesh_cache_dynamic_begin(render_scene3d_mesh_cache *);
void render_scene3d_mesh_cache_dynamic_end(render_scene3d_mesh_cache *);
void render_scene3d_mesh_cache_dynamic_clear(render_scene3d_mesh_cache *);
bool render_scene3d_mesh_cache_dynamic_upload(
    render_scene3d_mesh_cache *,
    uint64_t identity,
    uint64_t revision,
    const render_mesh_asset *
);
void render_scene3d_mesh_cache_dynamic_draw(
    const render_scene3d_mesh_cache *,
    uint64_t identity,
    const render_mesh_asset *,
    int first,
    int count
);
int render_scene3d_mesh_cache_dynamic_count(const render_scene3d_mesh_cache *);
int render_scene3d_mesh_cache_dynamic_uploads(const render_scene3d_mesh_cache *);

#endif
