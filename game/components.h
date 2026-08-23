#ifndef CBLUEPRINT_GAME_COMPONENTS_H
#define CBLUEPRINT_GAME_COMPONENTS_H

#include "../core/annotate.h"

typedef NODE_COMPONENT_TYPE(GameTransform)
{
    float x;
    float y;
}
GameTransform;

typedef NODE_COMPONENT_TYPE_DENSE(GameHealth)
{
    int current;
    int maximum;
}
GameHealth;

#endif
