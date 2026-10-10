#include "render/scene3d_mesh_cache.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <stddef.h>
#include <stdlib.h>

typedef struct
{
    uint64_t identity;
    CF_Mesh mesh;
} uploaded;

struct render_scene3d_mesh_cache
{
    uploaded *meshes;
};

render_scene3d_mesh_cache *render_scene3d_mesh_cache_new()
{
    return xcalloc(1, sizeof(render_scene3d_mesh_cache));
}

void render_scene3d_mesh_cache_free(render_scene3d_mesh_cache *cache)
{
    for (int i = 0; i < acount(cache->meshes); i++) {
        cf_destroy_mesh(cache->meshes[i].mesh);
    }
    afree(cache->meshes);
    free(cache);
}

/* The attributes scene3d_mesh.vert reads. */
static const CF_VertexAttribute attributes[] = {
    { .name = "in_pos",
      .format = CF_VERTEX_FORMAT_FLOAT3,
      .offset = offsetof(render_mesh_surface_vertex, position) },
    { .name = "in_normal",
      .format = CF_VERTEX_FORMAT_FLOAT3,
      .offset = offsetof(render_mesh_surface_vertex, normal) },
    { .name = "in_uv",
      .format = CF_VERTEX_FORMAT_FLOAT2,
      .offset = offsetof(render_mesh_surface_vertex, uv) },
    { .name = "in_part_position",
      .format = CF_VERTEX_FORMAT_FLOAT3,
      .offset = offsetof(render_mesh_surface_vertex, part_position) },
    { .name = "in_part_frame0",
      .format = CF_VERTEX_FORMAT_FLOAT3,
      .offset = offsetof(render_mesh_surface_vertex, part_from_object[0]) },
    { .name = "in_part_frame1",
      .format = CF_VERTEX_FORMAT_FLOAT3,
      .offset = offsetof(render_mesh_surface_vertex, part_from_object[1]) },
    { .name = "in_part_frame2",
      .format = CF_VERTEX_FORMAT_FLOAT3,
      .offset = offsetof(render_mesh_surface_vertex, part_from_object[2]) },
};

CF_Mesh render_scene3d_mesh_cache_get(render_scene3d_mesh_cache *cache, const render_mesh_asset *mesh)
{
    for (int i = 0; i < acount(cache->meshes); i++) {
        if (cache->meshes[i].identity == mesh->identity) {
            return cache->meshes[i].mesh;
        }
    }
    int vertices = acount(mesh->vertices), indices = acount(mesh->indices);
    CF_Mesh result = cf_make_mesh(
        vertices * (int)sizeof *mesh->vertices,
        attributes,
        (int)(sizeof attributes / sizeof *attributes),
        (int)sizeof *mesh->vertices
    );
    if (!result.id) {
        return result;
    }
    cf_mesh_update_vertex_data(result, (void *)mesh->vertices, vertices);
    cf_mesh_set_index_buffer(result, indices * (int)sizeof *mesh->indices, 32);
    cf_mesh_update_index_data(result, (void *)mesh->indices, indices);
    apush(cache->meshes, ((uploaded){ mesh->identity, result }));
    return result;
}

int render_scene3d_mesh_cache_count(const render_scene3d_mesh_cache *cache)
{
    return acount(cache->meshes);
}

static bool resident(uint64_t identity)
{
    const render_mesh_table *table = render_mesh_table_current();
    for (int i = 0; i < render_mesh_table_source_count(table); i++) {
        if (render_mesh_table_asset_at(table, i)->identity == identity) {
            return true;
        }
    }
    return false;
}

void render_scene3d_mesh_cache_prune(render_scene3d_mesh_cache *cache)
{
    int kept = 0;
    for (int i = 0; i < acount(cache->meshes); i++) {
        if (resident(cache->meshes[i].identity)) {
            cache->meshes[kept++] = cache->meshes[i];
            continue;
        }
        cf_destroy_mesh(cache->meshes[i].mesh);
    }
    asetlen(cache->meshes, kept);
}
