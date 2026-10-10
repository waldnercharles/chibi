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
/* The mesh's GPU copy, uploaded on first use; a zero id when it cannot be made. */
CF_Mesh render_scene3d_mesh_cache_get(render_scene3d_mesh_cache *cache, const render_mesh_asset *mesh);
/* How many meshes have a GPU copy. */
int render_scene3d_mesh_cache_count(const render_scene3d_mesh_cache *cache);
/* Frees the copies of meshes the catalog no longer holds, such as ones a
 * reload replaced. */
void render_scene3d_mesh_cache_prune(render_scene3d_mesh_cache *cache);

#endif
