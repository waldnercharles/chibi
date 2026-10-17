#include "runtime/geometry/terrain_volume.h"

#include "runtime/geometry/distance_transform.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Coarse cells whose corners all lie farther from zero than this bound times
 * the distance from a cell's center to its corner plus two fine diagonals
 * keep their coarse blend: an authored field changing no faster than this per
 * unit has no surface there, and every sample a surface vertex reads is fine. */
#define LIPSCHITZ 1.5f
/* Bricks baked per point batch, bounding the position scratch. */
#define BAKE_BRICKS 2048
/* A surface reads samples up to two away: its cell's corners and their
 * gradients. Under the slope bound above, a sample this many fine samples
 * below zero has no air that near, so removed distances stop here and a
 * cut's influence stays local. */
#define CARVE_FLOOR 6
#define SURFACE_READ 2

static int coarse_count(int fine, int refine)
{
    return (fine - 1) / refine + 1;
}

/* Exact at both ends, so coarse corners keep their values. */
static float mix(float a, float b, float t)
{
    return a * (1 - t) + b * t;
}

static int block(const terrain_volume *v)
{
    return v->refine * v->refine * v->refine;
}

/* The coarse cell holding fine sample index i along an axis of cells coarse
 * cells; the last one also holds the closing sample. */
static int brick_of(int i, int refine, int cells)
{
    int brick = i / refine;
    return brick < cells ? brick : cells - 1;
}

/* Every coarse corner of brick (bi, bj, bk): their minimum and maximum. */
static void corner_bounds(const terrain_volume *v, int bi, int bj, int bk, float *low, float *high)
{
    int cw = coarse_count(v->plane.width, v->refine),
        ch = coarse_count(v->plane.height, v->refine);
    const float *c = v->coarse + ((size_t)bk * ch + bj) * cw + bi;
    size_t dy = (size_t)cw, dz = (size_t)cw * ch;
    size_t corners[8] = { 0, 1, dy, dy + 1, dz, dz + 1, dz + dy, dz + dy + 1 };
    *low = *high = c[0];
    for (int i = 1; i < 8; i++) {
        *low = fminf(*low, c[corners[i]]);
        *high = fmaxf(*high, c[corners[i]]);
    }
}

/* Uncarved density at fine sample (i, j, k). */
static float base_sample(const terrain_volume *v, int i, int j, int k)
{
    int r = v->refine;
    int cw = coarse_count(v->plane.width, r), ch = coarse_count(v->plane.height, r);
    if (r == 1) {
        return v->coarse[((size_t)k * ch + j) * cw + i];
    }
    int bi = brick_of(i, r, cw - 1), bj = brick_of(j, r, ch - 1),
        bk = brick_of(k, r, coarse_count(v->layers, r) - 1);
    int li = i - bi * r, lj = j - bj * r, lk = k - bk * r;
    /* A brick stores the samples it owns; closing samples always blend. */
    int brick = li < r && lj < r && lk < r
                  ? v->bricks[((size_t)bk * (ch - 1) + bj) * (cw - 1) + bi]
                  : -1;
    if (brick >= 0) {
        return v->detail[(size_t)brick * block(v) + ((size_t)lk * r + lj) * r + li];
    }
    const float *c = v->coarse + ((size_t)bk * ch + bj) * cw + bi;
    size_t dy = (size_t)cw, dz = (size_t)cw * ch;
    float fx = (float)li / r, fy = (float)lj / r, fz = (float)lk / r;
    float low = mix(mix(c[0], c[1], fx), mix(c[dy], c[dy + 1], fx), fy);
    float high = mix(mix(c[dz], c[dz + 1], fx), mix(c[dz + dy], c[dz + dy + 1], fx), fy);
    return mix(low, high, fz);
}

