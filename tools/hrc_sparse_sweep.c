#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_DIMENSION 1024
#define MAX_DIRECTIONS 256
#define MAX_QUERIES 64
#define TRACE_GUARD 4

typedef struct
{
    double x, y;
} Vec2;
typedef struct
{
    double l[3], t;
} Path;
typedef struct
{
    double sigma, e[3];
} OpticalCell;
typedef struct
{
    double t0, t1;
    int cell;
    Path path;
} DenseLeaf;
typedef struct
{
    double t0, t1;
    Path path;
    int left, right, cell;
} TreeNode;
typedef struct
{
    double b, t0, t1;
    int root;
} QueryRay;
typedef struct
{
    QueryRay query;
    size_t dense_first, dense_count, node_first, node_count;
} BuildRay;
typedef struct
{
    double d[2], n[2];
} Direction;
typedef struct
{
    DenseLeaf *data;
    size_t count, capacity;
} LeafArray;
typedef struct
{
    TreeNode *data;
    size_t count, capacity;
} NodeArray;
typedef struct
{
    BuildRay *data;
    size_t count, capacity;
} RayArray;
typedef struct
{
    int width, height;
    OpticalCell *cells;
} Grid;
typedef struct
{
    double max_error, sum_sq;
    size_t values;
} ErrorStats;
typedef struct
{
    size_t count;
    double sum_sq, max_l, max_t;
    size_t light_leaks, false_occlusions;
} QualityStats;
typedef struct
{
    size_t ray;
    double a, b;
    Path serial;
} Query;
typedef struct
{
    Query *data;
    size_t count, capacity;
} QueryArray;
typedef struct
{
    double t0, t1;
    int cell;
} CellHit;

typedef struct
{
    int width, height;
    double pitch, phase;
    int queries_per_ray, quality_samples;
    int *directions;
    size_t direction_count;
    const char *scene;
    const char *ppm_prefix;
} Options;

static const Path IDENTITY = { .l = { 0.0, 0.0, 0.0 }, .t = 1.0 };
static volatile double benchmark_sink;

static void *xrealloc(void *ptr, size_t size)
{
    void *next = realloc(ptr, size);
    if (!next && size) {
        fprintf(stderr, "out of memory allocating %zu bytes\n", size);
        exit(2);
    }
    return next;
}

static double now_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(2);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

static Path compose(Path near, Path far)
{
    Path out;
    for (int c = 0; c < 3; ++c) {
        out.l[c] = near.l[c] + near.t * far.l[c];
    }
    out.t = near.t * far.t;
    return out;
}

static Path integrate_cell(const OpticalCell *cell, double distance)
{
    Path out = IDENTITY;
    if (distance <= 0.0) {
        return out;
    }
    out.t = exp(-cell->sigma * distance);
    double scale = cell->sigma > 1.0e-14 ? (1.0 - out.t) / cell->sigma : distance;
    for (int c = 0; c < 3; ++c) {
        out.l[c] = cell->e[c] * scale;
    }
    return out;
}

static bool is_identity(const OpticalCell *cell)
{
    return cell->sigma == 0.0 && cell->e[0] == 0.0 && cell->e[1] == 0.0 &&
           cell->e[2] == 0.0;
}

static void error_add(ErrorStats *stats, Path a, Path b)
{
    double vals_a[4] = { a.l[0], a.l[1], a.l[2], a.t };
    double vals_b[4] = { b.l[0], b.l[1], b.l[2], b.t };
    for (int i = 0; i < 4; ++i) {
        double e = fabs(vals_a[i] - vals_b[i]);
        stats->max_error = fmax(stats->max_error, e);
        stats->sum_sq += e * e;
        stats->values++;
    }
}

static double error_rms(const ErrorStats *stats)
{
    return stats->values ? sqrt(stats->sum_sq / (double)stats->values) : 0.0;
}

static void leaf_push(LeafArray *array, DenseLeaf leaf)
{
    if (array->count == array->capacity) {
        array->capacity = array->capacity ? array->capacity * 2 : 256;
        array->data = xrealloc(array->data, array->capacity * sizeof(*array->data));
    }
    array->data[array->count++] = leaf;
}

static void node_push(NodeArray *array, TreeNode node)
{
    if (array->count == array->capacity) {
        array->capacity = array->capacity ? array->capacity * 2 : 256;
        array->data = xrealloc(array->data, array->capacity * sizeof(*array->data));
    }
    array->data[array->count++] = node;
}

static void ray_push(RayArray *array, BuildRay ray)
{
    if (array->count == array->capacity) {
        array->capacity = array->capacity ? array->capacity * 2 : 128;
        array->data = xrealloc(array->data, array->capacity * sizeof(*array->data));
    }
    array->data[array->count++] = ray;
}

static void query_push(QueryArray *array, Query query)
{
    if (array->count == array->capacity) {
        array->capacity = array->capacity ? array->capacity * 2 : 1024;
        array->data = xrealloc(array->data, array->capacity * sizeof(*array->data));
    }
    array->data[array->count++] = query;
}

static void grid_set(Grid *grid, int x, int y, double sigma, double r, double g, double b)
{
    assert(x >= 0 && x < grid->width && y >= 0 && y < grid->height);
    OpticalCell *cell = &grid->cells[(size_t)y * (size_t)grid->width + (size_t)x];
    cell->sigma = sigma;
    cell->e[0] = r;
    cell->e[1] = g;
    cell->e[2] = b;
}

static void grid_rect(Grid *grid, int x0, int y0, int x1, int y1, double sigma, double r, double g, double b)
{
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > grid->width ? grid->width : x1;
    y1 = y1 > grid->height ? grid->height : y1;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            grid_set(grid, x, y, sigma, r, g, b);
        }
    }
}

