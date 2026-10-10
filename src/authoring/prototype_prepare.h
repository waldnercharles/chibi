#ifndef CHIBI_AUTHORING_PROTOTYPE_PREPARE_H
#define CHIBI_AUTHORING_PROTOTYPE_PREPARE_H

#include "assets/prototype.h"
#include "runtime/scenes/scene.h"

typedef struct game_entities game_entities;
/* Prepared objects being gathered, borrowing the prototypes' data. */
typedef struct
{
    game_scene_object *objects;
    game_scene_component **components;
    int *parents;
} authoring_objects;
/* Appends the prototype as one object, without its children. */
void authoring_objects_add(authoring_objects *, const game_prototype *, const char *name, int parent);
/* Appends the prototype's children under the object at index, each followed
 * by its own. */
void authoring_objects_add_children(authoring_objects *, const game_prototype *, int index);
/* Prepares the gathered objects as the spec's and releases them. */
game_scene *authoring_objects_prepare(authoring_objects *, game_scene_spec spec, char *error, size_t size);
/* Snapshot resolved authoring data; the caller owns the prepared generation. */
/* Checks the resources of the prototype and its children. */
bool authoring_prototype_resources(const game_prototype *, char *error, size_t size);
game_scene *authoring_prototype_prepare(const game_prototype *, char *error, size_t size);
int game_entities_register(game_entities *, const game_prototype *);

#endif
