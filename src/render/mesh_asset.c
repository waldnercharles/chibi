#include "render/mesh_asset.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define MESH_VERTEX_LIMIT 1048575
#define MESH_FILE_LIMIT (128 * 1024 * 1024)
typedef struct
{
    char *source;
    render_mesh_asset asset;
    char error[256];
} mesh_source;
static mesh_source **sources;
static uint64_t next_identity = 1;

void render_mesh_asset_free(render_mesh_asset *asset)
{
    afree(asset->vertices);
    memset(asset, 0, sizeof *asset);
}

bool render_mesh_asset_parse(const char *text, render_mesh_asset *asset, char *error, size_t size)
{
    yyjson_doc *doc = text ? yyjson_read(text, strlen(text), 0) : NULL;
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *version = yyjson_obj_get(root, "version");
    yyjson_val *vertices = yyjson_obj_get(root, "vertices");
    size_t count = yyjson_arr_size(vertices);
    if (!yyjson_is_obj(root) || !yyjson_is_int(version) ||
        yyjson_get_int(version) != 1 || !yyjson_is_arr(vertices) || !count ||
        count > MESH_VERTEX_LIMIT || count % 3) {
        snprintf(error, size, "Mesh needs version 1 and a nonempty triangle list (at most %d vertices).", MESH_VERTEX_LIMIT);
        yyjson_doc_free(doc);
        return false;
    }
    render_mesh_asset fresh = { .min = { FLT_MAX, FLT_MAX, FLT_MAX },
                                .max = { -FLT_MAX, -FLT_MAX, -FLT_MAX } };
    for (size_t i = 0; i < count; i++) {
        yyjson_val *row = yyjson_arr_get(vertices, i);
        float values[8];
        if (!yyjson_is_arr(row) || yyjson_arr_size(row) != 8) {
            snprintf(error, size, "Mesh vertex %zu needs position, normal and UV (8 numbers).", i);
            goto fail;
        }
        for (int j = 0; j < 8; j++) {
            yyjson_val *value = yyjson_arr_get(row, (size_t)j);
            double number = yyjson_get_num(value);
            if (!yyjson_is_num(value) || !isfinite(number) || fabs(number) > FLT_MAX) {
                snprintf(error, size, "Mesh vertex %zu contains an invalid number.", i);
                goto fail;
            }
            values[j] = (float)number;
        }
        CF_V3 normal = { values[3], values[4], values[5] };
        float length = cf_len_v3(normal);
        if (!isfinite(length) || length < 1e-8f) {
            snprintf(error, size, "Mesh vertex %zu has an invalid normal.", i);
            goto fail;
        }
        render_mesh_surface_vertex vertex = { { values[0], values[1], values[2] },
                                              cf_div_v3_f(normal, length),
                                              { values[6], values[7] } };
        fresh.min = cf_min_v3(fresh.min, vertex.position);
        fresh.max = cf_max_v3(fresh.max, vertex.position);
        apush(fresh.vertices, vertex);
    }
    for (int i = 0; i < acount(fresh.vertices); i += 3) {
        CF_V3 a = fresh.vertices[i].position, b = fresh.vertices[i + 1].position,
              c = fresh.vertices[i + 2].position;
        float area = cf_len_v3(cf_cross_v3(cf_sub_v3(b, a), cf_sub_v3(c, a)));
        if (!isfinite(area) || area <= 1e-12f) {
            snprintf(error, size, "Mesh triangle %d is degenerate.", i / 3);
            goto fail;
        }
    }
    yyjson_doc_free(doc);
    render_mesh_asset_free(asset);
    *asset = fresh;
    if (size) {
        error[0] = 0;
    }
    return true;
fail:
    yyjson_doc_free(doc);
    render_mesh_asset_free(&fresh);
    return false;
}

const render_mesh_asset *render_mesh_asset_load(const char *source, char *error, size_t size)
{
    if (!source || strncmp(source, "content/", 8) || strstr(source, "..") ||
        strchr(source, '\\')) {
        snprintf(error, size, "Mesh source must use a content/... path without parent traversal.");
        return NULL;
    }
    for (int i = 0; i < acount(sources); i++) {
        if (!strcmp(sources[i]->source, source)) {
            if (size) {
                snprintf(error, size, "%s", sources[i]->error);
            }
            return sources[i]->asset.vertices ? &sources[i]->asset : NULL;
        }
    }
    mesh_source fresh = { .source = xstrdup(source) };
    size_t length = 0;
    void *bytes = cf_fs_read_entire_file_to_memory(source, &length);
    if (!bytes || length > MESH_FILE_LIMIT) {
        snprintf(fresh.error, sizeof fresh.error, "Cannot read mesh %s (maximum %d bytes).", source, MESH_FILE_LIMIT);
    } else {
        char *text = xmalloc(length + 1);
        memcpy(text, bytes, length);
        text[length] = 0;
        if (render_mesh_asset_parse(
                text,
                &fresh.asset,
                fresh.error,
                sizeof fresh.error
            )) {
            fresh.asset.identity = next_identity++;
        }
        free(text);
    }
    cf_free(bytes);
    mesh_source *loaded = xmalloc(sizeof *loaded);
    *loaded = fresh;
    apush(sources, loaded);
    if (size) {
        snprintf(error, size, "%s", loaded->error);
    }
    return loaded->asset.vertices ? &loaded->asset : NULL;
}

void render_mesh_assets_clear()
{
    for (int i = 0; i < acount(sources); i++) {
        free(sources[i]->source);
        render_mesh_asset_free(&sources[i]->asset);
        free(sources[i]);
    }
    afree(sources);
}