static Grid make_grid(const char *scene, int width, int height)
{
    Grid grid = { .width = width, .height = height };
    size_t cell_count = (size_t)width * (size_t)height;
    grid.cells = calloc(cell_count, sizeof(*grid.cells));
    if (!grid.cells) {
        fprintf(stderr, "out of memory allocating grid\n");
        exit(2);
    }
    int cx = width / 2, cy = height / 2;
    if (strcmp(scene, "thinwall") == 0) {
        int y0 = height / 5, y1 = height * 4 / 5;
        grid_rect(&grid, cx, y0, cx + 1, y1, 14.0, 0.0, 0.0, 0.0);
        grid_set(&grid, width * 3 / 4, cy, 0.0, 9.0, 5.0, 2.0);
        grid_set(&grid, width * 3 / 4 + 1, cy, 0.0, 4.0, 2.0, 1.0);
    } else if (strcmp(scene, "tinyemitter") == 0) {
        grid_set(&grid, width * 3 / 4, height / 3, 0.0, 14.0, 3.0, 0.8);
        grid_rect(&grid, cx - 1, height / 4, cx, height * 3 / 4, 8.0, 0.0, 0.0, 0.0);
    } else if (strcmp(scene, "sparseobjects") == 0) {
        grid_rect(&grid, width / 5, height / 3, width / 5 + width / 12, height / 3 + 2, 9.0, 0.0, 0.0, 0.0);
        grid_rect(&grid, width * 2 / 5, height / 5, width * 2 / 5 + 2, height * 2 / 5, 7.0, 0.0, 0.0, 0.0);
        grid_rect(
            &grid,
            width * 3 / 5,
            height * 3 / 5,
            width * 3 / 5 + width / 10,
            height * 3 / 5 + 2,
            11.0,
            0.0,
            0.0,
            0.0
        );
        grid_set(&grid, width * 4 / 5, height / 4, 0.0, 4.0, 9.0, 2.0);
        grid_set(&grid, width / 3, height * 3 / 4, 0.0, 2.0, 3.0, 12.0);
        grid_set(&grid, width * 7 / 10, height * 4 / 5, 0.0, 8.0, 2.0, 1.0);
    } else if (strcmp(scene, "densefog") == 0) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                double wave = 0.5 + 0.5 * sin((double)x * 0.13 + cos((double)y * 0.09));
                double sigma = 0.045 + 0.055 * wave;
                double heat = 0.012 + 0.015 * (1.0 - wave);
                grid_set(&grid, x, y, sigma, heat, heat * 0.78, heat * 0.56);
            }
        }
        grid_rect(&grid, cx, height / 4, cx + 1, height * 3 / 4, 4.0, 0.0, 0.0, 0.0);
    } else {
        fprintf(stderr, "unknown scene '%s'\n", scene);
        exit(2);
    }
    return grid;
}

static Direction make_direction(double angle)
{
    Direction d;
    d.d[0] = cos(angle);
    d.d[1] = sin(angle);
    if (fabs(d.d[0]) < 1.0e-14) {
        d.d[0] = 0.0;
    }
    if (fabs(d.d[1]) < 1.0e-14) {
        d.d[1] = 0.0;
    }
    double length = hypot(d.d[0], d.d[1]);
    d.d[0] /= length;
    d.d[1] /= length;
    d.n[0] = -d.d[1];
    d.n[1] = d.d[0];
    return d;
}

static bool clip_axis(double origin, double direction, double low, double high, double *t0, double *t1)
{
    if (fabs(direction) < 1.0e-15) {
        return origin >= low && origin < high;
    }
    double a = (low - origin) / direction;
    double b = (high - origin) / direction;
    if (a > b) {
        double tmp = a;
        a = b;
        b = tmp;
    }
    *t0 = fmax(*t0, a);
    *t1 = fmin(*t1, b);
    return *t1 > *t0;
}

static bool clip_line(const Grid *grid, Direction direction, double b, double *entry, double *exit)
{
    double ox = direction.n[0] * b;
    double oy = direction.n[1] * b;
    double t0 = -DBL_MAX, t1 = DBL_MAX;
    if (!clip_axis(ox, direction.d[0], 0.0, (double)grid->width, &t0, &t1)) {
        return false;
    }
    if (!clip_axis(oy, direction.d[1], 0.0, (double)grid->height, &t0, &t1)) {
        return false;
    }
    if (!(t1 > t0)) {
        return false;
    }
    *entry = t0;
    *exit = t1;
    return true;
}

static int cell_index(double coordinate, double direction, int extent)
{
    if (coordinate <= 0.0) {
        return 0;
    }
    if (coordinate >= (double)extent) {
        return extent - 1;
    }
    double f = floor(coordinate);
    int index = (int)f;
    if (direction < 0.0 && coordinate == f) {
        --index;
    }
    return index < 0 ? 0 : (index >= extent ? extent - 1 : index);
}

static Path trace_line(
    const Grid *grid,
    Direction direction,
    double b,
    double stop_t,
    LeafArray *leaves,
    double *entry_out,
    double *exit_out
)
{
    double entry, ray_end;
    if (!clip_line(grid, direction, b, &entry, &ray_end)) {
        if (entry_out) {
            *entry_out = 0.0;
        }
        if (exit_out) {
            *exit_out = 0.0;
        }
        return IDENTITY;
    }
    if (entry_out) {
        *entry_out = entry;
    }
    if (exit_out) {
        *exit_out = ray_end;
    }
    double end = fmin(ray_end, stop_t);
    if (!(end > entry)) {
        return IDENTITY;
    }
    double x0 = direction.n[0] * b;
    double y0 = direction.n[1] * b;
    double t = entry;
    int x = cell_index(x0 + direction.d[0] * t, direction.d[0], grid->width);
    int y = cell_index(y0 + direction.d[1] * t, direction.d[1], grid->height);
    Path path = IDENTITY;
    int guard = 0;
    while (t < end && x >= 0 && x < grid->width && y >= 0 && y < grid->height) {
        if (++guard > grid->width + grid->height + TRACE_GUARD) {
            fprintf(stderr, "DDA iteration bound exceeded\n");
            exit(3);
        }
        double tx = DBL_MAX, ty = DBL_MAX;
        if (direction.d[0] > 0.0) {
            tx = ((double)(x + 1) - x0) / direction.d[0];
        } else if (direction.d[0] < 0.0) {
            tx = ((double)x - x0) / direction.d[0];
        }
        if (direction.d[1] > 0.0) {
            ty = ((double)(y + 1) - y0) / direction.d[1];
        } else if (direction.d[1] < 0.0) {
            ty = ((double)y - y0) / direction.d[1];
        }
        double next = fmin(end, fmin(tx, ty));
        if (next > t) {
            int cell_index_value = y * grid->width + x;
            Path segment = integrate_cell(&grid->cells[(size_t)cell_index_value], next - t);
            path = compose(path, segment);
            if (leaves) {
                leaf_push(
                    leaves,
                    (DenseLeaf){ .t0 = t, .t1 = next, .cell = cell_index_value, .path = segment }
                );
            }
            t = next;
        }
        if (t >= end) {
            break;
        }
        double tolerance = 16.0 * DBL_EPSILON * fmax(1.0, fabs(t));
        bool step_x = tx <= t + tolerance;
        bool step_y = ty <= t + tolerance;
        if (!step_x && !step_y) {
            fprintf(stderr, "DDA made no progress at %.17g (tx %.17g, ty %.17g)\n", t, tx, ty);
            exit(3);
        }
        if (step_x) {
            x += direction.d[0] > 0.0 ? 1 : -1;
        }
        if (step_y) {
            y += direction.d[1] > 0.0 ? 1 : -1;
        }
    }
    return path;
}

