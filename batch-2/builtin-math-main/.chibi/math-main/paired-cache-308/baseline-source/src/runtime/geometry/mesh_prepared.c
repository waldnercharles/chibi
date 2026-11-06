#include "runtime/geometry/mesh_prepared.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
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
static bool inside(CF_V3 p, CF_V3 min, CF_V3 max)
{
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
           p.z >= min.z && p.z <= max.z;
}
static bool nonzero(CF_V3 v)
{
    double norm = (double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z;
    return isfinite(norm) && norm >= 1e-16;
}
static bool valid_vertex(const render_mesh_asset *a, const render_mesh_surface_vertex *v)
{
    CF_V3 tangent = { v->tangent.x, v->tangent.y, v->tangent.z };
    if (!finite3(v->position) || !inside(v->position, a->min, a->max) ||
        !nonzero(v->normal) || !isfinite(v->uv.x) || !isfinite(v->uv.y) ||
        !nonzero(tangent) || (v->tangent.w != 1 && v->tangent.w != -1)) {
        return false;
    }
    if (!a->has_part_coordinates) {
        return true;
    }
    if (!finite3(v->part_position)) {
        return false;
    }
    for (int j = 0; j < 3; j++) {
        for (int k = 0; k < 3; k++) {
            if (!isfinite(v->part_from_object[j][k])) {
                return false;
            }
        }
    }
    return true;
}
static bool valid_triangle(const render_mesh_asset *a, const int *corner)
{
    int n = acount(a->vertices);
    for (int j = 0; j < 3; j++) {
        if (corner[j] < 0 || corner[j] >= n) {
            return false;
        }
    }
    CF_V3 a0 = a->vertices[corner[0]].position,
          b = a->vertices[corner[1]].position, c = a->vertices[corner[2]].position;
    double x1 = (double)b.x - a0.x, y1 = (double)b.y - a0.y, z1 = (double)b.z - a0.z;
    double x2 = (double)c.x - a0.x, y2 = (double)c.y - a0.y, z2 = (double)c.z - a0.z;
    double x = y1 * z2 - z1 * y2, y = z1 * x2 - x1 * z2, z = x1 * y2 - y1 * x2;
    double area = sqrt(x * x + y * y + z * z);
    return isfinite(area) && area > RENDER_MESH_MIN_TRIANGLE_AREA;
}
static bool valid_group(const render_mesh_group *g, int covered, int indices)
{
    return g->first == covered && g->count > 0 && g->count % 3 == 0 &&
           g->count <= indices - covered && g->part >= -1 && g->part <= 1023 &&
           g->asset && g->name && g->path && g->material && g->material_scope &&
           strlen(g->material) < 128;
}
static bool valid_bounds(const render_mesh_asset *a)
{
    return finite3(a->min) && finite3(a->max) && a->min.x <= a->max.x &&
           a->min.y <= a->max.y && a->min.z <= a->max.z && finite3(a->uv_min) &&
           finite3(a->uv_max) &&
           (!a->has_uv_bounds ||
            (a->uv_min.x < a->uv_max.x && a->uv_min.y < a->uv_max.y &&
             a->uv_min.z < a->uv_max.z));
}
/* Writes why the mesh is invalid and returns false. */
static bool refuse(char *error, size_t size, const char *format, ...)
{
    if (!size) {
        return false;
    }
    int n = snprintf(error, size, "Invalid prepared mesh: ");
    va_list arguments;
    va_start(arguments, format);
    if (n >= 0 && (size_t)n < size) {
        vsnprintf(error + n, size - (size_t)n, format, arguments);
    }
    va_end(arguments);
    return false;
}
static bool valid(const render_mesh_asset *a, char *error, size_t size)
{
    int n = acount(a->vertices), indices = acount(a->indices),
        groups = acount(a->groups);
    if (!n || n > MESH_PREPARED_VERTEX_MAX) {
        return refuse(error, size, "%d vertices (1 to %d)", n, MESH_PREPARED_VERTEX_MAX);
    }
    if (!indices || indices % 3) {
        return refuse(error, size, "%d indices are not whole triangles", indices);
    }
    if (groups > RENDER_MESH_PREPARED_GROUP_MAX) {
        return refuse(error, size, "%d groups (at most %d)", groups, RENDER_MESH_PREPARED_GROUP_MAX);
    }
    if (!valid_bounds(a)) {
        return refuse(error, size, "bounds are not finite and ordered");
    }
    for (int i = 0; i < n; i++) {
        if (!valid_vertex(a, &a->vertices[i])) {
            return refuse(error, size, "vertex %d is not finite, lies outside the bounds, or has a zero normal or tangent or a handedness other than 1 or -1", i);
        }
    }
    for (int i = 0; i < indices; i += 3) {
        if (!valid_triangle(a, a->indices + i)) {
            return refuse(error, size, "triangle %d names a missing vertex or has no area", i / 3);
        }
    }
    int covered = 0;
    for (int i = 0; i < groups; i++) {
        if (!valid_group(&a->groups[i], covered, indices)) {
            return refuse(error, size, "group %d must continue the indices with whole triangles and name its part and material", i);
        }
        covered += a->groups[i].count;
    }
    if (groups && covered != indices) {
        return refuse(error, size, "groups cover %d of %d indices", covered, indices);
    }
    return true;
}
/* Part coordinates are stored only for meshes that have them. The others
 * read back with each vertex's own position and an identity frame. */
static void wire_vertex(execution_wire *w, bool parts, render_mesh_surface_vertex *v)
{
    v3(w, &v->position);
    v3(w, &v->normal);
    execution_wire_float(w, &v->uv.x);
    execution_wire_float(w, &v->uv.y);
    execution_wire_float(w, &v->tangent.x);
    execution_wire_float(w, &v->tangent.y);
    execution_wire_float(w, &v->tangent.z);
    execution_wire_float(w, &v->tangent.w);
    if (parts) {
        v3(w, &v->part_position);
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                execution_wire_float(w, &v->part_from_object[j][k]);
            }
        }
        return;
    }
    if (!w->reading) {
        return;
    }
    v->part_position = v->position;
    for (int j = 0; j < 3; j++) {
        for (int k = 0; k < 3; k++) {
            v->part_from_object[j][k] = j == k;
        }
    }
}
static void wire_group(execution_wire *w, render_mesh_group *g)
{
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
static void wire(execution_wire *w, render_mesh_asset *a)
{
    uint64_t magic = UINT64_C(0x314853454d484343),
             version = RENDER_MESH_PREPARED_VERSION;
    execution_wire_u64(w, &magic);
    execution_wire_u64(w, &version);
    if (!w->error && magic != UINT64_C(0x314853454d484343)) {
        w->error = "Not a prepared mesh";
    }
    if (!w->error && version != RENDER_MESH_PREPARED_VERSION) {
        w->error = "Prepared mesh version differs from this engine's";
    }
    execution_wire_bool(w, &a->has_part_coordinates);
    execution_wire_bool(w, &a->has_uv_bounds);
    v3(w, &a->uv_min);
    v3(w, &a->uv_max);
    v3(w, &a->min);
    v3(w, &a->max);
    int count = w->reading ? 0 : acount(a->vertices);
    execution_wire_count(w, &count, 12);
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
        wire_vertex(w, a->has_part_coordinates, &a->vertices[i]);
    }
    count = w->reading ? 0 : acount(a->indices);
    execution_wire_count(w, &count, 1);
    if (!w->error && w->reading &&
        execution_wire_reserve(w, (size_t)count * sizeof(int))) {
        asetlen(a->indices, count);
    }
    for (int i = 0; i < count && !w->error; i++) {
        execution_wire_int(w, &a->indices[i]);
    }
    count = w->reading ? 0 : acount(a->groups);
    execution_wire_count(w, &count, 1);
    if (!w->error && count > RENDER_MESH_PREPARED_GROUP_MAX) {
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
        wire_group(w, &a->groups[i]);
    }
}
bool render_mesh_prepared_fits(int vertices, int indices)
{
    if (vertices > MESH_PREPARED_VERTEX_MAX) {
        return false;
    }
    size_t vertex_bytes = (size_t)vertices * sizeof(render_mesh_surface_vertex);
    return vertex_bytes <= EXECUTION_WIRE_MAX_BYTES &&
           (size_t)indices <= (EXECUTION_WIRE_MAX_BYTES - vertex_bytes) / sizeof(int);
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
    if (!valid(a, error, size)) {
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
    arena_free(&storage);
    if (!report(&w, error, size) || !valid(&decoded, error, size)) {
        render_mesh_asset_free(&decoded);
        return false;
    }
    size_t used = w.owned_bytes - (EXECUTION_WIRE_MAX_BYTES - budget);
    render_mesh_asset_free(result);
    *result = decoded;
    *remaining -= used;
    return true;
}