/* Bakes the fine samples of every brick near a surface. */
static bool bake_detail(
    terrain_volume *v,
    field_program_baker *baker,
    const task_system *tasks,
    char *error,
    size_t size
)
{
    int r = v->refine, count = block(v);
    int bw = coarse_count(v->plane.width, r) - 1,
        bh = coarse_count(v->plane.height, r) - 1,
        bd = coarse_count(v->layers, r) - 1;
    float cell = v->plane.cell_size;
    float margin = LIPSCHITZ * sqrtf(3) * (r * .5f + 2) * cell;
    size_t bricks = (size_t)bw * bh * bd;
    v->bricks = xmalloc(bricks * sizeof *v->bricks);
    int baked = 0;
    for (size_t b = 0; b < bricks; b++) {
        float low, high;
        corner_bounds(v, (int)(b % bw), (int)(b / bw % bh), (int)(b / bw / bh), &low, &high);
        v->bricks[b] = low < margin && high > -margin ? baked++ : -1;
    }
    size_t coarse = (size_t)(bw + 1) * (bh + 1) * (bd + 1);
    if ((double)baked * count + coarse > TERRAIN_VOLUME_SAMPLE_LIMIT) {
        snprintf(
            error,
            size,
            "Terrain volume needs %.0f samples near its surfaces; at most %d "
            "fit",
            (double)baked * count + coarse,
            TERRAIN_VOLUME_SAMPLE_LIMIT
        );
        return false;
    }
    v->detail = xmalloc((size_t)baked * count * sizeof *v->detail);
    v->detail_count = baked;
    size_t batch = (size_t)BAKE_BRICKS * count;
    float *x = xmalloc(batch * sizeof *x), *y = xmalloc(batch * sizeof *y),
          *z = xmalloc(batch * sizeof *z);
    const field_grid *p = &v->plane;
    int first = 0, filled = 0;
    bool ok = true;
    for (size_t b = 0; b < bricks && ok; b++) {
        if (v->bricks[b] >= 0) {
            int bi = (int)(b % bw), bj = (int)(b / bw % bh), bk = (int)(b / bw / bh);
            float *px = x + (size_t)filled * count,
                  *py = y + (size_t)filled * count, *pz = z + (size_t)filled * count;
            for (int n = 0; n < count; n++) {
                int i = bi * r + n % r, j = bj * r + n / r % r,
                    k = bk * r + n / r / r;
                px[n] = p->center_x + (i + .5f - p->width * .5f) * cell;
                py[n] = p->center_y + (j + .5f - p->height * .5f) * cell;
                pz[n] = (k - v->front) * cell;
            }
            filled++;
        }
        if (filled == BAKE_BRICKS || (b + 1 == bricks && filled)) {
            ok = field_program_baker_bake_points(
                baker,
                x,
                y,
                z,
                filled * count,
                v->detail + (size_t)first * count,
                tasks,
                error,
                size
            );
            first += filled;
            filled = 0;
        }
    }
    free(z);
    free(y);
    free(x);
    return ok;
}

bool terrain_volume_bake(
    const field_program *program,
    field_context context,
    terrain_volume_shape shape,
    const task_system *tasks,
    terrain_volume *out,
    char *error,
    size_t size
)
{
    field_grid plane = shape.plane;
    int front = shape.front, layers = shape.layers, refine = shape.refine;
    if (plane.width < 2 || plane.height < 2 || front < 0 ||
        layers - front < 2 || refine < 1 || refine > TERRAIN_VOLUME_REFINE_MAX ||
        (double)(plane.width - 1) * refine >= 4096 ||
        (double)(plane.height - 1) * refine >= 4096 ||
        (double)(layers - 1) * refine >= 4096 ||
        (double)plane.width * plane.height * layers > TERRAIN_VOLUME_SAMPLE_LIMIT) {
        snprintf(error, size, "Terrain volume needs 2 to %d samples per axis, two behind the plane and at most %d in total", 4096, TERRAIN_VOLUME_SAMPLE_LIMIT);
        return false;
    }
    if (!(isfinite(shape.slab) && shape.slab > 0)) {
        snprintf(error, size, "Terrain volume needs a positive slab");
        return false;
    }
    field_program_baker *
        baker = field_program_baker_new(program, context, tasks ? tasks->workers : 1, error, size);
    if (!baker) {
        return false;
    }
    size_t slice = (size_t)plane.width * plane.height;
    float *coarse = xmalloc(slice * layers * sizeof *coarse);
    for (int k = 0; k < layers; k++) {
        plane.depth = (k - front) * plane.cell_size;
        if (!field_program_baker_bake_rows(baker, plane, 0, plane.height, coarse + slice * k, tasks, error, size)) {
            field_program_baker_free(baker);
            free(coarse);
            return false;
        }
    }
    float cell = plane.cell_size / refine;
    float *back = coarse + slice * (layers - 1);
    for (size_t i = 0; i < slice && !shape.open_back; i++) {
        back[i] = fminf(back[i], -cell * .5f);
    }
    field_grid fine = { (plane.width - 1) * refine + 1,
                        (plane.height - 1) * refine + 1,
                        cell,
                        plane.center_x,
                        plane.center_y,
                        0 };
    terrain_volume v = { .plane = fine,
                         .front = front * refine,
                         .layers = (layers - 1) * refine + 1,
                         .refine = refine,
                         .open_back = shape.open_back,
                         .slab = shape.slab,
                         .coarse = coarse };
    bool ok = refine == 1 || bake_detail(&v, baker, tasks, error, size);
    field_program_baker_free(baker);
    if (!ok) {
        terrain_volume_free(&v);
        return false;
    }
    *out = v;
    return true;
}

