#include "runtime/geometry/mesh_prepared.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MESH_PREPARED_VERTEX_MAX 1048575
static void v3(execution_wire *w, CF_V3 *p)
{
    execution_wire_float(w, &p->x);
    execution_wire_float(w, &p->y);
    execution_wire_float(w, &p->z);
}
static bool finite3(CF_V3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}
static bool valid(const render_mesh_asset *a)
{
    int n = acount(a->vertices);
    if (!n || n > MESH_PREPARED_VERTEX_MAX || n % 3 ||
        acount(a->groups) > 1024 || !finite3(a->min) || !finite3(a->max) ||
        a->min.x > a->max.x || a->min.y > a->max.y || a->min.z > a->max.z ||
        !finite3(a->uv_min) || !finite3(a->uv_max) ||
        (a->has_uv_bounds &&
         (a->uv_min.x >= a->uv_max.x || a->uv_min.y >= a->uv_max.y ||
          a->uv_min.z >= a->uv_max.z))) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        const render_mesh_surface_vertex *v = &a->vertices[i];
        if (!finite3(v->position) || !finite3(v->normal) ||
            !finite3(v->part_position) || !isfinite(v->uv.x) || !isfinite(v->uv.y) ||
            v->position.x < a->min.x || v->position.x > a->max.x ||
            v->position.y < a->min.y || v->position.y > a->max.y ||
            v->position.z < a->min.z || v->position.z > a->max.z) {
            return false;
        }
        double norm = (double)v->normal.x * v->normal.x +
                      (double)v->normal.y * v->normal.y +
                      (double)v->normal.z * v->normal.z;
        if (!isfinite(norm) || norm < 1e-16) {
            return false;
        }
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                if (!isfinite(v->part_from_object[j][k])) {
                    return false;
                }
            }
        }
    }
    for (int i = 0; i < n; i += 3) {
        CF_V3 a0 = a->vertices[i].position, b = a->vertices[i + 1].position,
              c = a->vertices[i + 2].position;
        double x1 = (double)b.x - a0.x, y1 = (double)b.y - a0.y,
               z1 = (double)b.z - a0.z;
        double x2 = (double)c.x - a0.x, y2 = (double)c.y - a0.y,
               z2 = (double)c.z - a0.z;
        double x = y1 * z2 - z1 * y2, y = z1 * x2 - x1 * z2, z = x1 * y2 - y1 * x2;
        double area = sqrt(x * x + y * y + z * z);
        if (!isfinite(area) || area <= RENDER_MESH_MIN_TRIANGLE_AREA) {
            return false;
        }
    }
    int covered = 0;
    for (int i = 0; i < acount(a->groups); i++) {
        const render_mesh_group *g = &a->groups[i];
        if (g->first != covered || g->count <= 0 || g->count % 3 ||
            g->count > n - covered || g->part < -1 || g->part > 1023 ||
            !g->asset || !g->name || !g->path || !g->material ||
            !g->material_scope || strlen(g->material) >= 128) {
            return false;
        }
        covered += g->count;
    }
    return !acount(a->groups) || covered == n;
}
static void wire(execution_wire *w, render_mesh_asset *a)
{
    uint64_t magic = UINT64_C(0x314853454d484343),
             version = RENDER_MESH_PREPARED_VERSION;
    execution_wire_u64(w, &magic);
    execution_wire_u64(w, &version);
    if (!w->error && (magic != UINT64_C(0x314853454d484343) ||
                      version != RENDER_MESH_PREPARED_VERSION)) {
        w->error = "Unknown prepared mesh format";
    }
    execution_wire_bool(w, &a->has_part_coordinates);
    execution_wire_bool(w, &a->has_uv_bounds);
    v3(w, &a->uv_min);
    v3(w, &a->uv_max);
    v3(w, &a->min);
    v3(w, &a->max);
    int count = w->reading ? 0 : acount(a->vertices);
    execution_wire_count(w, &count, 8);
    if (!w->error && count > MESH_PREPARED_VERTEX_MAX) {
        w->error = "Prepared mesh vertex limit";
    }
    for (int i = 0; i < count && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(render_mesh_surface_vertex))) {
                break;
            }
            render_mesh_surface_vertex value = { 0 };
            apush(a->vertices, value);
        }
        render_mesh_surface_vertex *v = &a->vertices[i];
        v3(w, &v->position);
        v3(w, &v->normal);
        execution_wire_float(w, &v->uv.x);
        execution_wire_float(w, &v->uv.y);
        v3(w, &v->part_position);
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                execution_wire_float(w, &v->part_from_object[j][k]);
            }
        }
    }
    count = w->reading ? 0 : acount(a->groups);
    execution_wire_count(w, &count, 8);
    if (!w->error && count > 1024) {
        w->error = "Prepared mesh group limit";
    }
    for (int i = 0; i < count && !w->error; i++) {
        if (w->reading) {
            if (!execution_wire_reserve(w, sizeof(render_mesh_group))) {
                break;
            }
            render_mesh_group value = { 0 };
            apush(a->groups, value);
        }
        render_mesh_group *g = &a->groups[i];
        execution_wire_int(w, &g->first);
        execution_wire_int(w, &g->count);
        execution_wire_int(w, &g->part);
        execution_wire_u64(w, &g->node);
        execution_wire_owned_string(w, &g->asset);
        execution_wire_owned_string(w, &g->name);
        execution_wire_owned_string(w, &g->path);
        execution_wire_owned_string(w, &g->material);
        execution_wire_owned_string(w, &g->material_scope);
    }
}
static bool report(execution_wire *w, char *error, size_t size)
{
    if (!w->error) {
        return true;
    }
    if (size) {
        snprintf(error, size, "%s", w->error);
    }
    return false;
}
bool render_mesh_asset_prepared_write(
    const render_mesh_asset *a,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
)
{
    assert(a && data && length);
    *data = NULL;
    *length = 0;
    if (!valid(a)) {
        if (size) {
            snprintf(error, size, "Invalid prepared mesh payload");
        }
        return false;
    }
    execution_wire w = { 0 };
    render_mesh_asset view = *a;
    wire(&w, &view);
    if (!report(&w, error, size)) {
        afree(w.data);
        return false;
    }
    *data = xmalloc(w.size);
    memcpy(*data, w.data, w.size);
    *length = w.size;
    afree(w.data);
    return true;
}
bool render_mesh_asset_prepared_read_budget(
    const void *data,
    size_t length,
    size_t *remaining,
    render_mesh_asset *result,
    char *error,
    size_t size
)
{
    assert(remaining && result);
    size_t budget = *remaining < EXECUTION_WIRE_MAX_BYTES ? *remaining
                                                          : EXECUTION_WIRE_MAX_BYTES;
    arena storage = { 0 };
    execution_wire w = { .data = (unsigned char *)data,
                         .size = length,
                         .reading = true,
                         .storage = &storage,
                         .owned_bytes = EXECUTION_WIRE_MAX_BYTES - budget };
    render_mesh_asset decoded = { 0 };
    wire(&w, &decoded);
    if (!w.error && w.offset != w.size) {
        w.error = "Trailing prepared mesh bytes";
    }
    if (!w.error && !valid(&decoded)) {
        w.error = "Invalid prepared mesh payload";
    }
    arena_free(&storage);
    if (!report(&w, error, size)) {
        render_mesh_asset_free(&decoded);
        return false;
    }
    size_t used = w.owned_bytes - (EXECUTION_WIRE_MAX_BYTES - budget);
    render_mesh_asset_free(result);
    *result = decoded;
    *remaining -= used;
    return true;
}
