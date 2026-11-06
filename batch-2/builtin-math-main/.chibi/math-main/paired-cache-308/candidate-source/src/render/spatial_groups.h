#ifndef CHIBI_RENDER_SPATIAL_GROUPS_H
#define CHIBI_RENDER_SPATIAL_GROUPS_H

#include "platform/cute.h"
#include "render/view_2d.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

enum
{
    RENDER_SCRATCH_GROUP_MAX = 16,
    RENDER_SCRATCH_MIN_SAVING = 256
};

/* Half-open padded destination pixel bounds, shared by scratch and grouping. */
typedef struct
{
    int x, y, right, bottom;
} render_pixel_rect;

typedef struct
{
    render_pixel_rect rect;
    int actor, group;
} render_spatial_actor;

static inline render_pixel_rect render_mesh_pixel_bounds(
    render_view_2d view,
    CF_V2 lo,
    CF_V2 hi,
    int width,
    int height
)
{
    float sx = width / view.width, sy = height / view.height;
    int x = (int)floorf(fminf(width, fmaxf(0, (lo.x - view.min_x) * sx))) - 1;
    int y = (int)floorf(fminf(height, fmaxf(0, (view.max_y + lo.y) * sy))) - 1;
    int right = (int)ceilf(fminf(width, fmaxf(0, (hi.x - view.min_x) * sx))) + 1;
    int bottom = (int)ceilf(fminf(height, fmaxf(0, (view.max_y + hi.y) * sy))) + 1;
    return (render_pixel_rect){ x < 0 ? 0 : x,
                                y < 0 ? 0 : y,
                                right > width ? width : right,
                                bottom > height ? height : bottom };
}

static inline int render_spatial_compare_x(const void *a, const void *b)
{
    const render_spatial_actor *x = a, *y = b;
    if (x->rect.x != y->rect.x) {
        return (x->rect.x > y->rect.x) - (x->rect.x < y->rect.x);
    }
    return (x->actor > y->actor) - (x->actor < y->actor);
}
static inline int render_spatial_compare_y(const void *a, const void *b)
{
    const render_spatial_actor *x = a, *y = b;
    if (x->rect.y != y->rect.y) {
        return (x->rect.y > y->rect.y) - (x->rect.y < y->rect.y);
    }
    return (x->actor > y->actor) - (x->actor < y->actor);
}

/* Split only at an empty line across the whole group. Choose the most balanced
 * available cut; no intersecting bounds can enter different depth groups. */
static inline int render_spatial_cut(render_spatial_actor *actors, int count, bool vertical)
{
    qsort(actors, (size_t)count, sizeof *actors, vertical ? render_spatial_compare_x : render_spatial_compare_y);
    int far = INT_MIN, cut = 0, balance = INT_MAX;
    for (int i = 0; i < count - 1; i++) {
        int end = vertical ? actors[i].rect.right : actors[i].rect.bottom;
        if (end > far) {
            far = end;
        }
        int next = vertical ? actors[i + 1].rect.x : actors[i + 1].rect.y;
        int difference = abs(count - 2 * (i + 1));
        if (far <= next && difference < balance) {
            cut = i + 1;
            balance = difference;
        }
    }
    return cut;
}
static inline size_t render_spatial_area(const render_spatial_actor *actors, int count)
{
    render_pixel_rect bounds = actors[0].rect;
    for (int i = 1; i < count; i++) {
        if (actors[i].rect.x < bounds.x) {
            bounds.x = actors[i].rect.x;
        }
        if (actors[i].rect.y < bounds.y) {
            bounds.y = actors[i].rect.y;
        }
        if (actors[i].rect.right > bounds.right) {
            bounds.right = actors[i].rect.right;
        }
        if (actors[i].rect.bottom > bounds.bottom) {
            bounds.bottom = actors[i].rect.bottom;
        }
    }
    return (size_t)(bounds.right - bounds.x) * (bounds.bottom - bounds.y);
}

static inline int render_spatial_partition_budget(
    render_spatial_actor *actors,
    int count,
    int first_group,
    int budget
)
{
    if (count < 2) {
        if (count) {
            actors[0].group = first_group;
        }
        return count;
    }
    int cut = budget > 1 ? render_spatial_cut(actors, count, true) : 0;
    if (!cut && budget > 1) {
        cut = render_spatial_cut(actors, count, false);
    }
    if (cut) {
        size_t parent = render_spatial_area(actors, count);
        size_t children = render_spatial_area(actors, cut) +
                          render_spatial_area(actors + cut, count - cut);
        // Bound pass overhead: a cut must remove >=256 destination pixels and
        // at least a quarter of this node's rectangular scratch allocation.
        if (children > parent || parent - children < RENDER_SCRATCH_MIN_SAVING ||
            parent - children < parent / 4) {
            cut = 0;
        }
    }
    if (!cut) {
        for (int i = 0; i < count; i++) {
            actors[i].group = first_group;
        }
        return 1;
    }
    int left = render_spatial_partition_budget(actors, cut, first_group, budget / 2);
    return left +
           render_spatial_partition_budget(actors + cut, count - cut, first_group + left, budget - left);
}
static inline int render_spatial_partition(render_spatial_actor *actors, int count, int first_group)
{
    return render_spatial_partition_budget(actors, count, first_group, RENDER_SCRATCH_GROUP_MAX);
}
#endif