void terrain_volume_free(terrain_volume *volume)
{
    free(volume->detail);
    free(volume->bricks);
    free(volume->coarse);
    *volume = (terrain_volume){ 0 };
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

void terrain_volume_removed(
    const terrain_volume *v,
    const unsigned char *open,
    int x0,
    int y0,
    int x1,
    int y1,
    float *removed
)
{
    assert(0 <= x0 && x0 < x1 && x1 <= v->plane.width);
    assert(0 <= y0 && y0 < y1 && y1 <= v->plane.height);
    int width = v->plane.width, height = v->plane.height;
    float cell = v->plane.cell_size;
    /* Values past the clamps never matter, so a change reaches only samples
     * within this many of it, and each of those reads only as far again. */
    int reach = (int)ceilf(fmaxf(v->slab / cell, CARVE_FLOOR) + .5f) + 1;
    int wx0 = max_int(x0 - reach, 0), wy0 = max_int(y0 - reach, 0),
        wx1 = min_int(x1 + reach, width), wy1 = min_int(y1 + reach, height);
    int rx0 = max_int(wx0 - reach, 0), ry0 = max_int(wy0 - reach, 0),
        rx1 = min_int(wx1 + reach, width), ry1 = min_int(wy1 + reach, height);
    int w = rx1 - rx0, h = ry1 - ry0;
    double *inside = xmalloc((size_t)w * h * sizeof *inside);
    double *outside = xmalloc((size_t)w * h * sizeof *outside);
    for (int y = ry0; y < ry1; y++) {
        for (int x = rx0; x < rx1; x++) {
            size_t n = (size_t)(y - ry0) * w + x - rx0;
            bool cut = open[(size_t)y * width + x] &&
                       base_sample(v, x, y, v->front) < 0;
            inside[n] = cut ? DISTANCE_TRANSFORM_FAR : 0;
            outside[n] = cut ? 0 : DISTANCE_TRANSFORM_FAR;
        }
    }
    distance_transform(inside, w, h);
    distance_transform(outside, w, h);
    for (int y = wy0; y < wy1; y++) {
        for (int x = wx0; x < wx1; x++) {
            size_t n = (size_t)(y - ry0) * w + x - rx0;
            float value = inside[n] > 0 ? ((float)sqrt(inside[n]) - .5f) * cell
                                        : (.5f - (float)sqrt(outside[n])) * cell;
            removed[(size_t)y * width + x] = fminf(v->slab, fmaxf(-CARVE_FLOOR * cell, value));
        }
    }
    free(outside);
    free(inside);
}

int terrain_volume_reach(const terrain_volume *v)
{
    return (int)ceilf(v->slab / v->plane.cell_size) + CARVE_FLOOR + SURFACE_READ;
}

bool terrain_volume_changed_layers(
    const terrain_volume *v,
    const float *before,
    const float *after,
    int x0,
    int y0,
    int x1,
    int y1,
    int *k0,
    int *k1
)
{
    int width = v->plane.width;
    x0 = max_int(x0 - SURFACE_READ, 0);
    y0 = max_int(y0 - SURFACE_READ, 0);
    x1 = min_int(x1 + SURFACE_READ, width);
    y1 = min_int(y1 + SURFACE_READ, v->plane.height);
    float deepest = -INFINITY;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            size_t i = (size_t)y * width + x;
            if (before[i] != after[i]) {
                deepest = fmaxf(deepest, fmaxf(before[i], after[i]));
            }
        }
    }
    if (deepest == -INFINITY) {
        return false;
    }
    /* The carve opens samples to depth deepest and reshapes those it brings
     * within reach of a surface. */
    int layers = (int)ceilf(deepest / v->plane.cell_size) + CARVE_FLOOR + SURFACE_READ;
    *k0 = max_int(v->front - layers, 0);
    *k1 = min_int(v->front + layers + 1, v->layers);
    return true;
}

/* Carved density, symmetric about the plane; a closing back layer is never
 * carved. */
static float sample(const terrain_volume *v, const float *removed, int i, int j, int k)
{
    float base = base_sample(v, i, j, k);
    if (k == v->layers - 1 && !v->open_back) {
        return base;
    }
    float z = fabsf((float)(k - v->front)) * v->plane.cell_size;
    return fmaxf(base, removed[(size_t)j * v->plane.width + i] - z);
}

float terrain_volume_density(const terrain_volume *v, const float *removed, int i, int j, int k)
{
    assert(
        0 <= i && i < v->plane.width && 0 <= j && j < v->plane.height &&
        0 <= k && k < v->layers
    );
    return sample(v, removed, i, j, k);
}

