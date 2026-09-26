#ifndef CHIBI_GAME_SCENE_DOCUMENT_H
#define CHIBI_GAME_SCENE_DOCUMENT_H

#include "runtime/scenes/physics_settings.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    char *name;
    char *prototype;
    char *components_json;
    char *retained_json;
    char **behaviors;
} game_scene_entity;

typedef struct game_scene_document
{
    char *name;
    char *ui; /* the root ui document its UI shows on (render/ui.h), or NULL for the project's */
    game_scene_entity *entities;
    bool perspective;
    game_physics_settings physics;
} game_scene_document;

game_scene_document *game_scene_document_new(const char *name);
void game_scene_document_free(game_scene_document *document);
int game_scene_document_set_perspective(game_scene_document *document, bool perspective);
/* Names the scene's root ui document; NULL or "" leaves the project's. */
void game_scene_document_set_ui(game_scene_document *document, const char *ui);
int game_scene_document_entity_index(const game_scene_document *document, const char *name);
int game_scene_document_add_entity(
    game_scene_document *document,
    const char *name,
    const char *prototype,
    const char *const *behaviors,
    int behavior_count
);
int game_scene_document_remove_entity(game_scene_document *document, int index);
int game_scene_document_set_entity_prototype(game_scene_document *document, int index, const char *prototype);
int game_scene_document_set_entity_behaviors(
    game_scene_document *document,
    int index,
    const char *const *behaviors,
    int behavior_count
);
int game_scene_document_set_entity_components(
    game_scene_document *document,
    int index,
    const char *components_json
);
char *game_scene_document_save(const game_scene_document *document);
int game_scene_document_set_entity_retained(game_scene_document *document, int index, const char *json);
game_scene_document *game_scene_document_load(const char *json, char *error, size_t error_size);

#endif
