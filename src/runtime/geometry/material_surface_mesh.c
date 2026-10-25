#include "runtime/geometry/material_surface_mesh.h"

#include "runtime/geometry/distance_transform.h"
#include "runtime/shaders/effects.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SURFACE_CORNER_LIMIT = 3000000
};
typedef struct
{
    material_occupancy_view view;
    /* Contour and wall source: the view itself, or its recess dilation. */
    material_occupancy_view shape;
    MaterialSurface3D options;
    MaterialFrame frame;
    const material_surface_height *height;
    /* Recess only: signed samples on the view padded by its halo radius,
     * negative inside solid, distance to the nearest solid edge in air. */
    float *field;
    render_mesh_asset mesh;
    int *slots, slot_count;
    bool failed;
    bool sample_failed;
} builder;

static float sample(material_occupancy_view v, int x, int y)
{
    bool solid;
    float d;
    if (material_occupancy_sample(v, x, y, &solid, &d)) {
        return d;
    }
    int cx = x < 0 ? 0 : x >= v.width ? v.width - 1 : x;
    int cy = y < 0 ? 0 : y >= v.height ? v.height - 1 : y;
    if (material_occupancy_sample(v, x, cy, &solid, &d) ||
        material_occupancy_sample(v, cx, y, &solid, &d)) {
        return d;
    }
    material_occupancy_sample(v, cx, cy, &solid, &d);
    return d;
}
static float signed_at(builder *b, int x, int y)
{
    if (!b->field) {
        return sample(b->view, x, y);
    }
    int pad = b->view.halo_radius, stride = b->view.width + 2 * pad;
    int rows = b->view.height + 2 * pad;
    x += pad;
    y += pad;
    x = x < 0 ? 0 : x >= stride ? stride - 1 : x;
    y = y < 0 ? 0 : y >= rows ? rows - 1 : y;
    return b->field[y * stride + x];
}
static float distance_at(builder *b, CF_V2 p)
{
    float x = p.x / b->view.cell_size + b->view.width * .5f - .5f;
    float y = p.y / b->view.cell_size + b->view.height * .5f - .5f;
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float tx = x - ix, ty = y - iy;
    float a = signed_at(b, ix, iy), c = signed_at(b, ix, iy + 1);
    return (a + tx * (signed_at(b, ix + 1, iy) - a)) * (1 - ty) +
           (c + tx * (signed_at(b, ix + 1, iy + 1) - c)) * ty;
}
/* Signed distance to the outline that bounds the generated surface. */
static float outline_at(builder *b, CF_V2 p)
{
    return distance_at(b, p) - b->options.recess_width;
}
static float smooth(float t)
{
    return t * t * (3 - 2 * t);
}
static CF_V2 material_point(builder *b, CF_V2 p)
{
    float c = cosf(b->frame.radians), s = sinf(b->frame.radians);
    return (CF_V2){ b->frame.x + c * p.x - s * p.y, b->frame.y + s * p.x + c * p.y };
}
static float height_at(builder *b, CF_V2 p)
{
    if (b->failed) {
        return 0;
    }
    MaterialSurface3D o = b->options;
    CF_V2 q = material_point(b, p);
    float displacement = NAN;
    if (b->height) {
        if (!b->height->sample(b->height->context, q, &displacement) ||
            !isfinite(displacement) || fabsf(displacement) >= o.depth) {
            b->failed = b->sample_failed = true;
            return 0;
        }
    } else {
        float point[3] = { q.x / o.relief_scale,
                           q.y / o.relief_scale,
                           (float)(o.seed % 4096u) };
        displacement = o.relief * (2 * (.57f * shader_noise(point, 3, 1) +
                                        .28f * shader_noise(point, 3, 2) +
                                        .15f * shader_noise(point, 3, 4)) -
                                   1);
    }
    float d = distance_at(b, p), bevel = fminf(o.depth * .5f, o.bevel_width);
    if (d > 0 && o.recess_width > 0) {
        /* Eases from the contour lip into the cave and settles at full depth. */
        float x = 1 - fminf(1, d / o.recess_width);
        float t = o.bevel_width > 0 ? smooth(fminf(1, d / o.bevel_width)) : 1;
        float h = o.elevation - bevel - o.recess_depth * (1 - x * x) + displacement * t;
        if (h <= o.elevation - o.depth) {
            b->failed = b->sample_failed = true;
            return 0;
        }
        return h;
    }
    float t = o.bevel_width > 0 ? fminf(1, fmaxf(0, -d / o.bevel_width)) : 1;
    return o.elevation - bevel * (1 - smooth(t)) + displacement * smooth(t);
}
static render_mesh_surface_vertex vertex(builder *b, CF_V2 p)
{
    float e = b->view.cell_size * .01f;
    double dx = ((double)height_at(b, (CF_V2){ p.x + e, p.y }) -
                 height_at(b, (CF_V2){ p.x - e, p.y })) /
                (2.0 * e);
    double dz = ((double)height_at(b, (CF_V2){ p.x, p.y + e }) -
                 height_at(b, (CF_V2){ p.x, p.y - e })) /
                (2.0 * e);
    double length = sqrt(dx * dx + dz * dz + 1);
    CF_V2 q = material_point(b, p);
    float c = cosf(b->frame.radians), s = sinf(b->frame.radians);
    render_mesh_surface_vertex v = {
        .position = { p.x, height_at(b, p), p.y },
        .normal = { (float)(-dx / length), (float)(1 / length), (float)(-dz / length) },
        .uv = { q.x / b->options.relief_scale, q.y / b->options.relief_scale },
        .part_position = { q.x, height_at(b, p), q.y },
        .part_from_object = { { c, 0, -s }, { 0, 1, 0 }, { s, 0, c } }
    };
    return v;
}
static unsigned vertex_hash(const render_mesh_surface_vertex *v)
{
    unsigned hash = 2166136261u;
    const unsigned char *bytes = (const unsigned char *)v;
    for (size_t i = 0; i < sizeof *v; i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}
static int vertex_index(builder *b, render_mesh_surface_vertex v)
{
    if (2 * (acount(b->mesh.vertices) + 1) >= b->slot_count) {
        b->slot_count = b->slot_count ? b->slot_count * 2 : 256;
        free(b->slots);
        b->slots = xcalloc((size_t)b->slot_count, sizeof *b->slots);
        for (int i = 0; i < acount(b->mesh.vertices); i++) {
            unsigned slot = vertex_hash(&b->mesh.vertices[i]) &
                            (unsigned)(b->slot_count - 1);
            while (b->slots[slot]) {
                slot = (slot + 1) & (unsigned)(b->slot_count - 1);
            }
            b->slots[slot] = i + 1;
        }
    }
    unsigned slot = vertex_hash(&v) & (unsigned)(b->slot_count - 1);
    while (b->slots[slot]) {
        int index = b->slots[slot] - 1;
        if (!memcmp(&b->mesh.vertices[index], &v, sizeof v)) {
            return index;
        }
        slot = (slot + 1) & (unsigned)(b->slot_count - 1);
    }
    int index = acount(b->mesh.vertices);
    apush(b->mesh.vertices, v);
    b->slots[slot] = index + 1;
    CF_V3 p = v.position;
    if (!index) {
        b->mesh.min = b->mesh.max = p;
    }
    b->mesh.min.x = fminf(b->mesh.min.x, p.x);
    b->mesh.max.x = fmaxf(b->mesh.max.x, p.x);
    b->mesh.min.y = fminf(b->mesh.min.y, p.y);
    b->mesh.max.y = fmaxf(b->mesh.max.y, p.y);
    b->mesh.min.z = fminf(b->mesh.min.z, p.z);
    b->mesh.max.z = fmaxf(b->mesh.max.z, p.z);
    return index;
}
static void triangle(builder *b, render_mesh_surface_vertex a, render_mesh_surface_vertex c, render_mesh_surface_vertex d)
{
    if (b->failed) {
        return;
    }
    if (acount(b->mesh.indices) + 3 > SURFACE_CORNER_LIMIT) {
        b->failed = true;
        return;
    }
    render_mesh_surface_vertex v[3] = { a, c, d };
    for (int i = 0; i < 3; i++) {
        int index = vertex_index(b, v[i]);
        apush(b->mesh.indices, index);
    }
}
static float length2(CF_V2 a, CF_V2 c)
{
    float x = a.x - c.x, y = a.y - c.y;
    return x * x + y * y;
}
static void top(builder *b, CF_V2 a, CF_V2 c, CF_V2 d, int level)
{
    float lengths[3] = { length2(a, c), length2(c, d), length2(d, a) };
    int edge = lengths[1] > lengths[0] ? 1 : 0;
    if (lengths[2] > lengths[edge]) {
        edge = 2;
    }
    if (lengths[edge] <= b->options.spacing * b->options.spacing) {
        float area = (c.x - a.x) * (d.y - a.y) - (c.y - a.y) * (d.x - a.x);
        if (fabsf(area) > 1e-12f) {
            triangle(b, vertex(b, a), vertex(b, d), vertex(b, c));
        }
        return;
    }
    if (level >= 24 || b->failed) {
        b->failed = true;
        return;
    }
    CF_V2 points[3] = { a, c, d };
    a = points[edge];
    c = points[(edge + 1) % 3];
    d = points[(edge + 2) % 3];
    CF_V2 mid = { (a.x + c.x) * .5f, (a.y + c.y) * .5f };
    top(b, a, mid, d, level + 1);
    top(b, mid, c, d, level + 1);
}
static void wall(builder *b, CF_V2 a, CF_V2 c)
{
    float dx = c.x - a.x, dy = c.y - a.y, l = sqrtf(dx * dx + dy * dy);
    if (l <= 1e-6f) {
        return;
    }
    CF_V2 middle = { (a.x + c.x) * .5f, (a.y + c.y) * .5f };
    float epsilon = b->view.cell_size * .01f;
    CF_V2 outside = { middle.x + dy / l * epsilon, middle.y - dx / l * epsilon };
    float hx = b->view.width * b->view.cell_size * .5f,
          hy = b->view.height * b->view.cell_size * .5f;
    bool boundary = fabsf(middle.x) >= hx - 1e-5f || fabsf(middle.y) >= hy - 1e-5f;
    if (outline_at(b, outside) <= 0 && !boundary) {
        return;
    }
    if (boundary) {
        int x = (int)floorf(outside.x / b->view.cell_size + b->view.width * .5f);
        int y = (int)floorf(outside.y / b->view.cell_size + b->view.height * .5f);
        bool solid;
        float distance;
        if (material_occupancy_sample(b->shape, x, y, &solid, &distance) && solid) {
            return;
        }
        /* A streamed unavailable neighbor must not become an invented wall. */
        if (b->shape.halo_radius &&
            !material_occupancy_sample(b->shape, x, y, &solid, &distance)) {
            return;
        }
    }
    float *cuts = NULL;
    apush(cuts, 0);
    apush(cuts, 1);
    for (int i = 1; i < b->view.width; i++) {
        float t = (i * b->view.cell_size - hx - a.x) / dx;
        if (t > 0 && t < 1) {
            apush(cuts, t);
        }
    }
    for (int i = 1; i < b->view.height; i++) {
        float t = (i * b->view.cell_size - hy - a.y) / dy;
        if (t > 0 && t < 1) {
            apush(cuts, t);
        }
    }
    int count = acount(cuts);
    for (int i = 1; i < count; i++) {
        float t = cuts[i];
        int j = i;
        while (j && cuts[j - 1] > t) {
            cuts[j] = cuts[j - 1];
            j--;
        }
        cuts[j] = t;
    }
    for (int i = 0; i + 1 < count; i++) {
        if (cuts[i + 1] - cuts[i] < 1e-6f) {
            continue;
        }
        CF_V2 p = { a.x + dx * cuts[i], a.y + dy * cuts[i] },
              q = { a.x + dx * cuts[i + 1], a.y + dy * cuts[i + 1] };
        render_mesh_surface_vertex u = vertex(b, p), v = vertex(b, q), w = v, z = u;
        u.normal = v.normal = w.normal = z.normal = (CF_V3){ dy / l, 0, -dx / l };
        w.position.y = z.position.y = b->options.elevation - b->options.depth;
        w.part_position.y = w.position.y;
        z.part_position.y = z.position.y;
        triangle(b, u, v, w);
        triangle(b, u, w, z);
    }
    afree(cuts);
}
static void polygon_top(builder *b, material_polygon p)
{
    float cell = b->view.cell_size, hx = b->view.width * cell * .5f,
          hy = b->view.height * cell * .5f;
    float minx = p.points[0], maxx = minx, miny = p.points[1], maxy = miny;
    for (int i = 1; i < p.count; i++) {
        minx = fminf(minx, p.points[2 * i]);
        maxx = fmaxf(maxx, p.points[2 * i]);
        miny = fminf(miny, p.points[2 * i + 1]);
        maxy = fmaxf(maxy, p.points[2 * i + 1]);
    }
    int x0 = (int)floorf((minx + hx) / cell), x1 = (int)ceilf((maxx + hx) / cell);
    int y0 = (int)floorf((miny + hy) / cell), y1 = (int)ceilf((maxy + hy) / cell);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            CF_V2 a[16], c[16];
            int n = p.count;
            for (int i = 0; i < n; i++) {
                a[i] = (CF_V2){ p.points[2 * i], p.points[2 * i + 1] };
            }
            float bounds[4] = { x * cell - hx,
                                (x + 1) * cell - hx,
                                y * cell - hy,
                                (y + 1) * cell - hy };
            for (int plane = 0; plane < 4 && n; plane++) {
                int count = 0, axis = plane / 2;
                float bound = bounds[plane];
                for (int i = 0, j = n - 1; i < n; j = i++) {
                    float u = axis ? a[j].y : a[j].x, v = axis ? a[i].y : a[i].x;
                    bool in = plane % 2 ? u <= bound : u >= bound,
                         next = plane % 2 ? v <= bound : v >= bound;
                    if (in != next) {
                        float t = (bound - u) / (v - u);
                        c[count++] = (CF_V2){ a[j].x + t * (a[i].x - a[j].x),
                                              a[j].y + t * (a[i].y - a[j].y) };
                    }
                    if (next) {
                        c[count++] = a[i];
                    }
                }
                n = count;
                for (int i = 0; i < n; i++) {
                    a[i] = c[i];
                }
            }
            for (int i = 1; i + 1 < n; i++) {
                top(b, a[0], a[i], a[i + 1], 0);
            }
        }
    }
}
/* Dilates published occupancy by recess_width into an owned outline mask.
 * Distances come from cells, not authored field values, so cuts and removed
 * fragments recede by their actual surviving neighbors. A positive smoothing
 * measures them from the morphological opening instead: solid thinner than
 * twice its width still renders, but in front of the recess it leaves smooth. */
