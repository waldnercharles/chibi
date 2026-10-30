#include "runtime/models/model_bake.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    render_mesh_surface_vertex vertices[3];
    int part;
} bake_triangle;
typedef struct
{
    const model_program *program;
    model_bake_options options;
    model_bake_result result;
    float (*scratch)[4];
    bake_triangle *triangles;
    shader_error *error;
} baker;
static bool fail(baker *b, const char *message)
{
    b->error->node = 0;
    snprintf(b->error->message, sizeof b->error->message, "%s", message);
    return false;
}
model_bake_options model_bake_defaults()
{
    return (model_bake_options){ .max_axis_cells = 128,
                                 .max_triangles = 100000,
                                 .edge_iterations = 10,
                                 .max_samples = 2000000,
                                 .max_work = 1000000000 };
}
void model_bake_result_free(model_bake_result *result)
{
    render_mesh_asset_free(&result->mesh);
    memset(result, 0, sizeof *result);
}
static bool charge(baker *b, uint64_t work)
{
    if (b->result.samples >= b->options.max_samples ||
        work > b->options.max_work - b->result.work) {
        return fail(b, "Mesh bake exceeded its sample/node-work budget; use a coarser cell size or larger explicit budget");
    }
    b->result.samples++;
    b->result.work += work;
    return true;
}
static bool distance(baker *b, CF_V3 point, float *result)
{
    if (!charge(b, (uint64_t)acount(b->program->shader.nodes))) {
        return false;
    }
    shader_sample_context sample = {
        .resolve_image = b->program->resolve_image,
        .image_userdata = b->program->image_userdata,
        .object_position = { point.x, point.y, point.z }
    };
    if (!shader_graph_eval_values_validated(
            &b->program->shader,
            &sample,
            b->result.parameters,
            b->scratch,
            b->error
        )) {
        return false;
    }
    *result = b->scratch[b->program->shader.output[0]][0];
    return isfinite(*result) || fail(b, "Mesh bake encountered a non-finite distance");
}
static bool hit(baker *b, CF_V3 point, model_hit *result)
{
    /* Conservative budget charge includes retained hit IR and bounded numeric
     * frame preparation. Work is a budget estimate, not GPU instruction count. */
    return charge(b, (uint64_t)acount(b->program->hit_shader.nodes) + SHADER_NODE_MAX) &&
           model_program_hit(b->program, b->result.parameters, &point.x, result, b->error);
}
static bool edge_point(baker *b, CF_V3 a, CF_V3 c, float da, float dc, CF_V3 *point)
{
    if (da == 0) {
        *point = a;
        return true;
    }
    if (dc == 0) {
        *point = c;
        return true;
    }
    for (int i = 0; i < b->options.edge_iterations; i++) {
        float t = da / (da - dc);
        t = fmaxf(.1f, fminf(.9f, t));
        CF_V3 mid = cf_add_v3(a, cf_mul_v3_f(cf_sub_v3(c, a), t));
        float d;
        if (!distance(b, mid, &d)) {
            return false;
        }
        if ((d < 0) == (da < 0)) {
            a = mid;
            da = d;
        } else {
            c = mid;
            dc = d;
        }
    }
    float t = da / (da - dc);
    *point = cf_add_v3(a, cf_mul_v3_f(cf_sub_v3(c, a), t));
    return true;
}
static bool vertex(baker *b, int owner, CF_V3 point, CF_V3 fallback, render_mesh_surface_vertex *vertex)
{
    float epsilon = b->result.cell_size * .01f;
    CF_V3 gradient = { 0 };
    for (int axis = 0; axis < 3; axis++) {
        CF_V3 a = point, c = point;
        (&a.x)[axis] -= epsilon;
        (&c.x)[axis] += epsilon;
        float da, dc;
        if (!distance(b, a, &da) || !distance(b, c, &dc)) {
            return false;
        }
        (&gradient.x)[axis] = (dc - da) / (2 * epsilon);
    }
    float length = cf_len_v3(gradient);
    *vertex = (render_mesh_surface_vertex){
        .position = point,
        .normal = length > 1e-8f ? cf_div_v3_f(gradient, length) : fallback,
        .uv = { point.x, point.z }
    };
    CF_V3 local;
    for (int i = 0; i < 3; i++) {
        float extent = b->result.bounds.max[i] - b->result.bounds.min[i];
        (&local.x)[i] = ((&point.x)[i] -
                         (b->result.bounds.max[i] + b->result.bounds.min[i]) * .5f) /
                        extent;
    }
    CF_V3 side = { fabsf(vertex->normal.x),
                   fabsf(vertex->normal.y),
                   fabsf(vertex->normal.z) };
    vertex->uv = side.y >= side.x && side.y >= side.z
                   ? (CF_V2){ local.x + .5f, local.z + .5f }
               : side.x >= side.z ? (CF_V2){ local.z + .5f, .5f - local.y }
                                  : (CF_V2){ local.x + .5f, .5f - local.y };
    if (!charge(b, (uint64_t)acount(b->program->shader.nodes) + SHADER_NODE_MAX) ||
        !model_program_part_point(
            b->program,
            b->result.parameters,
            owner,
            &point.x,
            &vertex->part_position.x,
            vertex->part_from_object,
            b->error
        )) {
        return false;
    }
    float residual;
    if (!distance(b, point, &residual)) {
        return false;
    }
    b->result.max_residual = fmaxf(b->result.max_residual, fabsf(residual));
    return true;
}
static bool triangle(baker *b, CF_V3 a, CF_V3 c, CF_V3 d)
{
    CF_V3 u = cf_sub_v3(c, a), v = cf_sub_v3(d, a);
    /* Use double products for degeneracy: fused float cross products can leave
     * a nonzero residual even when two cut vertices coincide exactly. */
    CF_V3 normal = { (float)((double)u.y * v.z - (double)u.z * v.y),
                     (float)((double)u.z * v.x - (double)u.x * v.z),
                     (float)((double)u.x * v.y - (double)u.y * v.x) };
    float area = cf_len_v3(normal);
    float edge_area = cf_len_v3(u) * cf_len_v3(v);
    /* The exported float positions must also satisfy the importer's absolute
     * area floor; near-grid corner cuts can form microscopic right triangles. */
    float stored_area = cf_len_v3(cf_cross_v3(u, v));
    if (area <= edge_area * 1e-7f || stored_area <= RENDER_MESH_MIN_TRIANGLE_AREA) {
        return true;
    }
    if (!isfinite(area) || acount(b->triangles) >= b->options.max_triangles) {
        return fail(b, "Mesh bake exceeded triangle budget or finite geometry; use a coarser cell size");
    }
    CF_V3 center = cf_div_v3_f(cf_add_v3(cf_add_v3(a, c), d), 3);
    model_hit owner;
    if (!hit(b, center, &owner)) {
        return false;
    }
    bake_triangle t = { .part = owner.part };
    normal = cf_div_v3_f(normal, area);
    CF_V3 points[] = { a, c, d };
    for (int i = 0; i < 3; i++) {
        if (!vertex(b, owner.part, points[i], normal, &t.vertices[i])) {
            return false;
        }
        model_hit at_vertex;
        if (!hit(b, points[i], &at_vertex)) {
            return false;
        }
        if (at_vertex.part != owner.part) {
            b->result.boundary_triangles++;
            /* Count a boundary face once, not each changed corner. */
            break;
        }
    }
    /* Complete vertex records even after ownership boundary detection. */
    for (int i = 0; i < 3; i++) {
        if (cf_len_v3(t.vertices[i].normal) == 0 &&
            !vertex(b, owner.part, points[i], normal, &t.vertices[i])) {
            return false;
        }
    }
    CF_V3 expected = cf_add_v3(
        cf_add_v3(t.vertices[0].normal, t.vertices[1].normal),
        t.vertices[2].normal
    );
    if (cf_dot_v3(normal, expected) < 0) {
        render_mesh_surface_vertex swap = t.vertices[1];
        t.vertices[1] = t.vertices[2];
        t.vertices[2] = swap;
    }
    apush(b->triangles, t);
    return true;
}
static bool tetrahedron(baker *b, const CF_V3 points[8], const float values[8], const int indices[4])
{
    int inside[4], outside[4], ni = 0, no = 0;
    for (int i = 0; i < 4; i++) {
        int id = indices[i];
        if (values[id] < 0) {
            inside[ni++] = id;
        } else {
            outside[no++] = id;
        }
    }
    if (!ni || !no) {
        return true;
    }
    CF_V3 cuts[4];
    if (ni == 2) {
        int a[] = { inside[0], inside[0], inside[1], inside[1] };
        int c[] = { outside[0], outside[1], outside[1], outside[0] };
        for (int i = 0; i < 4; i++) {
            if (!edge_point(b, points[a[i]], points[c[i]], values[a[i]], values[c[i]], &cuts[i])) {
                return false;
            }
        }
        return triangle(b, cuts[0], cuts[1], cuts[2]) &&
               triangle(b, cuts[0], cuts[2], cuts[3]);
    }
    int a = ni == 1 ? inside[0] : outside[0];
    const int *other = ni == 1 ? outside : inside;
    for (int i = 0; i < 3; i++) {
        int c = other[i];
        if (!edge_point(b, points[a], points[c], values[a], values[c], &cuts[i])) {
            return false;
        }
    }
    return triangle(b, cuts[0], cuts[1], cuts[2]);
}
static void publish_mesh(baker *b)
{
    render_mesh_asset *mesh = &b->result.mesh;
    mesh->has_part_coordinates = true;
    mesh->has_uv_bounds = true;
    mesh->uv_min = (CF_V3){ b->result.bounds.min[0],
                            b->result.bounds.min[1],
                            b->result.bounds.min[2] };
    mesh->uv_max = (CF_V3){ b->result.bounds.max[0],
                            b->result.bounds.max[1],
                            b->result.bounds.max[2] };
    mesh->min = (CF_V3){ FLT_MAX, FLT_MAX, FLT_MAX };
    mesh->max = (CF_V3){ -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (int part = -1; part < acount(b->program->parts); part++) {
        render_mesh_group group = { .first = acount(mesh->indices), .part = part };
        for (int t = 0; t < acount(b->triangles); t++) {
            if (b->triangles[t].part != part) {
                continue;
            }
            for (int v = 0; v < 3; v++) {
                render_mesh_surface_vertex vertex = b->triangles[t].vertices[v];
                apush(mesh->indices, acount(mesh->vertices));
                apush(mesh->vertices, vertex);
                mesh->min = cf_min_v3(mesh->min, vertex.position);
                mesh->max = cf_max_v3(mesh->max, vertex.position);
            }
        }
        group.count = acount(mesh->indices) - group.first;
        if (!group.count) {
            continue;
        }
        const model_part *p = part >= 0 ? &b->program->parts[part] : NULL;
        group.node = p ? p->node : 0;
        group.asset = xstrdup(p ? p->asset : "");
        group.name = xstrdup(p ? p->name : "Entity material");
        group.path = xstrdup(p ? p->path : "");
        group.material = xstrdup(
            p && p->material >= 0 ? b->program->material_slots[p->material].material : ""
        );
        group.material_scope = xstrdup(
            p && p->material >= 0 ? b->program->material_slots[p->material].name : ""
        );
        apush(mesh->groups, group);
    }
    render_mesh_asset_tangents(mesh);
}
bool model_bake(
    const model_program *program,
    const float incoming[SHADER_PARAMETER_MAX][4],
    const model_bake_options *options,
    model_bake_result *result,
    shader_error *error
)
{
    baker b = { .program = program, .options = *options, .error = error };
    float *samples = NULL;
    bool valid = options->cell_size >= 0 && isfinite(options->cell_size) &&
                 options->max_axis_cells >= 2 &&
                 options->max_axis_cells <= 256 && options->max_triangles >= 1 &&
                 options->max_triangles <= MODEL_BAKE_TRIANGLE_MAX &&
                 options->edge_iterations >= 1 &&
                 options->edge_iterations <= 24 && options->max_samples >= 1 &&
                 options->max_samples <= 100000000 && options->max_work >= 1 &&
                 options->max_work <= UINT64_C(100000000000);
    if (!valid) {
        char message[128];
        snprintf(message, sizeof message, "Invalid mesh bake budgets (axis 2..256, triangles 1..%d, edge iterations 1..24)", MODEL_BAKE_TRIANGLE_MAX);
        return fail(&b, message);
    }
    if (!model_program_prepare(program, incoming, b.result.parameters, &b.result.bounds, error) ||
        !shader_graph_validate(&program->shader, error)) {
        return false;
    }
    if (b.result.bounds.empty) {
        return fail(&b, "Cannot bake an empty model");
    }
    float longest = 0;
    for (int i = 0; i < 3; i++) {
        longest = fmaxf(longest, b.result.bounds.max[i] - b.result.bounds.min[i]);
    }
    float h = options->cell_size ? options->cell_size : longest / 48;
    if (!isfinite(h) || h <= 0) {
        return fail(&b, "Mesh bake requires positive finite bounds and cell size");
    }
    b.result.cell_size = h;
    b.result.cell_diagonal = sqrtf(3) * h;
    CF_V3 origin;
    uint64_t total = 1;
    for (int i = 0; i < 3; i++) {
        float cells = ceilf((b.result.bounds.max[i] - b.result.bounds.min[i]) / h) + 2;
        if (!isfinite(cells) || cells > options->max_axis_cells || cells < 2) {
            return fail(&b, "Mesh bake axis exceeds grid budget; increase cell size");
        }
        b.result.cells[i] = (int)cells;
        (&origin.x)[i] = b.result.bounds.min[i] - h;
        total *= (uint64_t)b.result.cells[i] + 1;
    }
    if (total > options->max_samples ||
        total > options->max_work / (uint64_t)acount(program->shader.nodes)) {
        return fail(&b, "Mesh bake grid exceeds sample/node-work budget before allocation");
    }
    samples = xmalloc((size_t)total * sizeof *samples);
    b.scratch = xmalloc((size_t)acount(program->shader.nodes) * sizeof *b.scratch);
    int nx = b.result.cells[0] + 1, ny = b.result.cells[1] + 1,
        nz = b.result.cells[2] + 1;
    for (int z = 0; valid && z < nz; z++) {
        for (int y = 0; valid && y < ny; y++) {
            for (int x = 0; valid && x < nx; x++) {
                CF_V3 p = cf_add_v3(origin, (CF_V3){ x * h, y * h, z * h });
                valid = distance(&b, p, &samples[(z * ny + y) * nx + x]);
            }
        }
    }
    static const int corners[8][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 },
                                       { 1, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 },
                                       { 0, 1, 1 }, { 1, 1, 1 } };
    static const int tetra[6][4] = { { 0, 1, 3, 7 }, { 0, 3, 2, 7 },
                                     { 0, 2, 6, 7 }, { 0, 6, 4, 7 },
                                     { 0, 4, 5, 7 }, { 0, 5, 1, 7 } };
    for (int z = 0; valid && z < nz - 1; z++) {
        for (int y = 0; valid && y < ny - 1; y++) {
            for (int x = 0; valid && x < nx - 1; x++) {
                CF_V3 points[8];
                float values[8];
                for (int i = 0; i < 8; i++) {
                    int xx = x + corners[i][0], yy = y + corners[i][1],
                        zz = z + corners[i][2];
                    points[i] = cf_add_v3(origin, (CF_V3){ xx * h, yy * h, zz * h });
                    values[i] = samples[(zz * ny + yy) * nx + xx];
                }
                for (int i = 0; valid && i < 6; i++) {
                    valid = tetrahedron(&b, points, values, tetra[i]);
                }
            }
        }
    }
    if (valid && !acount(b.triangles)) {
        valid = fail(&b, "Mesh bake found no surface; use a finer cell size (thin features may be unsampled)");
    }
    if (valid) {
        publish_mesh(&b);
        model_bake_result_free(result);
        *result = b.result;
        memset(&b.result, 0, sizeof b.result);
    }
    afree(b.triangles);
    free(b.scratch);
    free(samples);
    model_bake_result_free(&b.result);
    return valid;
}
