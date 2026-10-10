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
    game_scene_object *records = xcalloc((size_t)count, sizeof *records);
    game_scene_component **components = xcalloc((size_t)count, sizeof *components);
    int *parents = xcalloc((size_t)count, sizeof *parents);
    unsigned char tag = 1;
    for (int i = 0; i < count; i++) {
        assert(objects[i]);
        const game_prototype *prototype = objects[i];
        for (int j = 0; j < acount(prototype->components); j++) {
            const game_prototype_component *component = &prototype->components[j];
            apush(
                components[i],
                ((game_scene_component){ component->desc, component->data })
            );
        }
        apush(components[i], ((game_scene_component){ game_prototype_tag_desc(prototype), &tag }));
        parents[i] = document->entities[i].parent;
        records[i] = (game_scene_object){
            .name = document->entities[i].name,
            .components = components[i],
            .component_count = acount(components[i]),
            .behaviors = (const char *const *)prototype->graphs,
            .behavior_count = acount(prototype->graphs),
        };
    }
    game_scene_spec spec = {
        .name = document->name,
        .ui = document->ui,
        .perspective = document->perspective,
        .physics = document->physics,
        .objects = records,
        .object_count = count,
        .parents = parents,
    };
    game_scene *scene = game_scene_prepare(&spec, error, size);
    for (int i = 0; i < count; i++) {
        afree(components[i]);
    }
    free(components);
    free(parents);
    free(records);
    return scene;
}
