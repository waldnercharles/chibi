#include "authoring/mesh_prepare.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <cute_math.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <yyjson.h>
#define MESH_VERTEX_LIMIT 1048575
#define MESH_FILE_LIMIT (128 * 1024 * 1024)

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
        render_mesh_surface_vertex vertex = {
            .position = { values[0], values[1], values[2] },
            .normal = cf_div_v3_f(normal, length),
            .uv = { values[6], values[7] }
        };
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

static char *content_root;

bool authoring_mesh_mount_content(const char *root, char *error, size_t size)
{
    struct stat info;
    if (!root || stat(root, &info) || !S_ISDIR(info.st_mode)) {
        if (size) {
            snprintf(error, size, "Mesh content root is not a directory");
        }
        return false;
    }
    free(content_root);
    content_root = xstrdup(root);
    return true;
}

void authoring_mesh_unmount_content()
{
    free(content_root);
    content_root = NULL;
}

const render_mesh_asset *authoring_mesh_prepare(const char *source, char *error, size_t size)
{
    const render_mesh_asset *resident = render_mesh_asset_load(source, error, size);
    if (resident) {
        return resident;
    }
    if (!source || strncmp(source, "content/", 8) || strstr(source, "..") ||
        strchr(source, '\\')) {
        return NULL;
    }
    if (!content_root) {
        if (size) {
            snprintf(error, size, "Mesh content root is not mounted");
        }
        return NULL;
    }
    size_t path_size = strlen(content_root) + strlen(source + 8) + 2;
    char *path = xmalloc(path_size);
    snprintf(path, path_size, "%s/%s", content_root, source + 8);
    FILE *file = fopen(path, "rb");
    free(path);
    bool valid = file && !fseek(file, 0, SEEK_END);
    long length = valid ? ftell(file) : -1;
    valid = length >= 0 && length <= MESH_FILE_LIMIT && !fseek(file, 0, SEEK_SET);
    char *text = valid ? xmalloc((size_t)length + 1) : NULL;
    if (valid) {
        valid = fread(text, 1, (size_t)length, file) == (size_t)length &&
                fgetc(file) == EOF;
    }
    if (file) {
        valid = fclose(file) == 0 && valid;
    }
    if (!valid) {
        if (size) {
            snprintf(error, size, "Cannot read mesh %s (maximum %d bytes).", source, MESH_FILE_LIMIT);
        }
        free(text);
        return NULL;
    }
    text[length] = 0;
    render_mesh_asset asset = { 0 };
    valid = render_mesh_asset_parse(text, &asset, error, size);
    free(text);
    if (!valid) {
        return NULL;
    }
    render_mesh_asset_publish(source, &asset);
    return render_mesh_asset_load(source, error, size);
}
