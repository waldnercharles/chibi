#ifndef CBLUEPRINT_GAME_COMPONENTS_H
#define CBLUEPRINT_GAME_COMPONENTS_H

#include "../core/annotate.h"

#include <cute_math.h> /* Game owns the external math representation. */

typedef NODE_COMPONENT_ALIAS(CF_V2) CF_V2 GameTransform;

typedef NODE_COMPONENT_TYPE_DENSE(GameHealth)
{
    int current;
    int maximum;
}
GameHealth;

#endif
