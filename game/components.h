#ifndef CBLUEPRINT_GAME_COMPONENTS_H
#define CBLUEPRINT_GAME_COMPONENTS_H

#include "../core/annotate.h"

typedef struct CF_V2
{
    float x;
    float y;
} CF_V2;

typedef NODE_COMPONENT_ALIAS(CF_V2) CF_V2 GameTransform;

typedef NODE_COMPONENT_TYPE_DENSE(GameCollider)
{
    float radius;
    unsigned category;
    unsigned mask;
}
GameCollider;

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
    float direction_x;
    float direction_y;
    float age;
}
GameProjectile;

enum
{
    GAME_SHAPE_BOX = 0,
    GAME_SHAPE_CIRCLE,
    GAME_SHAPE_DIAMOND,
    GAME_SHAPE_CHEVRON,
    GAME_SHAPE_STROKED_CHEVRON,
    GAME_SHAPE_CAPSULE,
};

typedef NODE_COMPONENT_TYPE_DENSE(GameAppearance)
{
    float width;
    float height;
    unsigned color;
    int shape;
    float border_width;
    unsigned border_color;
    float glow_radius;
    unsigned glow_color;
    float stroke_width;
    float antialias;
}
GameAppearance;

typedef NODE_COMPONENT_TYPE_DENSE(GameOrientation)
{
    float radians;
}
GameOrientation;

typedef NODE_COMPONENT_TYPE_DENSE(GameSdfMotion)
{
    float amplitude;
    float angular_frequency;
    float temporal_frequency;
    float time;
    float pluck_amplitude;
    float pluck_origin;
    float pluck_age;
    float pluck_speed;
}
GameSdfMotion;

typedef NODE_COMPONENT_TYPE_DENSE(GameMaterial)
{
    unsigned scene;
    unsigned albedo;
    unsigned filter;
    unsigned emission_color;
    float emission_intensity;
    float emission_multiplier;
    float absorption;
    unsigned border_scene;
    unsigned border_emission_color;
    unsigned border_albedo;
    unsigned border_filter;
}
GameMaterial;

#endif
