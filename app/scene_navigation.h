#ifndef CBLUEPRINT_APP_SCENE_NAVIGATION_H
#define CBLUEPRINT_APP_SCENE_NAVIGATION_H

#include "../editor/editor.h"
#include "../engine/engine.h"

typedef struct scene_navigation scene_navigation;

/* Inputs borrow strings; history owns deep copies of strings and graph selection. */
typedef struct
{
    int kind;
    const char *document;
    const char *scene_path;
    const char *system_path;
    const char *prototype;
    const char *content;
    const char *attachment;
    ecs_entity entity;
    int tab;
    editor_location graph;
    engine_viewport_view camera;
} scene_location;

scene_navigation *scene_navigation_new();
void scene_navigation_free(scene_navigation *navigation);
void scene_navigation_record(scene_navigation *navigation, const scene_location *location);
bool scene_navigation_can_move(const scene_navigation *navigation, bool forward);
const scene_location *scene_navigation_step(scene_navigation *navigation, bool forward);
void scene_navigation_discard_current(scene_navigation *navigation, bool forward);
#endif
