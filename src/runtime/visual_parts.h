#ifndef CHIBI_GAME_VISUAL_PARTS_H
#define CHIBI_GAME_VISUAL_PARTS_H

#include "runtime/components.h"

typedef struct
{
    int primitive;
    CF_V3 dimensions, center;
} game_visual_part;

/* Exact authored primitive pieces in entity-local units. Box Frame has twelve
 * nonoverlapping boxes; other primitives remain one intact piece. */
int game_visual_part_count(int primitive);
game_visual_part game_visual_part_at(GameGeometry3D shape, int index);

#endif
