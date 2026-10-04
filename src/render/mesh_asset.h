#ifndef CHIBI_RENDER_MESH_ASSET_H
#define CHIBI_RENDER_MESH_ASSET_H
#include "platform/cute.h"

#include <stddef.h>
#include <stdint.h>

#define RENDER_MESH_MIN_TRIANGLE_AREA 1e-12f

typedef struct
{
    CF_V3 position, normal;
    CF_V2 uv;
    CF_V3 part_position;
    float part_from_object[3][3];
} render_mesh_surface_vertex;

typedef struct
{
    int first, count, part;
    uint64_t node;
    char *asset, *name, *path, *material, *material_scope;
} render_mesh_group;
typedef struct
{
    render_mesh_surface_vertex *vertices;
    render_mesh_group *groups;
    bool has_part_coordinates, has_uv_bounds;
    CF_V3 uv_min, uv_max;
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
