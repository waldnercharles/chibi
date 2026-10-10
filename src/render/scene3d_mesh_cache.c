#include "render/scene3d_mesh_cache.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
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

/* The point the GPU copy's positions are measured from: the center of the
 * mesh's bounds. Cute sorts translucent draws back to front by the
 * translation of their transform, so a draw translated here sorts by where
 * its triangles are rather than by its origin. */
static CF_V3 center(const render_mesh_asset *mesh)
{
    return cf_mul_v3_f(cf_add_v3(mesh->min, mesh->max), .5f);
}

static CF_Mesh find(const render_scene3d_mesh_cache *cache, uint64_t identity)
{
    for (int i = 0; i < acount(cache->meshes); i++) {
        if (cache->meshes[i].identity == identity) {
            return cache->meshes[i].mesh;
        }
    }
    return (CF_Mesh){ 0 };
}

static CF_Mesh upload(const render_mesh_asset *mesh)
{
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
    render_mesh_surface_vertex *centered = xmalloc((size_t)vertices * sizeof *centered);
    CF_V3 offset = center(mesh);
    for (int i = 0; i < vertices; i++) {
        centered[i] = mesh->vertices[i];
        centered[i].position = cf_sub_v3(centered[i].position, offset);
    }
    cf_mesh_update_vertex_data(result, centered, vertices);
    free(centered);
    cf_mesh_set_index_buffer(result, indices * (int)sizeof *mesh->indices, 32);
    cf_mesh_update_index_data(result, (void *)mesh->indices, indices);
    return result;
}

bool render_scene3d_mesh_cache_upload(render_scene3d_mesh_cache *cache, const render_mesh_asset *mesh)
{
    if (find(cache, mesh->identity).id) {
        return true;
    }
    CF_Mesh gpu = upload(mesh);
    if (!gpu.id) {
        return false;
    }
    apush(cache->meshes, ((uploaded){ mesh->identity, gpu }));
    return true;
}

void render_scene3d_mesh_cache_draw(
    const render_scene3d_mesh_cache *cache,
    const render_mesh_asset *mesh,
    int first,
    int count
)
{
    CF_Mesh gpu = find(cache, mesh->identity);
    assert(gpu.id);
    cf_draw3d_push();
    cf_draw3d_translate(center(mesh));
    cf_draw3d_mesh_range(gpu, first, count);
    cf_draw3d_pop();
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