typedef enum
{
    BRICK_AIR,
    BRICK_SOLID,
    BRICK_MIXED
} brick_state;

/* Whether every carved sample of brick (bi, bj, bk), its faces included, is
 * air or solid, from bounds that hold for the stored density: unbaked bricks
 * blend their corners, and the carve cone is bounded by the plane samples'
 * removed range and the layers' depth range. MIXED when the bounds cannot
 * tell. */
static brick_state brick_bound(const terrain_volume *v, const float *removed, int bi, int bj, int bk)
{
    int r = v->refine, width = v->plane.width;
    float cut_low = INFINITY, cut_high = -INFINITY;
    for (int j = bj * r; j <= (bj + 1) * r; j++) {
        for (int i = bi * r; i <= (bi + 1) * r; i++) {
            cut_low = fminf(cut_low, removed[(size_t)j * width + i]);
            cut_high = fmaxf(cut_high, removed[(size_t)j * width + i]);
        }
    }
    int k0 = bk * r - v->front, k1 = (bk + 1) * r - v->front;
    float cell = v->plane.cell_size;
    float near = k0 <= 0 && k1 >= 0
                   ? 0
                   : fminf(fabsf((float)k0), fabsf((float)k1)) * cell;
    float far = fmaxf(fabsf((float)k0), fabsf((float)k1)) * cell;
    bool back = (bk + 1) * r == v->layers - 1 && !v->open_back;
    if (!back && cut_low - far >= 0) {
        return BRICK_AIR;
    }
    /* Samples on the far faces belong to the next bricks: the corner blend
     * bounds them only while those store no samples either. */
    int bw = coarse_count(v->plane.width, r) - 1,
        bh = coarse_count(v->plane.height, r) - 1,
        bd = coarse_count(v->layers, r) - 1;
    for (int n = 0; n < 8 && r > 1; n++) {
        int i = bi + (n & 1), j = bj + (n >> 1 & 1), k = bk + (n >> 2);
        if (i < bw && j < bh && k < bd &&
            v->bricks[((size_t)k * bh + j) * bw + i] >= 0) {
            return BRICK_MIXED;
        }
    }
    float low, high;
    corner_bounds(v, bi, bj, bk, &low, &high);
    if (high < 0 && cut_high - near < 0) {
        return BRICK_SOLID;
    }
    return !back && low >= 0 ? BRICK_AIR : BRICK_MIXED;
}

typedef struct
{
    const terrain_volume *volume;
    const float *removed;
    /* Cells with vertices: [cx0, cx1) x [cy0, cy1) x [cz0, cz1). */
    int cx0, cy0, cx1, cy1, cz0, cz1;
    int *slots;
    float elevation;
    CF_V2 origin;
    render_mesh_asset *mesh;
} mesher;

/* Central-difference gradient of the carved density at a sample, one-sided
 * at the grid's faces. Units are density per sample spacing. */
static void sample_gradient(const terrain_volume *v, const float *removed, int i, int j, int k, float g[3])
{
    int at[3] = { i, j, k };
    int limit[3] = { v->plane.width - 1, v->plane.height - 1, v->layers - 1 };
    for (int axis = 0; axis < 3; axis++) {
        int low[3] = { i, j, k }, high[3] = { i, j, k };
        low[axis] = at[axis] > 0 ? at[axis] - 1 : at[axis];
        high[axis] = at[axis] < limit[axis] ? at[axis] + 1 : at[axis];
        g[axis] = (sample(v, removed, high[0], high[1], high[2]) -
                   sample(v, removed, low[0], low[1], low[2])) /
                  (float)(high[axis] - low[axis]);
    }
}

/* Corner gradients blended trilinearly at a point inside the cell. */
static void cell_gradient(const float corners[8][3], const float f[3], float g[3])
{
    for (int axis = 0; axis < 3; axis++) {
        float x00 = mix(corners[0][axis], corners[1][axis], f[0]);
        float x10 = mix(corners[2][axis], corners[3][axis], f[0]);
        float x01 = mix(corners[4][axis], corners[5][axis], f[0]);
        float x11 = mix(corners[6][axis], corners[7][axis], f[0]);
        g[axis] = mix(mix(x00, x10, f[1]), mix(x01, x11, f[1]), f[2]);
    }
}

/* Dual contouring: the point minimizing squared distances to the tangent
 * planes at the cell's edge crossings, pulled gently toward their mass point
 * so flat or degenerate cells stay centered, then kept inside the cell. Flat
 * faces and sharp edges survive instead of rounding [Ju et al. 2002]. */
