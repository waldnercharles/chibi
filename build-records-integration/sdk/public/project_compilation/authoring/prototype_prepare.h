#ifndef CHIBI_AUTHORING_PROTOTYPE_PREPARE_H
#define CHIBI_AUTHORING_PROTOTYPE_PREPARE_H

#include "assets/prototype.h"
#include "runtime/scenes/scene.h"

typedef struct entities entities;
/* Prepared objects being gathered, borrowing the prototypes' data. */
typedef struct
{
    scene_object *objects;
    scene_component **components;
    int *parents;
    scene_link *links;
    ecs_relationship_desc *relationships;
    scene_pair *pairs;
    const prototype_pair *missing_pair;
    bool relationship_conflict;
    const prototype_link *missing; /* a link naming no entity, if any */
} authoring_objects;
/* Appends the prototype as one object, without its children. */
void authoring_objects_add(authoring_objects *, const prototype *, const char *name, int parent);
/* Appends the prototype's children under the object at index, each followed
 * by its own, linking the paths it and its document name. */
void authoring_objects_add_children(authoring_objects *, const prototype *, int index);
/* Links the object's `named` paths of the given kind to the document's
 * entities, which became the objects at placed[i], or at i when NULL. */
void authoring_objects_add_links(
    authoring_objects *,
    int object,
    const prototype *named,
    bool outer,
    const scene_document *document,
    const int *placed
);
/* Prepares the gathered objects as the spec's and releases them. */
scene *authoring_objects_prepare(authoring_objects *, scene_spec spec, char *error, size_t size);
/* Snapshot resolved authoring data; the caller owns the prepared generation. */
/* Checks the resources of the prototype and its children. */
bool authoring_prototype_resources(const prototype *, char *error, size_t size);
scene *authoring_prototype_prepare(const prototype *, char *error, size_t size);
int entities_register(entities *, const prototype *);

#endif