static int build_tree(const DenseLeaf *active, size_t begin, size_t end, NodeArray *nodes)
{
    if (begin >= end) {
        return -1;
    }
    if (end - begin == 1) {
        DenseLeaf leaf = active[begin];
        int index = (int)nodes->count;
        node_push(
            nodes,
            (TreeNode){ .t0 = leaf.t0,
                        .t1 = leaf.t1,
                        .path = leaf.path,
                        .left = -1,
                        .right = -1,
                        .cell = leaf.cell }
        );
        return index;
    }
    size_t mid = begin + (end - begin) / 2;
    int left = build_tree(active, begin, mid, nodes);
    int right = build_tree(active, mid, end, nodes);
    node_push(nodes, (TreeNode){ .left = left, .right = right, .cell = -1 });
    int index = (int)nodes->count - 1;
    TreeNode *node = &nodes->data[index];
    TreeNode *left_node = &nodes->data[left];
    TreeNode *right_node = &nodes->data[right];
    node->t0 = left_node->t0;
    node->t1 = right_node->t1;
    node->path = compose(left_node->path, right_node->path);
    node->left = left;
    node->right = right;
    node->cell = -1;
    return index;
}

static int build_ray_tree(
    const Grid *grid,
    const DenseLeaf *dense,
    size_t dense_count,
    NodeArray *nodes,
    size_t *scratch_peak
)
{
    if (!dense_count) {
        return -1;
    }
    size_t active_count = 0;
    for (size_t i = 0; i < dense_count; ++i) {
        int cell_index_value = dense[i].cell;
        if (!is_identity(&grid->cells[(size_t)cell_index_value])) {
            active_count++;
        }
    }
    DenseLeaf *active = active_count ? malloc(active_count * sizeof(*active)) : NULL;
    if (active_count && !active) {
        fprintf(stderr, "out of memory building active leaves\n");
        exit(2);
    }
    size_t out = 0;
    for (size_t i = 0; i < dense_count; ++i) {
        int cell_index_value = dense[i].cell;
        if (!is_identity(&grid->cells[(size_t)cell_index_value])) {
            active[out++] = dense[i];
        }
    }
    *scratch_peak = active_count * sizeof(*active) > *scratch_peak
                      ? active_count * sizeof(*active)
                      : *scratch_peak;
    int root = build_tree(active, 0, active_count, nodes);
    free(active);
    return root;
}

static Path tree_query_node(const Grid *grid, const NodeArray *nodes, int index, double a, double b)
{
    if (index < 0) {
        return IDENTITY;
    }
    const TreeNode *node = &nodes->data[index];
    if (b <= node->t0 || a >= node->t1) {
        return IDENTITY;
    }
    if (a <= node->t0 && b >= node->t1) {
        return node->path;
    }
    if (node->cell >= 0) {
        double lo = fmax(a, node->t0), hi = fmin(b, node->t1);
        if (!(hi > lo)) {
            return IDENTITY;
        }
        return integrate_cell(&grid->cells[(size_t)node->cell], hi - lo);
    }
    return compose(
        tree_query_node(grid, nodes, node->left, a, b),
        tree_query_node(grid, nodes, node->right, a, b)
    );
}

static Path query_tree(const Grid *grid, const NodeArray *nodes, const QueryRay *ray, double a, double b)
{
    if (b <= a || ray->root < 0) {
        return IDENTITY;
    }
    return tree_query_node(grid, nodes, ray->root, a, b);
}