static void cell_point(int count, const float points[12][3], const float normals[12][3], float f[3])
{
    float mass[3] = { 0 };
    for (int c = 0; c < count; c++) {
        for (int n = 0; n < 3; n++) {
            mass[n] += points[c][n] / count;
        }
    }
    const double pull = .05;
    double a[3][3] = { { pull, 0, 0 }, { 0, pull, 0 }, { 0, 0, pull } }, b[3] = { 0 };
    for (int c = 0; c < count; c++) {
        double length = sqrt(
            (double)normals[c][0] * normals[c][0] +
            (double)normals[c][1] * normals[c][1] +
            (double)normals[c][2] * normals[c][2]
        );
        if (length <= 0) {
            continue;
        }
        double n[3] = { normals[c][0] / length,
                        normals[c][1] / length,
                        normals[c][2] / length };
        double offset = n[0] * (points[c][0] - mass[0]) +
                        n[1] * (points[c][1] - mass[1]) +
                        n[2] * (points[c][2] - mass[2]);
        for (int r = 0; r < 3; r++) {
            for (int s = 0; s < 3; s++) {
                a[r][s] += n[r] * n[s];
            }
            b[r] += n[r] * offset;
        }
    }
    double co[3][3];
    for (int r = 0; r < 3; r++) {
        for (int s = 0; s < 3; s++) {
            int r1 = (r + 1) % 3, r2 = (r + 2) % 3, s1 = (s + 1) % 3,
                s2 = (s + 2) % 3;
            co[s][r] = a[r1][s1] * a[r2][s2] - a[r1][s2] * a[r2][s1];
        }
    }
    double det = a[0][0] * co[0][0] + a[0][1] * co[1][0] + a[0][2] * co[2][0];
    for (int r = 0; r < 3; r++) {
        double x = (co[r][0] * b[0] + co[r][1] * b[1] + co[r][2] * b[2]) / det;
        f[r] = fminf(1, fmaxf(0, mass[r] + (float)x));
    }
}

/* The dual contouring vertex of a cell, with the density gradient there as
 * its shading normal. */
static int cell_vertex(mesher *m, int i, int j, int k)
{
    int *slot = &m->slots[((size_t)(k - m->cz0) * (m->cy1 - m->cy0) + j - m->cy0) * (m->cx1 - m->cx0) + i - m->cx0];
    if (*slot >= 0) {
        return *slot;
    }
    float d[8], corners[8][3];
    for (int c = 0; c < 8; c++) {
        int ci = i + (c & 1), cj = j + (c >> 1 & 1), ck = k + (c >> 2);
        d[c] = sample(m->volume, m->removed, ci, cj, ck);
        sample_gradient(m->volume, m->removed, ci, cj, ck, corners[c]);
    }
    float points[12][3], normals[12][3];
    int crossings = 0;
    for (int a = 0; a < 8; a++) {
        for (int axis = 0; axis < 3; axis++) {
            int b = a | 1 << axis;
            if (b == a || (d[a] < 0) == (d[b] < 0)) {
                continue;
            }
            float t = d[a] / (d[a] - d[b]);
            for (int n = 0; n < 3; n++) {
                points[crossings][n] = n == axis ? t : (float)(a >> n & 1);
            }
            cell_gradient(corners, points[crossings], normals[crossings]);
            crossings++;
        }
    }
    assert(crossings > 0);
    float f[3], g[3];
    cell_point(crossings, points, normals, f);
    cell_gradient(corners, f, g);
    const field_grid *p = &m->volume->plane;
    float cell = p->cell_size;
    float x = p->center_x + (i + f[0] + .5f - p->width * .5f) * cell;
    float y = p->center_y + (j + f[1] + .5f - p->height * .5f) * cell;
    float z = (k + f[2] - m->volume->front) * cell;
    /* Grid (x, y, depth) maps to mesh (X, Z, -Y): a proper rotation. */
    CF_V3 normal = { g[0], -g[2], g[1] };
    float length = sqrtf(cf_dot_v3(normal, normal));
    normal = length > 0 ? cf_mul_v3_f(normal, 1 / length) : (CF_V3){ 0, 1, 0 };
    render_mesh_surface_vertex v = {
        .position = { x, m->elevation - z, y },
        .normal = normal,
        .uv = { x / 64, y / 64 },
        .part_position = { m->origin.x + x, m->elevation - z, m->origin.y + y },
        .part_from_object = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
    };
    *slot = acount(m->mesh->vertices);
    if (!*slot) {
        m->mesh->min = m->mesh->max = v.position;
    }
    m->mesh->min = cf_min_v3(m->mesh->min, v.position);
    m->mesh->max = cf_max_v3(m->mesh->max, v.position);
    apush(m->mesh->vertices, v);
    return *slot;
}

