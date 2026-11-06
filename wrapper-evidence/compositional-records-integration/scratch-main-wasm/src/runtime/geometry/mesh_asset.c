#include "runtime/geometry/mesh_asset.h"

#include "runtime/geometry/mesh_pick.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *source;
    render_mesh_asset asset;
    char error[256];
} mesh_source;
struct render_mesh_table
{
    mesh_source **items;
};
static render_mesh_table resident;

static uint64_t next_identity = 1;
static uint64_t generation;

/* The resident catalog changed, so a mesh may be gone or replaced. */
static void changed()
{
    generation++;
    render_mesh_picks_clear();
}

void render_mesh_asset_free(render_mesh_asset *asset)
{
    afree(asset->vertices);
    afree(asset->indices);
    for (int i = 0; i < acount(asset->groups); i++) {
        free(asset->groups[i].asset);
        free(asset->groups[i].name);
        free(asset->groups[i].path);
        free(asset->groups[i].material);
        free(asset->groups[i].material_scope);
    }
    afree(asset->groups);
    memset(asset, 0, sizeof *asset);
}

/* A unit vector perpendicular to `normal`. */
static CF_V3 perpendicular(CF_V3 normal)
{
    CF_V3 axis = fabsf(normal.x) < .9f ? (CF_V3){ 1, 0, 0 } : (CF_V3){ 0, 1, 0 };
    return cf_norm_v3(cf_cross_v3(normal, axis));
}

/* Tangents are summed in double, where the products of edges anywhere in
 * float's range neither overflow nor underflow. */
typedef struct
{
    double x, y, z;
} wide_v3;

/* The texture directions summed at one vertex, and the surface behind them. */
typedef struct
{
    wide_v3 side, up;
    double weight;
} texture_frame;

static wide_v3 widen(CF_V3 v)
{
    return (wide_v3){ v.x, v.y, v.z };
}

/* a * s + b * t */
static wide_v3 combine(wide_v3 a, double s, wide_v3 b, double t)
{
    return (wide_v3){ a.x * s + b.x * t, a.y * s + b.y * t, a.z * s + b.z * t };
}

static wide_v3 cross(wide_v3 a, wide_v3 b)
{
    return (wide_v3){ a.y * b.z - a.z * b.y,
                      a.z * b.x - a.x * b.z,
                      a.x * b.y - a.y * b.x };
}