static Path query_serial(const Grid *grid, const DenseLeaf *leaves, const BuildRay *ray, double a, double b)
{
    Path path = IDENTITY;
    size_t first = 0, end = ray->dense_count;
    while (first < end) {
        size_t mid = first + (end - first) / 2;
        if (leaves[ray->dense_first + mid].t1 <= a) {
            first = mid + 1;
        } else {
            end = mid;
        }
    }
    for (size_t i = first; i < ray->dense_count; ++i) {
        const DenseLeaf *leaf = &leaves[ray->dense_first + i];
        if (leaf->t0 >= b) {
            break;
        }
        double lo = fmax(a, leaf->t0), hi = fmin(b, leaf->t1);
        if (!(hi > lo)) {
            continue;
        }
        Path segment = (hi == leaf->t1 && lo == leaf->t0)
                         ? leaf->path
                         : integrate_cell(&grid->cells[(size_t)leaf->cell], hi - lo);
        path = compose(path, segment);
    }
    return path;
}

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static QueryArray make_queries(const Grid *grid, const LeafArray *leaves, const RayArray *rays, int queries_per_ray)
{
    QueryArray queries = { 0 };
    for (size_t ri = 0; ri < rays->count; ++ri) {
        const BuildRay *ray = &rays->data[ri];
        double span = ray->query.t1 - ray->query.t0;
        if (!(span > 0.0)) {
            continue;
        }
        for (int qi = 0; qi < queries_per_ray; ++qi) {
            double u0 = 0.0, u1 = 1.0;
            if (qi == 1) {
                u1 = 0.5;
            } else if (qi == 2) {
                u0 = 0.5;
            } else if (qi == 3) {
                u0 = 0.17;
                u1 = 0.83;
            } else if (qi >= 4) {
                uint32_t seed = hash32((uint32_t)(ri * 1315423911U +
                                                  (size_t)qi * 2654435761U));
                u0 = (double)(seed & 0xffffU) / 65535.0;
                u1 = (double)(hash32(seed) & 0xffffU) / 65535.0;
                if (u0 > u1) {
                    double temp = u0;
                    u0 = u1;
                    u1 = temp;
                }
                if (u1 - u0 < 1.0e-4) {
                    u1 = fmin(1.0, u0 + 0.125);
                }
            }
            double a = ray->query.t0 + u0 * span;
            double b = ray->query.t0 + u1 * span;
            Query query = { .ray = ri, .a = a, .b = b };
            query.serial = query_serial(grid, leaves->data, ray, a, b);
            query_push(&queries, query);
        }
    }
    return queries;
}

static int compare_hits(const void *left, const void *right)
{
    const CellHit *a = left, *b = right;
    if (a->t0 < b->t0) {
        return -1;
    }
    if (a->t0 > b->t0) {
        return 1;
    }
    if (a->cell < b->cell) {
        return -1;
    }
    if (a->cell > b->cell) {
        return 1;
    }
    return 0;
}

static bool clip_cell_axis(double origin, double direction, double low, double high, double *t0, double *t1)
{
    if (fabs(direction) < 1.0e-15) {
        return origin >= low && origin < high;
    }
    double a = (low - origin) / direction;
    double b = (high - origin) / direction;
    if (a > b) {
        double temp = a;
        a = b;
        b = temp;
    }
    *t0 = fmax(*t0, a);
    *t1 = fmin(*t1, b);
    return *t1 > *t0;
}

static Path brute_cell_line(const Grid *grid, Direction direction, double b)
{
    double ox = direction.n[0] * b, oy = direction.n[1] * b;
    size_t capacity = (size_t)grid->width * (size_t)grid->height;
    CellHit *hits = malloc(capacity * sizeof(*hits));
    if (!hits) {
        fprintf(stderr, "out of memory for reference hits\n");
        exit(2);
    }
    size_t count = 0;
    for (int y = 0; y < grid->height; ++y) {
        for (int x = 0; x < grid->width; ++x) {
            double t0 = -DBL_MAX, t1 = DBL_MAX;
            if (!clip_cell_axis(ox, direction.d[0], (double)x, (double)(x + 1), &t0, &t1)) {
                continue;
            }
            if (!clip_cell_axis(oy, direction.d[1], (double)y, (double)(y + 1), &t0, &t1)) {
                continue;
            }
            if (t1 - t0 <= 64.0 * DBL_EPSILON * fmax(1.0, fabs(t0))) {
                continue;
            }
            hits[count++] = (CellHit){ .t0 = t0, .t1 = t1, .cell = y * grid->width + x };
        }
    }
    qsort(hits, count, sizeof(*hits), compare_hits);
    Path path = IDENTITY;
    for (size_t i = 0; i < count; ++i) {
        Path segment = integrate_cell(
            &grid->cells[(size_t)hits[i].cell],
            hits[i].t1 - hits[i].t0
        );
        path = compose(path, segment);
    }
    free(hits);
    return path;
}

static void sample_range(const Grid *grid, Direction direction, double *min_b, double *max_b)
{
    Vec2 corners[4] = { { 0.0, 0.0 },
                        { (double)grid->width, 0.0 },
                        { 0.0, (double)grid->height },
                        { (double)grid->width, (double)grid->height } };
    *min_b = DBL_MAX;
    *max_b = -DBL_MAX;
    for (int i = 0; i < 4; ++i) {
        double b = direction.n[0] * corners[i].x + direction.n[1] * corners[i].y;
        *min_b = fmin(*min_b, b);
        *max_b = fmax(*max_b, b);
    }
}

static void build_samples(
    const Grid *grid,
    Direction direction,
    double pitch,
    double phase,
    RayArray *rays,
    LeafArray *leaves,
    NodeArray *nodes,
    size_t *scratch_peak,
    size_t *dense_count,
    size_t *active_count,
    double *line_sampling_ms,
    double *tree_build_ms
)
{
    double min_b, max_b;
    sample_range(grid, direction, &min_b, &max_b);
    long long first_k = (long long)ceil((min_b - phase) / pitch) - 1;
    long long last_k = (long long)floor((max_b - phase) / pitch) + 1;
    if (last_k - first_k > 100000) {
        fprintf(stderr, "sample pitch creates too many lines\n");
        exit(2);
    }
    for (long long k = first_k; k <= last_k; ++k) {
        double b = phase + (double)k * pitch;
        BuildRay ray = { 0 };
        ray.query.b = b;
        ray.dense_first = leaves->count;
        double entry = 0.0, exit = 0.0;
        double phase_start = now_seconds();
        (void)trace_line(grid, direction, b, INFINITY, leaves, &entry, &exit);
        *line_sampling_ms += (now_seconds() - phase_start) * 1000.0;
        ray.dense_count = leaves->count - ray.dense_first;
        ray.query.t0 = entry;
        ray.query.t1 = exit;
        ray.node_first = nodes->count;
        const DenseLeaf *ray_leaves = ray.dense_count ? &leaves->data[ray.dense_first]
                                                      : NULL;
        phase_start = now_seconds();
        ray.query.root = build_ray_tree(grid, ray_leaves, ray.dense_count, nodes, scratch_peak);
        *tree_build_ms += (now_seconds() - phase_start) * 1000.0;
        ray.node_count = nodes->count - ray.node_first;
        for (size_t i = 0; i < ray.dense_count; ++i) {
            (*dense_count)++;
            if (!is_identity(
                    &grid->cells[(size_t)leaves->data[ray.dense_first + i].cell]
                )) {
                (*active_count)++;
            }
        }
        ray_push(rays, ray);
    }
}

