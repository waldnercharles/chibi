#include "runtime/geometry/mesh_asset.h"

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

void render_mesh_asset_tangents(render_mesh_asset *asset)
{
    int count = acount(asset->vertices);
    assert(acount(asset->indices) % 3 == 0);
    CF_V3 *sides = xcalloc((size_t)count * 2, sizeof *sides);
    CF_V3 *ups = sides + count;
    for (int i = 0; i < acount(asset->indices); i += 3) {
        const int *corner = asset->indices + i;
        for (int j = 0; j < 3; j++) {
            assert(corner[j] >= 0 && corner[j] < count);
        }
        const render_mesh_surface_vertex *a = &asset->vertices[corner[0]],
                                         *b = &asset->vertices[corner[1]],
                                         *c = &asset->vertices[corner[2]];
        CF_V3 ab = cf_sub_v3(b->position, a->position),
              ac = cf_sub_v3(c->position, a->position);
        CF_V2 st = cf_sub_v2(b->uv, a->uv), su = cf_sub_v2(c->uv, a->uv);
        float area = st.x * su.y - su.x * st.y;
        if (!isfinite(area) || fabsf(area) < 1e-20f) {
            continue;
        }
        /* Each triangle's texture directions count by its surface area, so a
         * sliver with nearly no texture area cannot dominate its corners. */
        float weight = cf_len_v3(cf_cross_v3(ab, ac)) * (area < 0 ? -1 : 1);
        CF_V3 side = cf_sub_v3(cf_mul_v3_f(ab, su.y), cf_mul_v3_f(ac, st.y));
        CF_V3 up = cf_sub_v3(cf_mul_v3_f(ac, st.x), cf_mul_v3_f(ab, su.x));
        side = cf_mul_v3_f(cf_safe_norm_v3(side), weight);
        up = cf_mul_v3_f(cf_safe_norm_v3(up), weight);
        for (int j = 0; j < 3; j++) {
            sides[corner[j]] = cf_add_v3(sides[corner[j]], side);
            ups[corner[j]] = cf_add_v3(ups[corner[j]], up);
        }
    }
    for (int i = 0; i < count; i++) {
        render_mesh_surface_vertex *v = &asset->vertices[i];
        CF_V3 side = cf_sub_v3(sides[i], cf_mul_v3_f(v->normal, cf_dot_v3(v->normal, sides[i])));
        float length = cf_len_v3(side);
        CF_V3 tangent = isfinite(length) && length > 1e-12f
                          ? cf_div_v3_f(side, length)
                          : perpendicular(v->normal);
        float hand = cf_dot_v3(cf_cross_v3(v->normal, tangent), ups[i]) < 0 ? -1.0f
                                                                            : 1.0f;
        v->tangent = (CF_V4){ tangent.x, tangent.y, tangent.z, hand };
    }
    free(sides);
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