static double dot(wide_v3 a, wide_v3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* `sum` plus `v` resized to `length`, or `sum` when `v` has no direction. */
static wide_v3 add_resized(wide_v3 sum, wide_v3 v, double length)
{
    double size = sqrt(dot(v, v));
    return size == 0 ? sum : combine(sum, 1, v, length / size);
}

void render_mesh_asset_tangents(render_mesh_asset *asset)
{
    int count = acount(asset->vertices);
    assert(acount(asset->indices) % 3 == 0);
    texture_frame *frames = xcalloc((size_t)count, sizeof *frames);
    for (int i = 0; i < acount(asset->indices); i += 3) {
        const int *corner = asset->indices + i;
        for (int j = 0; j < 3; j++) {
            assert(corner[j] >= 0 && corner[j] < count);
        }
        const render_mesh_surface_vertex *a = &asset->vertices[corner[0]],
                                         *b = &asset->vertices[corner[1]],
                                         *c = &asset->vertices[corner[2]];
        wide_v3 ab = combine(widen(b->position), 1, widen(a->position), -1),
                ac = combine(widen(c->position), 1, widen(a->position), -1);
        /* Texture offsets along ab and ac. */
        double u1 = (double)b->uv.x - a->uv.x, v1 = (double)b->uv.y - a->uv.y,
               u2 = (double)c->uv.x - a->uv.x, v2 = (double)c->uv.y - a->uv.y;
        double area = u1 * v2 - u2 * v1;
        if (!isfinite(area) || fabs(area) < 1e-20) {
            continue;
        }
        /* Each triangle's texture directions count by its surface area, so a
         * sliver with nearly no texture area cannot dominate its corners. */
        wide_v3 normal = cross(ab, ac);
        double weight = sqrt(dot(normal, normal)),
               signed_weight = area < 0 ? -weight : weight;
        wide_v3 side = combine(ab, v2, ac, -v1), up = combine(ac, u1, ab, -u2);
        for (int j = 0; j < 3; j++) {
            texture_frame *frame = &frames[corner[j]];
            frame->side = add_resized(frame->side, side, signed_weight);
            frame->up = add_resized(frame->up, up, signed_weight);
            frame->weight += weight;
        }
    }
    for (int i = 0; i < count; i++) {
        render_mesh_surface_vertex *v = &asset->vertices[i];
        const texture_frame *frame = &frames[i];
        assert(fabsf(cf_len_v3(v->normal) - 1) < 1e-3f);
        wide_v3 normal = widen(v->normal);
        wide_v3 side = combine(frame->side, 1, normal, -dot(normal, frame->side));
        double length = sqrt(dot(side, side));
        /* The sums grow with the mesh, so a direction is measured against the
         * surface that went into it. */
        CF_V3 tangent = isfinite(length) && length > 1e-12 * frame->weight
                          ? (CF_V3){ (float)(side.x / length),
                                     (float)(side.y / length),
                                     (float)(side.z / length) }
                          : perpendicular(v->normal);
        float hand = dot(cross(normal, widen(tangent)), frame->up) < 0 ? -1.0f : 1.0f;
        v->tangent = (CF_V4){ tangent.x, tangent.y, tangent.z, hand };
    }
    free(frames);
}

CF_V3 render_mesh_asset_center(const render_mesh_asset *asset)
{
    return cf_add_v3(cf_mul_v3_f(asset->min, .5f), cf_mul_v3_f(asset->max, .5f));
}

const render_mesh_asset *render_mesh_asset_load(const char *source, char *error, size_t size)
{
    if (!source || strncmp(source, "content/", 8) || strstr(source, "..") ||
        strchr(source, '\\')) {
        snprintf(error, size, "Mesh source must use a content/... path without parent traversal.");
        return NULL;
    }
    for (int i = 0; i < acount(resident.items); i++) {
        if (!strcmp(resident.items[i]->source, source)) {
            if (size) {
                snprintf(error, size, "%s", resident.items[i]->error);
            }
            return resident.items[i]->asset.vertices ? &resident.items[i]->asset : NULL;
        }
    }
    snprintf(error, size, "Mesh %s has not been prepared.", source);
    return NULL;
}
void render_mesh_asset_publish(const char *source, render_mesh_asset *asset)
{
    mesh_source *loaded = xmalloc(sizeof *loaded);
    *loaded = (mesh_source){ .source = xstrdup(source), .asset = *asset };
    loaded->asset.identity = next_identity++;
    *asset = (render_mesh_asset){ 0 };
    changed();
    for (int i = 0; i < acount(resident.items); i++) {
        if (!strcmp(resident.items[i]->source, source)) {
            free(resident.items[i]->source);
            render_mesh_asset_free(&resident.items[i]->asset);
            free(resident.items[i]);
            resident.items[i] = loaded;
            return;
        }
    }
    apush(resident.items, loaded);
}

void render_mesh_assets_clear()
{
    for (int i = 0; i < acount(resident.items); i++) {
        free(resident.items[i]->source);
        render_mesh_asset_free(&resident.items[i]->asset);
        free(resident.items[i]);
    }
    afree(resident.items);
    changed();
}

uint64_t render_mesh_assets_generation()
{
    return generation;
}

render_mesh_table *render_mesh_table_new_budget(size_t *remaining)
{
    assert(remaining);
    if (*remaining < sizeof(render_mesh_table)) {
        return NULL;
    }
    *remaining -= sizeof(render_mesh_table);
    return xcalloc(1, sizeof(render_mesh_table));
}
render_mesh_table *render_mesh_table_new()
{
    size_t remaining = SIZE_MAX;
    return render_mesh_table_new_budget(&remaining);
}
bool render_mesh_table_add_owned_budget(
    render_mesh_table *table,
    const char *source,
    render_mesh_asset *asset,
    size_t *remaining
)
{
    assert(table && asset && remaining);
    if (!source || strncmp(source, "content/", 8) || strstr(source, "..") ||
        strchr(source, '\\') || !acount(asset->vertices)) {
        return false;
    }
    for (int i = 0; i < acount(table->items); i++) {
        if (!strcmp(table->items[i]->source, source)) {
            return false;
        }
    }
    if (acount(table->items) >= 4096) {
        return false;
    }
    int cap = acap(table->items), next = cap;
    if (acount(table->items) == cap) {
        next = cap ? cap * 2 : 16;
    }
    size_t path_bytes = strlen(source) + 1;
    size_t charge = sizeof(mesh_source) + (size_t)(next - cap) * sizeof *table->items +
                    (cap ? 0 : sizeof(dyna_hdr));
    if (path_bytes > *remaining || charge > *remaining - path_bytes) {
        return false;
    }
    *remaining -= charge + path_bytes;
    mesh_source *item = xcalloc(1, sizeof *item);
    item->source = xstrdup(source);
    item->asset = *asset;
    item->asset.identity = next_identity++;
    *asset = (render_mesh_asset){ 0 };
    apush(table->items, item);
    return true;
}
bool render_mesh_table_add_owned(render_mesh_table *table, const char *source, render_mesh_asset *asset)
{
    size_t remaining = SIZE_MAX;
    return render_mesh_table_add_owned_budget(table, source, asset, &remaining);
}
void render_mesh_table_exchange(render_mesh_table *table)
{
    assert(table && table != &resident);
    mesh_source **old = resident.items;
    resident.items = table->items;
    table->items = old;
    changed();
}
void render_mesh_table_free(render_mesh_table *table)
{
    if (!table) {
        return;
    }
    assert(table != &resident);
    for (int i = 0; i < acount(table->items); i++) {
        free(table->items[i]->source);
        render_mesh_asset_free(&table->items[i]->asset);
        free(table->items[i]);
    }
    afree(table->items);
    free(table);
}
const render_mesh_table *render_mesh_table_current()
{
    return &resident;
}
int render_mesh_table_source_count(const render_mesh_table *table)
{
    assert(table);
    return acount(table->items);
}
const char *render_mesh_table_source_at(const render_mesh_table *table, int i)
{
    assert(table && i >= 0 && i < acount(table->items));
    return table->items[i]->source;
}
const render_mesh_asset *render_mesh_table_asset_at(const render_mesh_table *table, int i)
{
    assert(table && i >= 0 && i < acount(table->items));
    return &table->items[i]->asset;
}