static Path ray_until(const Grid *grid, const NodeArray *nodes, const QueryRay *ray, double t)
{
    if (ray->root < 0 || t <= ray->t0) {
        return IDENTITY;
    }
    return query_tree(grid, nodes, ray, ray->t0, fmin(t, ray->t1));
}

static void bracket_rays(const RayArray *rays, double b, size_t *lo, size_t *hi, double *weight)
{
    size_t low = 0, high = rays->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (rays->data[mid].query.b < b) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low == 0) {
        *lo = *hi = 0;
        *weight = 0.0;
        return;
    }
    if (low >= rays->count) {
        *lo = *hi = rays->count - 1;
        *weight = 0.0;
        return;
    }
    if (rays->data[low].query.b == b) {
        *lo = *hi = low;
        *weight = 0.0;
        return;
    }
    *lo = low - 1;
    *hi = low;
    double b0 = rays->data[*lo].query.b, b1 = rays->data[*hi].query.b;
    *weight = b1 > b0 ? (b - b0) / (b1 - b0) : 0.0;
}

static Path blend_paths(Path a, Path b, double weight)
{
    Path out;
    for (int c = 0; c < 3; ++c) {
        out.l[c] = a.l[c] * (1.0 - weight) + b.l[c] * weight;
    }
    out.t = a.t * (1.0 - weight) + b.t * weight;
    return out;
}

static Path exact_to_point(const Grid *grid, Direction direction, Vec2 p)
{
    double b = direction.n[0] * p.x + direction.n[1] * p.y;
    double t = direction.d[0] * p.x + direction.d[1] * p.y;
    return trace_line(grid, direction, b, t, NULL, NULL, NULL);
}

static Path sparse_to_point(
    const Grid *grid,
    Direction direction,
    const RayArray *rays,
    const NodeArray *nodes,
    Vec2 p
)
{
    double b = direction.n[0] * p.x + direction.n[1] * p.y;
    double t = direction.d[0] * p.x + direction.d[1] * p.y;
    size_t lo, hi;
    double weight;
    bracket_rays(rays, b, &lo, &hi, &weight);
    Path a = ray_until(grid, nodes, &rays->data[lo].query, t);
    if (lo == hi) {
        return a;
    }
    Path c = ray_until(grid, nodes, &rays->data[hi].query, t);
    return blend_paths(a, c, weight);
}

static QualityStats measure_quality(
    const Grid *grid,
    Direction direction,
    const RayArray *rays,
    const NodeArray *nodes,
    int samples,
    uint32_t seed
)
{
    QualityStats stats = { 0 };
    size_t cells = (size_t)grid->width * (size_t)grid->height;
    for (int i = 0; i < samples; ++i) {
        seed = hash32(seed + (uint32_t)i + 0x9e3779b9U);
        size_t index = (size_t)seed % cells;
        int x = (int)(index % (size_t)grid->width);
        int y = (int)(index / (size_t)grid->width);
        Vec2 p = { (double)x + 0.5, (double)y + 0.5 };
        Path exact = exact_to_point(grid, direction, p);
        Path sparse = sparse_to_point(grid, direction, rays, nodes, p);
        double e_l = 0.0;
        for (int c = 0; c < 3; ++c) {
            e_l = fmax(e_l, fabs(exact.l[c] - sparse.l[c]));
        }
        double e_t = fabs(exact.t - sparse.t);
        stats.max_l = fmax(stats.max_l, e_l);
        stats.max_t = fmax(stats.max_t, e_t);
        for (int c = 0; c < 3; ++c) {
            double e = exact.l[c] - sparse.l[c];
            stats.sum_sq += e * e;
        }
        stats.sum_sq += e_t * e_t;
        stats.count += 4;
        if (exact.t < 1.0e-3 && sparse.t > 0.05) {
            stats.light_leaks++;
        }
        if (exact.t > 0.95 && sparse.t < 0.5) {
            stats.false_occlusions++;
        }
    }
    return stats;
}

static void measure_cell_reference(
    const Grid *grid,
    Direction direction,
    const RayArray *rays,
    const LeafArray *leaves,
    ErrorStats *errors,
    size_t *checks,
    double *reference_ms
)
{
    if (!rays->count) {
        return;
    }
    size_t pick[3] = { 0, rays->count / 2, rays->count - 1 };
    for (int i = 0; i < 3; ++i) {
        size_t ri = pick[i];
        const BuildRay *ray = &rays->data[ri];
        if (!(ray->query.t1 > ray->query.t0)) {
            continue;
        }
        double entry, exit;
        Path direct = trace_line(grid, direction, ray->query.b, INFINITY, NULL, &entry, &exit);
        double start = now_seconds();
        Path reference = brute_cell_line(grid, direction, ray->query.b);
        *reference_ms += (now_seconds() - start) * 1000.0;
        Path serial = query_serial(grid, leaves->data, ray, entry, exit);
        error_add(errors, direct, reference);
        error_add(errors, serial, reference);
        (*checks)++;
    }
}

static void make_query_bench(
    const Grid *grid,
    const NodeArray *nodes,
    const QueryArray *queries,
    const RayArray *rays,
    const LeafArray *leaves,
    ErrorStats *errors,
    double *tree_ms,
    double *serial_ms
)
{
    for (size_t i = 0; i < queries->count; ++i) {
        const Query *q = &queries->data[i];
        const BuildRay *ray = &rays->data[q->ray];
        Path tree = query_tree(grid, nodes, &ray->query, q->a, q->b);
        error_add(errors, tree, q->serial);
    }
    const int repeats = 4;
    double start = now_seconds();
    for (int repeat = 0; repeat < repeats; ++repeat) {
        for (size_t i = 0; i < queries->count; ++i) {
            const Query *q = &queries->data[i];
            Path path = query_tree(grid, nodes, &rays->data[q->ray].query, q->a, q->b);
            benchmark_sink += path.l[0] + path.l[1] + path.t;
        }
    }
    *tree_ms = (now_seconds() - start) * 1000.0 / (double)repeats;
    start = now_seconds();
    for (int repeat = 0; repeat < repeats; ++repeat) {
        for (size_t i = 0; i < queries->count; ++i) {
            const Query *q = &queries->data[i];
            Path path = query_serial(grid, leaves->data, &rays->data[q->ray], q->a, q->b);
            benchmark_sink += path.l[0] + path.l[1] + path.t;
        }
    }
    *serial_ms = (now_seconds() - start) * 1000.0 / (double)repeats;
}

