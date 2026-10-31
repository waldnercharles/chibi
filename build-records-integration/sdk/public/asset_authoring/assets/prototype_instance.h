#ifndef CHIBI_ASSETS_PROTOTYPE_INSTANCE_H
#define CHIBI_ASSETS_PROTOTYPE_INSTANCE_H

#include "assets/prototype.h"

/* An instance's values against its prototype, shared by Studio's prototype
 * actions and shared SDK consumers on a live object: which fields it overrides,
 * copying them into the prototype (Apply Overrides), and a prototype of it
 * (Save as New). Where an instance stands, its Transform, stays its own in
 * each, as Unity applies no root position. */

/* A field of one of a prototype's components. */
typedef struct
{
    const ecs_component_desc *component;
    int field;
} prototype_field;

/* The fields of `base`'s components, outside Transform, holding plain
 * values (scalars, text and plain structs) that `entity` sets apart from
 * them, in component and field order. The caller afrees the array. */
prototype_field *prototype_instance_overrides(
    const prototype *base,
    const ecs_world *world,
    ecs_entity entity
);
/* Writes the entity's values of `fields` into `prototype`. */
void prototype_apply_instance(
    prototype *prototype,
    const ecs_world *world,
    ecs_entity entity,
    const prototype_field *fields,
    int count
);
/* A new prototype `name` the entity, an instance of `base`, is a copy of:
 * its components and values, `base`'s placement, and `behaviors`. */
prototype *prototype_from_instance(
    const char *name,
    const prototype *base,
    const ecs_world *world,
    ecs_entity entity,
    const char *const *behaviors,
    int behavior_count
);

#endif
