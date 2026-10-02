#ifndef CHIBI_RENDER_MESH_ASSET_H
#define CHIBI_RENDER_MESH_ASSET_H
#include "platform/cute.h"

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    CF_V3 position, normal;
    CF_V2 uv;
} render_mesh_surface_vertex;

typedef struct
{
    render_mesh_surface_vertex *vertices;
    CF_V3 min, max;
    uint64_t identity;
} render_mesh_asset;

/* Version 1 JSON triangle list: {version:1,vertices:[[x,y,z,nx,ny,nz,u,v],...]}. */
bool render_mesh_asset_parse(const char *text, render_mesh_asset *asset, char *error, size_t size);
void render_mesh_asset_free(render_mesh_asset *asset);
/* Immutable catalog-owned storage. Content-relative path, e.g. content/meshes/a.mesh.json.
 * Resolving a resident source does no file IO; workspace reset invalidates the catalog. */
const render_mesh_asset *render_mesh_asset_load(const char *source, char *error, size_t size);
void render_mesh_assets_clear();
#endif