static double tone(double value)
{
    value = fmax(0.0, value);
    return 1.0 - exp(-value);
}

static unsigned char byte_from_unit(double value)
{
    value = fmin(1.0, fmax(0.0, value));
    return (unsigned char)lrint(value * 255.0);
}

static void ppm_pixel(FILE *file, Path path)
{
    fputc(byte_from_unit(tone(path.l[0])), file);
    fputc(byte_from_unit(tone(path.l[1])), file);
    fputc(byte_from_unit(tone(path.l[2])), file);
}

static bool write_ppm_set(
    const char *prefix,
    const char *scene,
    int directions_count,
    const Grid *grid,
    Direction direction,
    const RayArray *rays,
    const NodeArray *nodes
)
{
    char exact_name[1024], sparse_name[1024], error_name[1024];
    snprintf(exact_name, sizeof(exact_name), "%s_%s_%d_exact.ppm", prefix, scene, directions_count);
    snprintf(sparse_name, sizeof(sparse_name), "%s_%s_%d_sparse.ppm", prefix, scene, directions_count);
    snprintf(error_name, sizeof(error_name), "%s_%s_%d_error.ppm", prefix, scene, directions_count);
    FILE *exact = fopen(exact_name, "wb"), *sparse = fopen(sparse_name, "wb"),
         *error = fopen(error_name, "wb");
    if (!exact || !sparse || !error) {
        fprintf(stderr, "failed to create PPM output with prefix '%s'\n", prefix);
        if (exact) {
            fclose(exact);
        }
        if (sparse) {
            fclose(sparse);
        }
        if (error) {
            fclose(error);
        }
        return false;
    }
    fprintf(exact, "P6\n%d %d\n255\n", grid->width, grid->height);
    fprintf(sparse, "P6\n%d %d\n255\n", grid->width, grid->height);
    fprintf(error, "P6\n%d %d\n255\n", grid->width, grid->height);
    for (int y = 0; y < grid->height; ++y) {
        for (int x = 0; x < grid->width; ++x) {
            Vec2 p = { (double)x + 0.5, (double)y + 0.5 };
            Path a = exact_to_point(grid, direction, p);
            Path b = sparse_to_point(grid, direction, rays, nodes, p);
            ppm_pixel(exact, a);
            ppm_pixel(sparse, b);
            double e = 0.0;
            for (int c = 0; c < 3; ++c) {
                e = fmax(e, fabs(a.l[c] - b.l[c]));
            }
            fputc(byte_from_unit(fmin(1.0, e * 0.5)), error);
            fputc(byte_from_unit(fmin(1.0, fabs(a.t - b.t) * 2.0)), error);
            fputc(byte_from_unit(fmin(1.0, e * 0.2)), error);
        }
    }
    fclose(exact);
    fclose(sparse);
    fclose(error);
    return true;
}

static int parse_int(const char *value, const char *name, int min, int max)
{
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || *end || parsed < min || parsed > max) {
        fprintf(stderr, "invalid %s: %s\n", name, value);
        exit(2);
    }
    return (int)parsed;
}

static double parse_double(const char *value, const char *name, double min, double max)
{
    char *end = NULL;
    double parsed = strtod(value, &end);
    if (!end || *end || !isfinite(parsed) || parsed < min || parsed > max) {
        fprintf(stderr, "invalid %s: %s\n", name, value);
        exit(2);
    }
    return parsed;
}

static void parse_directions(Options *options, const char *value)
{
    char *copy = strdup(value);
    if (!copy) {
        exit(2);
    }
    char *save = NULL;
    for (char *part = strtok_r(copy, ",", &save); part;
         part = strtok_r(NULL, ",", &save)) {
        if (options->direction_count >= 16) {
            fprintf(stderr, "too many direction counts\n");
            exit(2);
        }
        int count = parse_int(part, "direction count", 1, MAX_DIRECTIONS);
        options->directions[options->direction_count++] = count;
    }
    free(copy);
    if (!options->direction_count) {
        fprintf(stderr, "empty direction list\n");
        exit(2);
    }
}

static Options parse_options(int argc, char **argv)
{
    Options options = { .width = 96,
                        .height = 64,
                        .pitch = 2.0,
                        .phase = 0.0,
                        .queries_per_ray = 8,
                        .quality_samples = 256,
                        .scene = "all",
                        .ppm_prefix = NULL };
    options.directions = calloc(16, sizeof(*options.directions));
    if (!options.directions) {
        exit(2);
    }
    parse_directions(&options, "4,16,64");
    for (int i = 1; i < argc; ++i) {
        if (i + 1 >= argc) {
            fprintf(stderr, "missing value for %s\n", argv[i]);
            exit(2);
        }
        const char *key = argv[i], *value = argv[++i];
        if (strcmp(key, "--width") == 0) {
            options.width = parse_int(value, "width", 8, MAX_DIMENSION);
        } else if (strcmp(key, "--height") == 0) {
            options.height = parse_int(value, "height", 8, MAX_DIMENSION);
        } else if (strcmp(key, "--pitch") == 0) {
            options.pitch = parse_double(value, "pitch", 0.25, 4096.0);
        } else if (strcmp(key, "--phase") == 0) {
            options.phase = parse_double(value, "phase", -1.0e9, 1.0e9);
        } else if (strcmp(key, "--queries-per-ray") == 0) {
            options.queries_per_ray = parse_int(value, "queries per ray", 1, MAX_QUERIES);
        } else if (strcmp(key, "--quality-samples") == 0) {
            options.quality_samples = parse_int(value, "quality samples", 0, 1000000);
        } else if (strcmp(key, "--directions") == 0) {
            options.direction_count = 0;
            parse_directions(&options, value);
        } else if (strcmp(key, "--scene") == 0) {
            options.scene = value;
        } else if (strcmp(key, "--ppm-prefix") == 0) {
            options.ppm_prefix = value;
        } else {
            fprintf(stderr, "unknown option: %s\n", key);
            exit(2);
        }
    }
    return options;
}

