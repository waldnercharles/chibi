#ifndef CHIBI_SCENE_RETENTION_H
#define CHIBI_SCENE_RETENTION_H

#include "assets/prototype.h"

#include <stdbool.h>

typedef struct
{
    char *name;
    char *values_json;
    char *defaults_json;
    bool automatic;
} scene_retained_component;

scene_retained_component *scene_retention_find(scene_retained_component *components, const char *name);
void scene_retention_remove(scene_retained_component **components, const char *name);
void scene_retention_free(scene_retained_component *components);
char *scene_retention_save(const scene_retained_component *components);
/* Atomic replacement. NULL JSON means no retained components. */
int scene_retention_load(const char *json, scene_retained_component **components);
bool scene_retention_validate(
    const scene_retained_component *components,
    const game_prototype_registry *registry
);

#endif
