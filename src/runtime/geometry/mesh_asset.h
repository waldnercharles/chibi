#ifndef CHIBI_RENDER_MESH_ASSET_H
#define CHIBI_RENDER_MESH_ASSET_H
#include <cute_math.h>
#include <cute_math3d.h>
#include <stddef.h>
#include <stdint.h>

#define RENDER_MESH_MIN_TRIANGLE_AREA 1e-12f

/* The tangent's w is its handedness: the bitangent is cross(normal, tangent) * w. */
typedef struct
{
    CF_V3 position, normal;
    CF_V2 uv;
    CF_V4 tangent;
    CF_V3 part_position;
    float part_from_object[3][3];
} render_mesh_surface_vertex;

/* A run of triangles, as `count` indices from `first`. */
typedef struct
{
    int first, count, part;
    uint64_t node;
    char *asset, *name, *path, *material, *material_scope;
} render_mesh_group;
/* Indexed triangles. Every three indices name one triangle's vertices. */
typedef struct
{
    render_mesh_surface_vertex *vertices;
    int *indices;
    render_mesh_group *groups;
    bool has_part_coordinates, has_uv_bounds;
    CF_V3 uv_min, uv_max;
    CF_V3 min, max;
    uint64_t identity;
} render_mesh_asset;

void render_mesh_asset_free(render_mesh_asset *asset);
/* Fills each vertex tangent from its triangles' UVs, perpendicular to its
 * normal, which must be unit length. A vertex whose UVs don't vary takes any
 * perpendicular tangent. */
void render_mesh_asset_tangents(render_mesh_asset *asset);
/* The middle of a mesh's bounds, from halves of each, so bounds near the
 * float limit cannot overflow it. */
CF_V3 render_mesh_asset_center(const render_mesh_asset *asset);
/* Immutable catalog-owned storage. Content-relative path, e.g. content/meshes/a.mesh.
 * Resolving a resident source does no file IO; workspace reset invalidates the catalog. */
const render_mesh_asset *render_mesh_asset_load(const char *source, char *error, size_t size);
/* Transfers an already validated owned snapshot into the resident catalog. */
void render_mesh_asset_publish(const char *source, render_mesh_asset *asset);
void render_mesh_assets_clear();
/* Changes whenever the resident catalog does: a publish, a clear or an exchange. */
uint64_t render_mesh_assets_generation();
/* Detached resident catalog generation. Successful insertion consumes asset;
 * rejection leaves it owned by the caller. Exchange cannot fail and transfers
 * the former resident generation into table for deferred retirement. */
typedef struct render_mesh_table render_mesh_table;
render_mesh_table *render_mesh_table_new();
render_mesh_table *render_mesh_table_new_budget(size_t *remaining);
bool render_mesh_table_add_owned_budget(
    render_mesh_table *,
    const char *,
    render_mesh_asset *,
    size_t *remaining
);
bool render_mesh_table_add_owned(render_mesh_table *, const char *source, render_mesh_asset *);
void render_mesh_table_exchange(render_mesh_table *);
void render_mesh_table_free(render_mesh_table *);
/* Current table and entry views are borrowed until the next exchange/mutation. */
const render_mesh_table *render_mesh_table_current();
int render_mesh_table_source_count(const render_mesh_table *);
const char *render_mesh_table_source_at(const render_mesh_table *, int);
const render_mesh_asset *render_mesh_table_asset_at(const render_mesh_table *, int);
#endif