static bool known_scene(const char *scene)
{
    return strcmp(scene, "thinwall") == 0 || strcmp(scene, "tinyemitter") == 0 ||
           strcmp(scene, "sparseobjects") == 0 || strcmp(scene, "densefog") == 0;
}

static double rms_quality(const QualityStats *stats)
{
    return stats->count ? sqrt(stats->sum_sq / (double)stats->count) : 0.0;
}

static bool run_case(const Options *options, const char *scene, int directions_count, bool *first_output)
{
    double total_start = now_seconds();
    Grid grid = make_grid(scene, options->width, options->height);
    size_t grid_cells = (size_t)grid.width * (size_t)grid.height;
    size_t occupied = 0;
    for (size_t i = 0; i < grid_cells; ++i) {
        if (!is_identity(&grid.cells[i])) {
            occupied++;
        }
    }
    RayArray rays = { 0 };
    LeafArray leaves = { 0 };
    NodeArray nodes = { 0 };
    size_t *direction_first = calloc((size_t)directions_count, sizeof(*direction_first));
    size_t *direction_rays = calloc((size_t)directions_count, sizeof(*direction_rays));
    if (!direction_first || !direction_rays) {
        fprintf(stderr, "out of memory for direction spans\n");
        exit(2);
    }
    size_t dense_count = 0, active_count = 0, scratch_peak = 0;
    double line_sampling_ms = 0.0, tree_build_ms = 0.0;
    double build_start = now_seconds();
    for (int i = 0; i < directions_count; ++i) {
        double angle = (2.0 * M_PI * (double)i) / (double)directions_count;
        Direction direction = make_direction(angle);
        direction_first[i] = rays.count;
        build_samples(
            &grid,
            direction,
            options->pitch,
            options->phase,
            &rays,
            &leaves,
            &nodes,
            &scratch_peak,
            &dense_count,
            &active_count,
            &line_sampling_ms,
            &tree_build_ms
        );
        direction_rays[i] = rays.count - direction_first[i];
    }
    double build_ms = (now_seconds() - build_start) * 1000.0;

    QueryArray queries = make_queries(&grid, &leaves, &rays, options->queries_per_ray);
    ErrorStats query_errors = { 0 }, cell_errors = { 0 };
    double tree_ms = 0.0, serial_ms = 0.0, cell_ref_ms = 0.0;
    make_query_bench(&grid, &nodes, &queries, &rays, &leaves, &query_errors, &tree_ms, &serial_ms);

    size_t reference_checks = 0;
    for (int i = 0; i < directions_count; ++i) {
        Direction direction = make_direction((2.0 * M_PI * (double)i) / (double)directions_count);
        RayArray view = { .data = rays.data + direction_first[i],
                          .count = direction_rays[i] };
        measure_cell_reference(&grid, direction, &view, &leaves, &cell_errors, &reference_checks, &cell_ref_ms);
    }

    QualityStats quality = { 0 };
    if (options->quality_samples > 0) {
        for (int i = 0; i < directions_count; ++i) {
            Direction direction = make_direction(
                (2.0 * M_PI * (double)i) / (double)directions_count
            );
            RayArray view = { .data = rays.data + direction_first[i],
                              .count = direction_rays[i] };
            QualityStats per_direction = measure_quality(
                &grid,
                direction,
                &view,
                &nodes,
                options->quality_samples,
                hash32((uint32_t)(i + directions_count * 17))
            );
            quality.count += per_direction.count;
            quality.sum_sq += per_direction.sum_sq;
            quality.max_l = fmax(quality.max_l, per_direction.max_l);
            quality.max_t = fmax(quality.max_t, per_direction.max_t);
            quality.light_leaks += per_direction.light_leaks;
            quality.false_occlusions += per_direction.false_occlusions;
        }
    }

    bool ppm_written = false;
    if (options->ppm_prefix) {
        /* The 45-degree field is shown when present; for four directions use the diagonal-free eastward field. */
        int index = directions_count >= 8 ? directions_count / 8 : 0;
        Direction direction = make_direction(
            (2.0 * M_PI * (double)index) / (double)directions_count
        );
        RayArray view = { .data = rays.data + direction_first[index],
                          .count = direction_rays[index] };
        ppm_written = write_ppm_set(
            options->ppm_prefix,
            scene,
            directions_count,
            &grid,
            direction,
            &view,
            &nodes
        );
    }

    size_t tree_node_bytes = nodes.count * sizeof(TreeNode);
    size_t ray_meta_bytes = rays.count * sizeof(QueryRay);
    size_t persistent_bytes = tree_node_bytes + ray_meta_bytes;
    size_t dense_reference_bytes = leaves.capacity * sizeof(DenseLeaf) +
                                   rays.capacity * sizeof(BuildRay);
    size_t input_grid_bytes = grid_cells * sizeof(OpticalCell);
    size_t node_array_bytes = nodes.capacity * sizeof(TreeNode);
    size_t build_ray_array_bytes = rays.capacity * sizeof(BuildRay);
    bool has_cardinal = directions_count >= 4 && directions_count % 4 == 0;
    bool has_diagonal = directions_count >= 8 && directions_count % 8 == 0;
    bool has_near_axis = directions_count >= 36;
    size_t dense_bytes = dense_count * sizeof(DenseLeaf);
    double total_ms = (now_seconds() - total_start) * 1000.0;
    double occupancy = grid_cells ? (double)occupied / (double)grid_cells : 0.0;
    double leaf_occupancy = dense_count ? (double)active_count / (double)dense_count
                                        : 0.0;
    bool invariants_pass = query_errors.max_error <= 1.0e-8 &&
                           cell_errors.max_error <= 1.0e-8;

    if (!*first_output) {
        puts(",");
    }
    *first_output = false;
    printf(
        "    "
        "{\"scene\":\"%s\",\"width\":%d,\"height\":%d,\"sample_pitch\":%.9g,"
        "\"world_phase\":%.9g,\"directions\":%d,\"rays\":%zu,\"occupied_grid_"
        "cells\":%zu,\"grid_occupancy\":%.9g,\"dense_sampled_leaves\":%zu,"
        "\"active_sampled_leaves\":%zu,\"active_leaf_occupancy\":%.9g,\"tree_"
        "nodes\":%zu,\"query_count\":%zu,\"tree_path_precision_bits\":64,"
        "\"tree_node_bytes_logical\":%zu,\"ray_metadata_bytes_logical\":%zu,"
        "\"persistent_tree_bytes_logical\":%zu,\"input_grid_bytes\":%zu,\"tree_"
        "node_array_allocated_bytes\":%zu,\"prototype_build_ray_array_"
        "allocated_bytes\":%zu,\"dense_reference_buffer_bytes\":%zu,\"active_"
        "leaf_build_scratch_peak_bytes\":%zu,\"dense_leaf_equivalent_bytes_"
        "logical\":%zu,\"line_sampling_ms\":%.6f,\"tree_build_ms\":%.6f,"
        "\"build_ms\":%.6f,\"tree_query_ms\":%.6f,\"serial_precomputed_leaf_"
        "query_ms\":%.6f,\"build_plus_tree_query_ms\":%.6f,\"query_comparison_"
        "scope\":\"tree range query versus serial replay of precomputed DDA "
        "leaves; serial timing excludes line traversal and "
        "construction\",\"query_max_abs_error\":%.17g,\"query_rms_error\":%."
        "17g,\"cell_reference_checks\":%zu,\"cell_reference_ms\":%.6f,\"cell_"
        "reference_max_abs_error\":%.17g,\"quality_samples_per_direction\":%d,"
        "\"quality_max_L_error\":%.9g,\"quality_max_T_error\":%.9g,\"quality_"
        "rms_error\":%.9g,\"false_light_leaks\":%zu,\"false_occlusions\":%zu,"
        "\"directions_contain_cardinals\":%s,\"directions_contain_45deg\":%s,"
        "\"directions_contain_near_axis_within_10deg\":%s,\"numerical_"
        "invariants_pass\":%s,\"total_wall_ms\":%.6f,\"ppm_written\":%s}",
        scene,
        grid.width,
        grid.height,
        options->pitch,
        options->phase,
        directions_count,
        rays.count,
        occupied,
        occupancy,
        dense_count,
        active_count,
        leaf_occupancy,
        nodes.count,
        queries.count,
        tree_node_bytes,
        ray_meta_bytes,
        persistent_bytes,
        input_grid_bytes,
        node_array_bytes,
        build_ray_array_bytes,
        dense_reference_bytes,
        scratch_peak,
        dense_bytes,
        line_sampling_ms,
        tree_build_ms,
        build_ms,
        tree_ms,
        serial_ms,
        build_ms + tree_ms,
        query_errors.max_error,
        error_rms(&query_errors),
        reference_checks,
        cell_ref_ms,
        cell_errors.max_error,
        options->quality_samples,
        quality.max_l,
        quality.max_t,
        rms_quality(&quality),
        quality.light_leaks,
        quality.false_occlusions,
        has_cardinal ? "true" : "false",
        has_diagonal ? "true" : "false",
        has_near_axis ? "true" : "false",
        invariants_pass ? "true" : "false",
        total_ms,
        ppm_written ? "true" : "false"
    );
    if (!invariants_pass) {
        fprintf(
            stderr,
            "%s/%d directions failed numerical invariants: query %.3g, cell "
            "%.3g\n",
            scene,
            directions_count,
            query_errors.max_error,
            cell_errors.max_error
        );
    }

    free(queries.data);
    free(nodes.data);
    free(leaves.data);
    free(rays.data);
    free(direction_first);
    free(direction_rays);
    free(grid.cells);
    return invariants_pass;
}

