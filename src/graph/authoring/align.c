#include "graph/authoring/align.h"

#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>

static float *start_of(align_box *box, bool vertical)
{
    return vertical ? &box->y : &box->x;
}

static float extent_of(const align_box *box, bool vertical)
{
    return vertical ? box->h : box->w;
}

static void bounds(align_box *boxes, int count, bool vertical, float *lo, float *hi)
{
    *lo = INFINITY;
    *hi = -INFINITY;
    for (int i = 0; i < count; i++) {
        float at = *start_of(&boxes[i], vertical);
        *lo = fminf(*lo, at);
        *hi = fmaxf(*hi, at + extent_of(&boxes[i], vertical));
    }
}

static float aligned(align_edge edge, float lo, float hi, float size)
{
    switch (edge) {
        case ALIGN_LEFT:
        case ALIGN_TOP: return lo;
        case ALIGN_RIGHT:
        case ALIGN_BOTTOM: return hi - size;
        default: return (lo + hi - size) / 2;
    }
}

void align_boxes(align_box *boxes, int count, align_edge edge)
{
    bool vertical = edge >= ALIGN_TOP;
    float lo, hi;
    bounds(boxes, count, vertical, &lo, &hi);
    for (int i = 0; i < count; i++) {
        *start_of(&boxes[i], vertical) = aligned(edge, lo, hi, extent_of(&boxes[i], vertical));
    }
}

typedef struct
{
    float at;
    int index;
} box_order;

static int by_start(const void *a, const void *b)
{
    const box_order *left = a, *right = b;
    if (left->at != right->at) {
        return left->at < right->at ? -1 : 1;
    }
    return left->index - right->index;
}

void distribute_boxes(align_box *boxes, int count, bool vertical)
{
    if (count < 3) {
        return;
    }
    float lo, hi, total = 0;
    bounds(boxes, count, vertical, &lo, &hi);
    box_order *order = xmalloc((size_t)count * sizeof *order);
    for (int i = 0; i < count; i++) {
        order[i] = (box_order){ *start_of(&boxes[i], vertical), i };
        total += extent_of(&boxes[i], vertical);
    }
    qsort(order, (size_t)count, sizeof *order, by_start);
    float gap = (hi - lo - total) / (float)(count - 1), at = lo;
    for (int i = 0; i < count; i++) {
        align_box *box = &boxes[order[i].index];
        *start_of(box, vertical) = at;
        at += extent_of(box, vertical) + gap;
    }
    free(order);
}
