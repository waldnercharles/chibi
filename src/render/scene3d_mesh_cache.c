#include "render/scene3d_mesh_cache.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint64_t identity;
    CF_Mesh mesh;
} uploaded;
typedef struct
{
    uint64_t identity, revision;
    CF_Mesh mesh;
    bool used;
} dynamic_mesh;

struct render_scene3d_mesh_cache
{
    uploaded *meshes; /* Ordered by identity, so draws find theirs by bisection. */
    uint64_t pruned; /* The catalog generation the copies were last pruned at. */
    dynamic_mesh *dynamic;
    int dynamic_uploads;
};

render_scene3d_mesh_cache *render_scene3d_mesh_cache_new()
{
    return xcalloc(1, sizeof(render_scene3d_mesh_cache));
}

void render_scene3d_mesh_cache_free(render_scene3d_mesh_cache *cache)
{
    render_scene3d_mesh_cache_dynamic_clear(cache);
    afree(cache->dynamic);
    for (int i = 0; i < acount(cache->meshes); i++) {
        cf_destroy_mesh(cache->meshes[i].mesh);
    }
    afree(cache->meshes);
    free(cache);
}

void render_scene3d_mesh_cache_dynamic_clear(render_scene3d_mesh_cache *cache)
{
    for (int i = 0; i < acount(cache->dynamic); i++) {
        cf_destroy_mesh(cache->dynamic[i].mesh);
    }
    aclear(cache->dynamic);
    cache->dynamic_uploads = 0;
}

void render_scene3d_mesh_cache_dynamic_begin(render_scene3d_mesh_cache *cache)
{
    cache->dynamic_uploads = 0;
    for (int i = 0; i < acount(cache->dynamic); i++) {
        cache->dynamic[i].used = false;
    }
}

void render_scene3d_mesh_cache_dynamic_end(render_scene3d_mesh_cache *cache)
{
    for (int i = acount(cache->dynamic) - 1; i >= 0; i--) {
        if (cache->dynamic[i].used) {
            continue;
        }
        cf_destroy_mesh(cache->dynamic[i].mesh);
        adel(cache->dynamic, i);
    }
}

int render_scene3d_mesh_cache_dynamic_count(const render_scene3d_mesh_cache *cache)
{
    return acount(cache->dynamic);
}

int render_scene3d_mesh_cache_dynamic_uploads(const render_scene3d_mesh_cache *cache)
{
    return cache->dynamic_uploads;
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
    { .name = "in_tangent",
      .format = CF_VERTEX_FORMAT_FLOAT4,
      .offset = offsetof(render_mesh_surface_vertex, tangent) },
};

/* Where `identity`'s copy is, or would go to keep the order. */
static int slot(const render_scene3d_mesh_cache *cache, uint64_t identity)
{
    int low = 0, high = acount(cache->meshes);
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (cache->meshes[middle].identity < identity) {
            low = middle + 1;
            continue;
        }
        high = middle;
    }
    return low;
}

static CF_Mesh find(const render_scene3d_mesh_cache *cache, uint64_t identity)
{
    int i = slot(cache, identity);
    bool held = i < acount(cache->meshes) && cache->meshes[i].identity == identity;
    return held ? cache->meshes[i].mesh : (CF_Mesh){ 0 };
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
    /* Positions are measured from the center of the mesh's bounds. Cute
     * sorts translucent draws back to front by the translation of their
     * transform, so a draw translated there sorts by where its triangles are
     * rather than by its origin. */
    CF_V3 offset = render_mesh_asset_center(mesh);
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

bool render_scene3d_mesh_cache_dynamic_upload(
    render_scene3d_mesh_cache *cache,
    uint64_t identity,
    uint64_t revision,
    const render_mesh_asset *mesh
)
{
    int index = 0;
    for (; index < acount(cache->dynamic); index++) {
        dynamic_mesh *held = &cache->dynamic[index];
        if (held->identity != identity) {
            continue;
        }
        held->used = true;
        if (held->revision == revision) {
            return true;
        }
        break;
    }
    CF_Mesh gpu = upload(mesh);
    if (!gpu.id) {
        return false;
    }
    dynamic_mesh fresh = { identity, revision, gpu, true };
    if (index < acount(cache->dynamic)) {
        cf_destroy_mesh(cache->dynamic[index].mesh);
        cache->dynamic[index] = fresh;
    } else {
        apush(cache->dynamic, fresh);
    }
    cache->dynamic_uploads++;
    return true;
}

void render_scene3d_mesh_cache_dynamic_draw(
    const render_scene3d_mesh_cache *cache,
    uint64_t identity,
    const render_mesh_asset *mesh,
    int first,
    int count
)
{
    for (int i = 0; i < acount(cache->dynamic); i++) {
        if (cache->dynamic[i].identity != identity) {
            continue;
        }
        cf_draw3d_push();
        cf_draw3d_translate(render_mesh_asset_center(mesh));
        cf_draw3d_mesh_range(cache->dynamic[i].mesh, first, count);
        cf_draw3d_pop();
        return;
    }
    assert(false && "Dynamic terrain mesh was not uploaded");
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
    int at = slot(cache, mesh->identity), after = acount(cache->meshes) - at;
    asetlen(cache->meshes, acount(cache->meshes) + 1);
    uploaded *place = &cache->meshes[at];
    memmove(place + 1, place, (size_t)after * sizeof *place);
    *place = (uploaded){ mesh->identity, gpu };
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
    cf_draw3d_translate(render_mesh_asset_center(mesh));
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
    uint64_t generation = render_mesh_assets_generation();
    if (cache->pruned == generation) {
        return;
    }
    cache->pruned = generation;
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
