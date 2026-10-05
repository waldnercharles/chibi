#ifndef CHIBI_ASSETS_PROTOTYPE_INSTANCE_H
#define CHIBI_ASSETS_PROTOTYPE_INSTANCE_H

#include "assets/prototype.h"

/* An instance's values against its prototype, shared by Studio's prototype
 * actions on a live object and the MCP's on a scene file's entity (composed
 * in a world of its own): which fields it overrides, copying them into the
 * prototype (Apply Overrides), and a prototype of it (Save as New). Where an
 * instance stands, its GameTransform, stays its own in each, as Unity applies
 * no root position. */

/* A field of one of a prototype's components. */
typedef struct
{
    const ecs_component_desc *component;
    int field;
} game_prototype_field;

/* The fields of `base`'s components, outside GameTransform, holding plain
 * values (scalars, text and plain structs) that `entity` sets apart from
 * them, in component and field order. The caller afrees the array. */
game_prototype_field *game_prototype_instance_overrides(
    const game_prototype *base,
    const ecs_world *world,
    ecs_entity entity
);
/* Writes the entity's values of `fields` into `prototype`. */
void game_prototype_apply_instance(
    game_prototype *prototype,
    const ecs_world *world,
    ecs_entity entity,
    const game_prototype_field *fields,
    int count
);
/* A new prototype `name` the entity, an instance of `base`, is a copy of:
 * its components and values, `base`'s placement, and `behaviors`. */
game_prototype *game_prototype_from_instance(
    const char *name,
    const game_prototype *base,
    const ecs_world *world,
    ecs_entity entity,
    const char *const *behaviors,
    int behavior_count
);

#endif
