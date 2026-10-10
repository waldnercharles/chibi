#include "runtime/geometry/mesh_pick.h"

#include "support/array.h"

#include <assert.h>
#include <float.h>
#include <math.h>

/* Triangles a pick tree's leaf box holds at most. */
#define PICK_LEAF_TRIANGLES 4
/* Widens where a ray leaves a box by the float rounding of finding it,
 * 1 + 2 * gamma(3) in Ize's robust traversal, so a ray through the seam
 * between two boxes always enters one. */
#define PICK_ROUNDING 1.0000004f

/* A box in a pick tree around `count` triangles from `first` in its tree's
 * order. An inner box's two halves are the boxes at `children` and
 * `children + 1`; a leaf has none, since no box's child is the root. */
typedef struct
{
    CF_V3 min, max;
    int first, count, children;
} pick_box;

/* A tree of boxes over one resident mesh's triangles. */
typedef struct
{
    uint64_t identity;
    pick_box *boxes;
    int *triangles; /* Each triangle's first index, in leaf order. */
} pick_tree;

/* A box still to fill while building a tree. */
typedef struct
{
    int box, first, count;
} pick_span;

/* Trees built for resident meshes since the catalog last changed. */
static pick_tree *trees;

static float along(CF_V3 v, int axis)
{
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

static CF_V3 corner(const render_mesh_asset *mesh, int triangle, int i)
{
    return mesh->vertices[mesh->indices[triangle + i]].position;
}

/* The middle of a triangle, from thirds that cannot overflow. */
static CF_V3 middle(const render_mesh_asset *mesh, int triangle)
{
    CF_V3 sum = cf_add_v3(
        cf_div_v3_f(corner(mesh, triangle, 0), 3),
        cf_div_v3_f(corner(mesh, triangle, 1), 3)
    );
    return cf_add_v3(sum, cf_div_v3_f(corner(mesh, triangle, 2), 3));
}

/* Fills `span`'s box around its triangles and, when it holds more than a
 * leaf does, splits them at the middle of their centers' longest extent,
 * or in half by count when that leaves one side empty. Returns the halves'
 * spans' count, zero or two. */
static int fill(pick_tree *tree, const render_mesh_asset *mesh, pick_span span, pick_span halves[2])
{
    int *run = tree->triangles + span.first;
    CF_V3 low = { FLT_MAX, FLT_MAX, FLT_MAX }, high = cf_neg_v3(low);
    CF_V3 lowest = low, highest = high;
    for (int i = 0; i < span.count; i++) {
        for (int j = 0; j < 3; j++) {
            low = cf_min_v3(low, corner(mesh, run[i], j));
            high = cf_max_v3(high, corner(mesh, run[i], j));
        }
        lowest = cf_min_v3(lowest, middle(mesh, run[i]));
        highest = cf_max_v3(highest, middle(mesh, run[i]));
    }
    tree->boxes[span.box] = (pick_box){ low, high, span.first, span.count, 0 };
    if (span.count <= PICK_LEAF_TRIANGLES) {
        return 0;
    }
    CF_V3 extent = cf_sub_v3(highest, lowest);
    int axis = extent.x >= extent.y && extent.x >= extent.z ? 0
             : extent.y >= extent.z                         ? 1
                                                            : 2;
    float split_at = along(lowest, axis) / 2 + along(highest, axis) / 2;
    int split = 0;
    for (int i = 0; i < span.count; i++) {
        if (along(middle(mesh, run[i]), axis) < split_at) {
            int kept = run[split];
            run[split++] = run[i];
            run[i] = kept;
        }
    }
    if (!split || split == span.count) {
        split = span.count / 2;
    }
    int children = acount(tree->boxes);
    tree->boxes[span.box].children = children;
    apush(tree->boxes, (pick_box){ 0 });
    apush(tree->boxes, (pick_box){ 0 });
    halves[0] = (pick_span){ children, span.first, split };
    halves[1] = (pick_span){ children + 1, span.first + split, span.count - split };
    return 2;
}

static pick_tree build(const render_mesh_asset *mesh)
{
    pick_tree tree = { .identity = mesh->identity };
    for (int i = 0; i < acount(mesh->indices); i += 3) {
        apush(tree.triangles, i);
    }
    apush(tree.boxes, (pick_box){ 0 });
    pick_span *pending = NULL;
    apush(pending, ((pick_span){ 0, 0, acount(tree.triangles) }));
    while (acount(pending)) {
        pick_span span = pending[acount(pending) - 1];
        asetlen(pending, acount(pending) - 1);
        pick_span halves[2];
        int count = fill(&tree, mesh, span, halves);
        for (int i = 0; i < count; i++) {
            apush(pending, halves[i]);
        }
    }
    afree(pending);
    return tree;
}

/* Whether a ray crosses a box before `reach` along it. It works in double,
 * where a gap across float's range divides by a direction of any length
 * without overflow, so only an axis the ray doesn't move along at all needs
 * its origin inside instead. */
static bool crosses(CF_Ray3 ray, float reach, const pick_box *box)
{
    double enter = 0, leave = reach;
    for (int axis = 0; axis < 3; axis++) {
        double origin = along(ray.p, axis), direction = along(ray.d, axis);
        double low = along(box->min, axis), high = along(box->max, axis);
        if (direction == 0) {
            if (origin < low || origin > high) {
                return false;
            }
            continue;
        }
        double from = (low - origin) / direction, to = (high - origin) / direction;
        enter = fmax(enter, fmin(from, to));
        leave = fmin(leave, fmax(from, to) * PICK_ROUNDING);
    }
    return enter <= leave;
}

/* The tree of a resident mesh, built on its first pick. */
static const pick_tree *tree_of(const render_mesh_asset *mesh)
{
    assert(mesh->identity);
    for (int i = 0; i < acount(trees); i++) {
        if (trees[i].identity == mesh->identity) {
            return &trees[i];
        }
    }
    apush(trees, build(mesh));
    return &trees[acount(trees) - 1];
}

/* `a - b` in double. */
static void difference(CF_V3 a, CF_V3 b, double out[3])
{
    out[0] = (double)a.x - b.x;
    out[1] = (double)a.y - b.y;
    out[2] = (double)a.z - b.z;
}

static void cross(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static double dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Moller-Trumbore in double, where no float's product overflows, so a
 * triangle near float's range picks like one of ordinary size. */
float render_triangle_hit(CF_Ray3 ray, const CF_V3 triangle[3])
{
    double direction[3] = { ray.d.x, ray.d.y, ray.d.z }, edge1[3], edge2[3], p[3];
    difference(triangle[1], triangle[0], edge1);
    difference(triangle[2], triangle[0], edge2);
    cross(direction, edge2, p);
    double determinant = dot(edge1, p);
    if (fabs(determinant) <= 1e-7 * sqrt(dot(direction, direction)) *
                                 sqrt(dot(edge1, edge1)) * sqrt(dot(edge2, edge2))) {
        return FLT_MAX;
    }
    double delta[3], q[3];
    difference(ray.p, triangle[0], delta);
    double u = dot(delta, p) / determinant;
    cross(delta, edge1, q);
    double v = dot(direction, q) / determinant;
    double t = dot(edge2, q) / determinant;
    return u >= 0 && v >= 0 && u + v <= 1 && t >= 0 && t <= ray.t ? (float)t : FLT_MAX;
}

render_mesh_hit render_mesh_pick(const render_mesh_asset *mesh, CF_Ray3 ray)
{
    const pick_tree *tree = tree_of(mesh);
    render_mesh_hit hit = { FLT_MAX, 0 };
    int *pending = NULL;
    apush(pending, 0);
    while (acount(pending)) {
        const pick_box *box = &tree->boxes[pending[acount(pending) - 1]];
        asetlen(pending, acount(pending) - 1);
        if (!crosses(ray, fminf(ray.t, hit.t), box)) {
            continue;
        }
        if (box->children) {
            apush(pending, box->children);
            apush(pending, box->children + 1);
            continue;
        }
        for (int i = box->first; i < box->first + box->count; i++) {
            int triangle = tree->triangles[i];
            CF_V3 corners[3] = { corner(mesh, triangle, 0),
                                 corner(mesh, triangle, 1),
                                 corner(mesh, triangle, 2) };
            hit.t = fminf(hit.t, render_triangle_hit(ray, corners));
            hit.tested++;
        }
    }
    afree(pending);
    return hit;
}

void render_mesh_picks_clear()
{
    for (int i = 0; i < acount(trees); i++) {
        afree(trees[i].boxes);
        afree(trees[i].triangles);
    }
    afree(trees);
}

int render_mesh_pick_trees()
{
    return acount(trees);
}
