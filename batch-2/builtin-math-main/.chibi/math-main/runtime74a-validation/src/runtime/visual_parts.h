#ifndef CHIBI_VISUAL_PARTS_H
#define CHIBI_VISUAL_PARTS_H

#include "runtime/components.h"

typedef struct
{
    int primitive;
    CF_V3 dimensions, center;
} visual_part;

/* Exact authored primitive pieces in entity-local units. Box Frame has twelve
 * nonoverlapping boxes; other primitives remain one intact piece. */
int visual_part_count(int primitive);
visual_part visual_part_at(Geometry3D shape, int index);

#endif