/* A quad around the sample edge from (i, j, k) along axis, joining the four
 * cells that share it in counterclockwise order about +axis. */
static void edge_quad(mesher *m, int i, int j, int k, int axis)
{
    int at[3] = { i, j, k };
    int low[3] = { 0, 0, m->cz0 };
    int high[3] = { m->volume->plane.width - 1, m->volume->plane.height - 1, m->cz1 };
    int u = (axis + 1) % 3, w = (axis + 2) % 3;
    if (at[axis] < low[axis] || at[axis] >= high[axis] || at[u] - 1 < low[u] ||
        at[u] >= high[u] || at[w] - 1 < low[w] || at[w] >= high[w]) {
        return;
    }
    int next[3] = { i, j, k };
    next[axis]++;
    bool inside = sample(m->volume, m->removed, i, j, k) < 0;
    if (inside == (sample(m->volume, m->removed, next[0], next[1], next[2]) < 0)) {
        return;
    }
    int corners[4];
    static const int around[4][2] = { { -1, -1 }, { 0, -1 }, { 0, 0 }, { -1, 0 } };
    for (int c = 0; c < 4; c++) {
        int cell[3] = { i, j, k };
        cell[u] += around[c][0];
        cell[w] += around[c][1];
        corners[c] = cell_vertex(m, cell[0], cell[1], cell[2]);
    }
    /* Faces look into the air: along +axis when solid leads into air. */
    int order[2][4] = { { 0, 1, 2, 3 }, { 0, 3, 2, 1 } };
    const int *o = order[!inside];
    int triangles[6] = { corners[o[0]], corners[o[1]], corners[o[2]],
                         corners[o[0]], corners[o[2]], corners[o[3]] };
    for (int t = 0; t < 6; t++) {
        apush(m->mesh->indices, triangles[t]);
    }
}

bool terrain_volume_changed_lattice(
    const terrain_volume *v,
    const float *before,
    const float *after,
    terrain_volume_bricks *points
)
{
    int width = v->plane.width, height = v->plane.height, r = v->refine;
    int x0 = width, y0 = height, x1 = 0, y1 = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t i = (size_t)y * width + x;
            if (before[i] != after[i]) {
                x0 = min_int(x0, x);
                y0 = min_int(y0, y);
                x1 = max_int(x1, x + 1);
                y1 = y + 1;
            }
        }
    }
    int k0, k1;
    if (x0 >= x1 ||
        !terrain_volume_changed_layers(v, before, after, x0, y0, x1, y1, &k0, &k1)) {
        return false;
    }
    /* A coarse point's density reads only its own column of the carve. */
    *points = (terrain_volume_bricks){
        .x0 = (x0 + r - 1) / r,
        .y0 = (y0 + r - 1) / r,
        .z0 = k0 / r,
        .x1 = (x1 - 1) / r + 1,
        .y1 = (y1 - 1) / r + 1,
        .z1 = min_int((k1 - 1) / r + 1, (v->layers - 1) / r + 1)
    };
    return points->x0 < points->x1 && points->y0 < points->y1;
}

/* One past the last fine sample brick b owns along an axis of cells bricks. */
static int owned_end(int b, int refine, int cells)
{
    return (b + 1) * refine + (b == cells - 1);
}

