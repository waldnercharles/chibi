#include "authoring/scene_prepare.h"

#include "authoring/prototype_prepare.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

game_scene *authoring_scene_prepare(
    const game_scene_document *document,
    const game_prototype *const *objects,
    int count,
    char *error,
    size_t size
)
{
    assert(document && count >= 0 && (!count || objects) && (!size || error));
    if (count != acount(document->entities)) {
        if (size) {
            snprintf(error, size, "prepared scene object count differs from the document");
        }
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        assert(objects[i]);
        if (!authoring_prototype_resources(objects[i], error, size)) {
            return NULL;
        }
    }
    /* Instances' children follow the document's objects, keeping their
     * indices. */
    authoring_objects records = { 0 };
    for (int i = 0; i < count; i++) {
        const game_scene_entity *entity = &document->entities[i];
        authoring_objects_add(&records, objects[i], entity->name, entity->parent);
    }
    for (int i = 0; i < count; i++) {
        authoring_objects_add_children(&records, objects[i], i);
    }
    game_scene_spec spec = {
        .name = document->name,
        .ui = document->ui,
        .perspective = document->perspective,
        .physics = document->physics,
    };
    return authoring_objects_prepare(&records, spec, error, size);
}