int main(int argc, char **argv)
{
    Options options = parse_options(argc, argv);
    bool all = strcmp(options.scene, "all") == 0;
    if (!all && !known_scene(options.scene)) {
        fprintf(stderr, "--scene must be all, thinwall, tinyemitter, sparseobjects, or densefog\n");
        return 2;
    }
    const char *scenes[] = { "thinwall", "tinyemitter", "sparseobjects", "densefog" };
    puts("{");
    puts("  \"study\":\"CPU-only prototype; no GPU frame-time or GPU memory claims\",");
    puts("  \"method\":\"world-anchored parallel lines sample a piecewise-constant optical grid; each line stores exact ordered (L,T) cell transport in a balanced tree over nonidentity leaves; arbitrary range queries are checked against same-leaf serial composition\",");
    puts("  \"composition\":\"(La,Ta) o (Lb,Tb) = (La + Ta*Lb, Ta*Tb); only exactly identity cells are omitted; interpolation, when measured, occurs after each path has been composed\",");
    puts("  \"precision\":\"CPU tree path L is three IEEE-754 64-bit doubles and T is one IEEE-754 64-bit double; logical bytes include C struct padding and exclude allocator overhead; current GPU RGB8B/scalar4B precision and memory behavior are not modeled\",");
    printf("  \"results\":[\n");
    bool first = true;
    bool passed = true;
    for (size_t si = 0; si < (all ? 4U : 1U); ++si) {
        const char *scene = all ? scenes[si] : options.scene;
        for (size_t di = 0; di < options.direction_count; ++di) {
            passed = run_case(&options, scene, options.directions[di], &first) && passed;
        }
    }
    puts("\n  ]");
    puts("}");
    free(options.directions);
    return passed ? 0 : 1;
}