void terrain_volume_mesh(
    const terrain_volume *v,
    const float *removed,
    int x0,
    int y0,
    int x1,
    int y1,
    int z0,
    int z1,
    float elevation,
    CF_V2 origin,
    render_mesh_asset *out
)
{
    assert(0 <= x0 && x0 < x1 && x1 <= v->plane.width);
    assert(0 <= y0 && y0 < y1 && y1 <= v->plane.height);
    assert(0 <= z0 && z0 < z1 && z1 <= v->layers);
    bool behind = z0 >= v->front;
    assert(behind || z1 <= v->front);
    /* This side's cells: from the plane to the deepest layer behind it, or
     * from the frontmost layer to the plane. */
    int low = behind ? v->front : 0, high = behind ? v->layers - 1 : v->front;
    mesher m = { .volume = v,
                 .removed = removed,
                 .cx0 = x0 > 0 ? x0 - 1 : 0,
                 .cy0 = y0 > 0 ? y0 - 1 : 0,
                 .cx1 = x1 < v->plane.width ? x1 : v->plane.width - 1,
                 .cy1 = y1 < v->plane.height ? y1 : v->plane.height - 1,
                 .cz0 = max_int(z0 - 1, low),
                 .cz1 = min_int(z1, high),
                 .elevation = elevation,
                 .origin = origin,
                 .mesh = out };
    *out = (render_mesh_asset){ 0 };
    if (m.cz1 <= m.cz0) {
        return;
    }
    size_t slots = (size_t)(m.cx1 - m.cx0) * (m.cy1 - m.cy0) * (m.cz1 - m.cz0);
    m.slots = xmalloc(slots * sizeof *m.slots);
    memset(m.slots, 0xff, slots * sizeof *m.slots);
    int r = v->refine;
    int bw = coarse_count(v->plane.width, r) - 1,
        bh = coarse_count(v->plane.height, r) - 1,
        bd = coarse_count(v->layers, r) - 1;
    /* Each brick owns the edges leaving its samples; uniform bricks own none
     * that cross the surface. */
    for (int bk = brick_of(z0, r, bd); bk <= brick_of(z1 - 1, r, bd); bk++) {
        int k0 = max_int(bk * r, z0), k1 = min_int(owned_end(bk, r, bd), z1);
        for (int bj = brick_of(y0, r, bh); bj <= brick_of(y1 - 1, r, bh); bj++) {
            int j0 = max_int(bj * r, y0), j1 = min_int(owned_end(bj, r, bh), y1);
            for (int bi = brick_of(x0, r, bw); bi <= brick_of(x1 - 1, r, bw); bi++) {
                int i0 = max_int(bi * r, x0), i1 = min_int(owned_end(bi, r, bw), x1);
                if (brick_bound(v, removed, bi, bj, bk) != BRICK_MIXED) {
                    continue;
                }
                for (int k = k0; k < k1; k++) {
                    for (int j = j0; j < j1; j++) {
                        for (int i = i0; i < i1; i++) {
                            for (int axis = 0; axis < 3; axis++) {
                                edge_quad(&m, i, j, k, axis);
                            }
                        }
                    }
                }
            }
        }
    }
    free(m.slots);
    render_mesh_asset_tangents(out);
    out->has_part_coordinates = true;
}

