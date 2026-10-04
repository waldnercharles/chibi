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

static bool numbers(yyjson_val *row, float *values, int count)
{
    if (!yyjson_is_arr(row) || yyjson_arr_size(row) != (size_t)count) {
        return false;
    }
    for (int j = 0; j < count; j++) {
        yyjson_val *value = yyjson_arr_get(row, (size_t)j);
        double number = yyjson_get_num(value);
        if (!yyjson_is_num(value) || !isfinite(number) || fabs(number) > FLT_MAX) {
            return false;
        }
        values[j] = (float)number;
    }
    return true;
}
static char *group_string(yyjson_val *group, const char *key)
{
    const char *text = yyjson_get_str(yyjson_obj_get(group, key));
    return text ? xstrdup(text) : NULL;
}
bool render_mesh_asset_parse(const char *text, render_mesh_asset *asset, char *error, size_t size)
{
    size_t length = text ? strlen(text) : 0;
    if (length > MESH_FILE_LIMIT) {
        snprintf(error, size, "Mesh exceeds the %d-byte file limit.", MESH_FILE_LIMIT);
        return false;
    }
    yyjson_doc *doc = text ? yyjson_read(text, length, 0) : NULL;
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
    yyjson_val *uv_bounds = yyjson_obj_get(root, "uv_bounds");
    if (uv_bounds) {
        float values[6];
        if (!numbers(uv_bounds, values, 6)) {
            snprintf(error, size, "Mesh UV bounds need six finite numbers.");
            goto fail;
        }
        for (int i = 0; i < 3; i++) {
            if (values[i] >= values[i + 3]) {
                snprintf(error, size, "Mesh UV bounds need positive extents.");
                goto fail;
            }
        }
        fresh.has_uv_bounds = true;
        fresh.uv_min = (CF_V3){ values[0], values[1], values[2] };
        fresh.uv_max = (CF_V3){ values[3], values[4], values[5] };
    }
    yyjson_val *part_positions = yyjson_obj_get(root, "part_positions");
    yyjson_val *part_frames = yyjson_obj_get(root, "part_frames");
    if ((part_positions || part_frames) &&
        (!yyjson_is_arr(part_positions) || !yyjson_is_arr(part_frames) ||
         yyjson_arr_size(part_positions) != count ||
         yyjson_arr_size(part_frames) != count)) {
        snprintf(error, size, "Mesh part positions and frames must both match the vertex count.");
        goto fail;
    }
    fresh.has_part_coordinates = part_positions != NULL;
    yyjson_arr_iter rows = yyjson_arr_iter_with(vertices);
    yyjson_arr_iter positions = yyjson_arr_iter_with(part_positions);
    yyjson_arr_iter frames = yyjson_arr_iter_with(part_frames);
    for (size_t i = 0; i < count; i++) {
        yyjson_val *row = yyjson_arr_iter_next(&rows);
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
        vertex.part_position = vertex.position;
        for (int j = 0; j < 3; j++) {
            vertex.part_from_object[j][j] = 1;
        }
        if (part_positions &&
            (!numbers(yyjson_arr_iter_next(&positions), &vertex.part_position.x, 3) ||
             !numbers(yyjson_arr_iter_next(&frames), &vertex.part_from_object[0][0], 9))) {
            snprintf(error, size, "Mesh vertex %zu has invalid part coordinates/frame.", i);
            goto fail;
        }
        fresh.min = cf_min_v3(fresh.min, vertex.position);
        fresh.max = cf_max_v3(fresh.max, vertex.position);
        apush(fresh.vertices, vertex);
    }
    for (int i = 0; i < acount(fresh.vertices); i += 3) {
        CF_V3 a = fresh.vertices[i].position, b = fresh.vertices[i + 1].position,
              c = fresh.vertices[i + 2].position;
        float area = cf_len_v3(cf_cross_v3(cf_sub_v3(b, a), cf_sub_v3(c, a)));
        if (!isfinite(area) || area <= RENDER_MESH_MIN_TRIANGLE_AREA) {
            snprintf(error, size, "Mesh triangle %d is degenerate.", i / 3);
            goto fail;
        }
    }
    yyjson_val *groups = yyjson_obj_get(root, "groups");
    if (groups && (!yyjson_is_arr(groups) || !yyjson_arr_size(groups) ||
                   yyjson_arr_size(groups) > 1024)) {
        snprintf(error, size, "Mesh groups must be a nonempty array with at most 1024 entries.");
        goto fail;
    }
    int covered = 0;
    for (size_t i = 0; i < yyjson_arr_size(groups); i++) {
        yyjson_val *g = yyjson_arr_get(groups, i);
        yyjson_val *first = yyjson_obj_get(g, "first"),
                   *length = yyjson_obj_get(g, "count"),
                   *part = yyjson_obj_get(g, "part"),
                   *node = yyjson_obj_get(g, "node");
        if (!yyjson_is_obj(g) || !yyjson_is_int(first) || !yyjson_is_int(length) ||
            !yyjson_is_int(part) || !yyjson_is_uint(node) ||
            yyjson_get_int(first) != covered || yyjson_get_int(length) <= 0 ||
            yyjson_get_int(length) % 3 || yyjson_get_int(length) > (int)count - covered ||
            yyjson_get_int(part) < -1 || yyjson_get_int(part) > 1023) {
            snprintf(error, size, "Mesh group %zu must cover contiguous complete triangles.", i);
            goto fail;
        }
        render_mesh_group group = { .first = covered,
                                    .count = (int)yyjson_get_int(length),
                                    .part = (int)yyjson_get_int(part),
                                    .node = yyjson_get_uint(node),
                                    .asset = group_string(g, "asset"),
                                    .name = group_string(g, "name"),
                                    .path = group_string(g, "path"),
                                    .material = group_string(g, "material"),
                                    .material_scope = yyjson_obj_get(g, "material_scope")
                                                        ? group_string(g, "material_scope")
                                                        : xstrdup("") };
        apush(fresh.groups, group);
        if (!group.asset || !group.name || !group.path || !group.material ||
            !group.material_scope || strlen(group.material) >= 128) {
            snprintf(error, size, "Mesh group %zu needs asset/name/path/material strings (material below 128 bytes).", i);
            goto fail;
        }
        covered += group.count;
    }
    if (groups && covered != (int)count) {
        snprintf(error, size, "Mesh groups must cover every vertex exactly once.");
        goto fail;
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
