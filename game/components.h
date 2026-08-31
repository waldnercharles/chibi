#ifndef CBLUEPRINT_GAME_COMPONENTS_H
#define CBLUEPRINT_GAME_COMPONENTS_H

#include "../core/annotate.h"

typedef struct CF_V2
{
    float x;
    float y;
} CF_V2;

typedef NODE_COMPONENT_ALIAS(CF_V2) CF_V2 GameTransform;

typedef NODE_COMPONENT_TYPE_DENSE(GameHealth)
{
    int current;
    int maximum;
}
GameHealth;

typedef NODE_COMPONENT_TYPE_DENSE(GameMovement)
{
    float speed;
}
GameMovement;

typedef NODE_COMPONENT_TYPE_DENSE(GameInput)
{
    CF_V2 move;
}
GameInput;

typedef NODE_COMPONENT_TYPE_DENSE(GameWeapon)
{
    int damage;
    float cooldown;
    int projectile_count;
}
GameWeapon;

typedef NODE_COMPONENT_TYPE_DENSE(GameProjectile)
{
    float speed;
    float lifetime;
    int damage;
}
GameProjectile;

#endif
