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