/* Every carved sample of brick (bi, bj, bk), faces included, is solid. */
static bool solid_brick(const terrain_volume *v, const float *removed, int bi, int bj, int bk)
{
    brick_state state = brick_bound(v, removed, bi, bj, bk);
    if (state != BRICK_MIXED) {
        return state == BRICK_SOLID;
    }
    int r = v->refine;
    for (int k = bk * r; k <= (bk + 1) * r; k++) {
        for (int j = bj * r; j <= (bj + 1) * r; j++) {
            for (int i = bi * r; i <= (bi + 1) * r; i++) {
                if (sample(v, removed, i, j, k) >= 0) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* Groups of span bricks along an axis of count bricks; the last may be
 * narrower. */
static int group_count(int count, int span)
{
    return (count + span - 1) / span;
}

/* Every brick of group g, in bricks from the range's low corner, is solid. */
static bool solid_group(const unsigned char *solid, int cw, int ch, terrain_volume_bricks g)
{
    for (int k = g.z0; k < g.z1; k++) {
        for (int j = g.y0; j < g.y1; j++) {
            for (int i = g.x0; i < g.x1; i++) {
                if (!solid[((size_t)k * ch + j) * cw + i]) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* Boxes covering every group of span bricks per side, clamped at the range's
 * far faces, whose bricks are all solid, merged across the plane and then
 * through depth. */
static void merge_boxes(
    const terrain_volume *v,
    const unsigned char *solid_bricks,
    terrain_volume_bricks range,
    int span,
    float elevation,
    terrain_volume_box **boxes
)
{
    int r = v->refine;
    int cw = range.x1 - range.x0, ch = range.y1 - range.y0,
        cd = range.z1 - range.z0;
    int bw = group_count(cw, span), bh = group_count(ch, span),
        bd = group_count(cd, span);
    unsigned char *solid = xmalloc((size_t)bw * bh);
    /* Open runs of the previous layer's rectangles, extended through depth. */
    typedef struct
    {
        int x, y, w, h, first, last;
    } run;
    run *runs = NULL;
    for (int bk = 0; bk < bd; bk++) {
        for (int bj = 0; bj < bh; bj++) {
            for (int bi = 0; bi < bw; bi++) {
                terrain_volume_bricks group = { bi * span,
                                                bj * span,
                                                bk * span,
                                                min_int((bi + 1) * span, cw),
                                                min_int((bj + 1) * span, ch),
                                                min_int((bk + 1) * span, cd) };
                solid[bj * bw + bi] = solid_group(solid_bricks, cw, ch, group);
            }
        }
        for (int bj = 0; bj < bh; bj++) {
            for (int bi = 0; bi < bw; bi++) {
                if (!solid[bj * bw + bi]) {
                    continue;
                }
                int w = 1, h = 1;
                while (bi + w < bw && solid[bj * bw + bi + w]) {
                    w++;
                }
                for (bool full = true; full && bj + h < bh;) {
                    for (int x = 0; x < w && full; x++) {
                        full = solid[(bj + h) * bw + bi + x];
                    }
                    h += full;
                }
                for (int y = 0; y < h; y++) {
                    memset(solid + (bj + y) * bw + bi, 0, (size_t)w);
                }
                bool extended = false;
                for (int n = 0; n < acount(runs) && !extended; n++) {
                    run *open = &runs[n];
                    if (open->last == bk - 1 && open->x == bi &&
                        open->y == bj && open->w == w && open->h == h) {
                        open->last = bk;
                        extended = true;
                    }
                }
                if (!extended) {
                    apush(runs, ((run){ bi, bj, w, h, bk, bk }));
                }
            }
        }
    }
    const field_grid *p = &v->plane;
    float cell = p->cell_size, brick = r * cell;
    float left = p->center_x + (.5f - p->width * .5f) * cell + range.x0 * brick;
    float top = p->center_y + (.5f - p->height * .5f) * cell + range.y0 * brick;
    /* Brick depths start at the frontmost layer. */
    elevation += v->front * cell - range.z0 * brick;
    for (int n = 0; n < acount(runs); n++) {
        run b = runs[n];
        int x1 = min_int((b.x + b.w) * span, cw),
            y1 = min_int((b.y + b.h) * span, ch),
            z1 = min_int((b.last + 1) * span, cd);
        apush(
            *boxes,
            ((terrain_volume_box){
                { left + b.x * span * brick, elevation - z1 * brick, top + b.y * span * brick },
                { left + x1 * brick, elevation - b.first * span * brick, top + y1 * brick } })
        );
    }
    afree(runs);
    free(solid);
}

static float box_volume(terrain_volume_box b)
{
    CF_V3 e = cf_sub_v3(b.max, b.min);
    return e.x * e.y * e.z;
}

/* Appends the limit largest of boxes, the first of equals first. */
static void append_largest(terrain_volume_box *from, int limit, terrain_volume_box **boxes)
{
    int count = acount(from);
    for (int n = 0; n < limit && n < count; n++) {
        int best = n;
        for (int i = n + 1; i < count; i++) {
            best = box_volume(from[i]) > box_volume(from[best]) ? i : best;
        }
        terrain_volume_box chosen = from[best];
        memmove(from + n + 1, from + n, (size_t)(best - n) * sizeof *from);
        from[n] = chosen;
        apush(*boxes, chosen);
    }
}

int terrain_volume_boxes(
    const terrain_volume *v,
    const float *removed,
    terrain_volume_bricks range,
    int limit,
    float elevation,
    terrain_volume_box **boxes
)
{
    assert(
        0 <= range.x0 && range.x0 <= range.x1 &&
        range.x1 <= coarse_count(v->plane.width, v->refine) - 1
    );
    assert(
        0 <= range.y0 && range.y0 <= range.y1 &&
        range.y1 <= coarse_count(v->plane.height, v->refine) - 1
    );
    assert(
        0 <= range.z0 && range.z0 <= range.z1 &&
        range.z1 <= coarse_count(v->layers, v->refine) - 1
    );
    assert(limit >= 0);
    int bw = range.x1 - range.x0, bh = range.y1 - range.y0,
        bd = range.z1 - range.z0;
    if (!bw || !bh || !bd || !limit) {
        return 0;
    }
    unsigned char *solid = xmalloc((size_t)bw * bh * bd);
    for (int bk = 0; bk < bd; bk++) {
        for (int bj = 0; bj < bh; bj++) {
            for (int bi = 0; bi < bw; bi++) {
                solid[((size_t)bk * bh + bj) * bw + bi] = solid_brick(
                    v,
                    removed,
                    range.x0 + bi,
                    range.y0 + bj,
                    range.z0 + bk
                );
            }
        }
    }
    /* A span as wide as the range leaves one group and at most one box, so
     * doubling ends within the limit. */
    int start = acount(*boxes);
    terrain_volume_box *finer = NULL;
    for (int span = 1;; span *= 2) {
        merge_boxes(v, solid, range, span, elevation, boxes);
        int count = acount(*boxes) - start;
        if (count <= limit) {
            break;
        }
        asetlen(finer, count);
        memcpy(finer, *boxes + start, (size_t)count * sizeof *finer);
        asetlen(*boxes, start);
    }
    /* Groups wide enough to fit can all hold air; the finer span's largest
     * boxes still cover solid rock. */
    if (acount(*boxes) == start) {
        append_largest(finer, limit, boxes);
    }
    afree(finer);
    free(solid);
    return acount(*boxes) - start;
}
