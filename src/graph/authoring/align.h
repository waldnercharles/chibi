#ifndef CHIBI_GRAPH_ALIGN_H
#define CHIBI_GRAPH_ALIGN_H

#include <stdbool.h>

/* Aligning and distributing node boxes (Unreal's node alignment). Boxes are
 * x, y, width and height in graph units; only positions change. */
typedef struct align_box
{
    float x, y, w, h;
} align_box;

typedef enum
{
    ALIGN_LEFT,
    ALIGN_CENTER, /* the bounds' horizontal center */
    ALIGN_RIGHT,
    ALIGN_TOP,
    ALIGN_MIDDLE, /* the bounds' vertical center */
    ALIGN_BOTTOM,
} align_edge;

/* Lines every box up on an edge of their bounds, or on its center. */
void align_boxes(align_box *boxes, int count, align_edge edge);
/* Equal gaps between neighbours along one axis, keeping their order and the
 * bounds' extent (Figma's distribute spacing). */
void distribute_boxes(align_box *boxes, int count, bool vertical);

#endif