static material_mask recess_outline(builder *b)
{
    material_occupancy_view v = b->view;
    int pad = v.halo_radius, stride = v.width + 2 * pad, rows = v.height + 2 * pad;
    size_t count = (size_t)stride * rows;
    double *squared = xmalloc(count * sizeof *squared);
    unsigned char *known = xmalloc(count);
    float *original = xmalloc(count * sizeof *original);
    const double far = DISTANCE_TRANSFORM_FAR;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < stride; x++) {
            int i = y * stride + x;
            bool solid = false;
            known[i] = material_occupancy_sample(v, x - pad, y - pad, &solid, &original[i]);
            known[i] |= (unsigned char)(known[i] && solid) << 1;
            squared[i] = known[i] & 2 ? 0 : far;
        }
    }
    float cell = v.cell_size, width = b->options.recess_width;
    float smoothing = b->options.recess_smoothing;
    if (smoothing > 0) {
        /* Erode by distance to air, then measure from the surviving core. */
        for (size_t i = 0; i < count; i++) {
            squared[i] = known[i] & 2 ? far : 0;
        }
        distance_transform(squared, stride, rows);
        double core = smoothing / cell + .5;
        for (size_t i = 0; i < count; i++) {
            squared[i] = known[i] & 2 && squared[i] > core * core ? 0 : far;
        }
    }
    distance_transform(squared, stride, rows);
    b->field = xmalloc(count * sizeof *b->field);
    for (size_t i = 0; i < count; i++) {
        b->field[i] = known[i] & 2
                        ? fminf(original[i], -.0001f)
                        : fmaxf((float)sqrt(squared[i]) * cell - smoothing - cell * .5f, cell * .5f);
    }
    float *inner = xmalloc((size_t)v.width * v.height * sizeof *inner);
    for (int y = 0; y < v.height; y++) {
        for (int x = 0; x < v.width; x++) {
            inner[y * v.width + x] = b->field[(y + pad) * stride + x + pad] - width;
        }
    }
    material_mask outline = material_mask_from_field(v.width, v.height, cell, inner);
    if (pad) {
        unsigned char *cells = xmalloc(count);
        for (size_t i = 0; i < count; i++) {
            original[i] = b->field[i] - width;
            cells[i] = known[i] ? original[i] <= 0 : MATERIAL_HALO_UNAVAILABLE;
        }
        material_mask_set_halo(&outline, (material_mask_halo){ pad, v.lod_limit, cells, original });
        free(cells);
    }
    free(inner);
    free(original);
    free(known);
    free(squared);
    return outline;
}
bool material_surface_mesh_build_height(
    material_occupancy_view view,
    MaterialSurface3D options,
    MaterialFrame frame,
    const material_surface_height *height,
    render_mesh_asset *output,
    char *error,
    size_t size
)
{
    float values[] = { options.elevation,
                       options.depth,
                       options.bevel_width,
                       options.relief,
                       options.relief_scale,
                       options.spacing,
                       options.recess_width,
                       options.recess_depth,
                       options.recess_smoothing,
                       frame.x,
                       frame.y,
                       frame.radians };
    bool valid = view.width > 0 && view.height > 0 && view.width <= 65536 &&
                 view.height <= 65536 &&
                 (double)view.width * view.height <= 1048576 && view.cells &&
                 view.distance && isfinite(view.cell_size) && view.cell_size > 0;
    for (int i = 0; i < 12; i++) {
        valid = valid && isfinite(values[i]) &&
                fabsf(values[i]) <= sqrtf(FLT_MAX) / 16;
    }
    double extent = fmax(view.width, view.height) * (double)view.cell_size;
    valid = valid && extent <= sqrtf(FLT_MAX) / 16 &&
            view.cell_size * .01f > 0 && options.relief_scale > 0 &&
            (extent + fabs(frame.x) + fabs(frame.y)) / options.relief_scale <
                FLT_MAX / 16;
    valid = valid && options.depth > 0 && options.bevel_width >= 0 &&
            options.relief >= 0 && options.relief < options.depth &&
            options.relief_scale > 0 && options.spacing >= view.cell_size;
    bool recess = options.recess_width > 0;
    float bevel = fminf(options.depth * .5f, options.bevel_width);
    valid = valid && options.recess_width >= 0 && options.recess_depth >= 0 &&
            options.recess_smoothing >= 0 &&
            (!recess ||
             (view.width <= 256 && view.height <= 256 &&
              bevel + options.recess_depth + options.relief < options.depth));
    if (!valid || (height && !height->sample)) {
        snprintf(error, size, "MaterialSurface3D requires finite positive depth/scale/spacing, relief and recess below depth on a bounded mask");
        return false;
    }
    int reach = recess
                  ? (int)ceilf(
                        (options.recess_width + options.recess_smoothing) / view.cell_size
                    ) + 2
                  : 0;
    if (recess && view.halo_radius && view.halo_radius < reach) {
        snprintf(error, size, "MaterialSurface3D recess needs a %d-cell neighbor halo; mask has %d", reach, view.halo_radius);
        return false;
    }
    builder b = { .view = view, .options = options, .frame = frame, .height = height };
    /* Reuse the exact published contour algorithm without owning its inputs.
     * Only the rebuilt temporary rectangle/polygon arrays are released here. */
    material_mask contour = { .width = view.width,
                              .height = view.height,
                              .cell_size = view.cell_size,
                              .cells = (unsigned char *)view.cells,
                              .distance = (float *)view.distance,
                              .halo_cells = (unsigned char *)view.halo_cells,
                              .halo_distance = (float *)view.halo_distance,
                              .halo_radius = view.halo_radius };
    if (recess) {
        contour = recess_outline(&b);
    } else {
        material_mask_rebuild(&contour);
    }
    b.shape = recess ? material_mask_occupancy(&contour) : view;
    for (int i = 0; i < acount(contour.surface) && !b.failed; i++) {
        material_polygon p = contour.surface[i];
        polygon_top(&b, p);
        for (int j = 0; j < p.count; j++) {
            int k = (j + 1) % p.count;
            wall(
                &b,
                (CF_V2){ p.points[2 * j], p.points[2 * j + 1] },
                (CF_V2){ p.points[2 * k], p.points[2 * k + 1] }
            );
        }
    }
    if (recess) {
        material_mask_free(&contour);
    } else {
        afree(contour.rects);
        afree(contour.surface);
    }
    free(b.field);
    free(b.slots);
    if (b.failed) {
        render_mesh_asset_free(&b.mesh);
        snprintf(error, size, "%s", b.sample_failed ? "MaterialSurface3D height sampler failed or returned nonfinite/out-of-depth displacement" : "MaterialSurface3D geometry exceeds bounded tessellation budget");
        return false;
    }
    render_mesh_asset_tangents(&b.mesh);
    b.mesh.has_part_coordinates = true;
    *output = b.mesh;
    return true;
}

bool material_surface_mesh_build(
    material_occupancy_view view,
    MaterialSurface3D options,
    MaterialFrame frame,
    render_mesh_asset *output,
    char *error,
    size_t size
)
{
    return material_surface_mesh_build_height(view, options, frame, NULL, output, error, size);
}
