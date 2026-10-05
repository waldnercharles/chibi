#ifndef CHIBI_AUTOMATION_PROTOTYPE_INSTANCE_H
#define CHIBI_AUTOMATION_PROTOTYPE_INSTANCE_H

#include "assets/prototype.h"
#include "automation/controller.h"

#include <stddef.h>

typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;
typedef struct yyjson_val yyjson_val;

/* buildPrototype's source_instance, Studio's Apply Overrides to Prototype and
 * Save as New Prototype for agents (assets/prototype_instance.h): a scene
 * file's entity, composed in a world of its own from the prototype it is an
 * instance of and its overrides. */
typedef struct
{
    const char *scene_text;     /* the scene the entity is in */
    const char *entity;         /* its name */
    const char *prototype_text; /* the prototype it is an instance of */
    const char *name;           /* a new prototype's, or NULL to apply */
    yyjson_val *fields; /* [{component, field}] to apply; NULL for all */
} automation_instance;

/* The name of the prototype `entity` of `scene_text` is an instance of, in
 * `out`. */
int automation_instance_prototype(
    const char *scene_text,
    const char *entity,
    char *out,
    size_t size,
    char *error,
    size_t error_size
);
/* The prototype text an edit starts from: with `name`, a new prototype of the
 * whole instance; else its prototype with the fields the entity overrides,
 * or the ones `fields` names, applied, listed in `result` as applied_fields.
 * The scene is left alone: buildScene's set_entity_prototype makes the
 * entity an instance of a new prototype. */
int automation_prototype_from_instance(
    const automation_instance *instance,
    const game_prototype_registry *registry,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **text,
    char *error,
    size_t error_size
);

#endif
