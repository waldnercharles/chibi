#ifndef CHIBI_GAME_SCENE_DOCUMENT_H
#define CHIBI_GAME_SCENE_DOCUMENT_H

#include "ecs/component_catalog.h"
#include "runtime/scenes/physics_settings.h"
#include "support/asset_value.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    char *name;
    char *prototype;
    av_value *components;
    av_value *retained;
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
/* Shared by document edits and Studio's live actors. name_used excludes the
 * object being renamed. Returns the user-facing rejection in error. */
int game_scene_entity_validate_name(const char *name, bool name_used, char *error, size_t error_size);
int game_scene_document_rename_entity(
    game_scene_document *document,
    int index,
    const char *name,
    char *error,
    size_t error_size
);
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
int game_scene_document_set_entity_components(game_scene_document *document, int index, av_value *components);
/* Serialization borrows the supplied component descriptions for precision,
 * color and enum formatting. Unknown components retain their authored values;
 * an empty catalog preserves all component values without reflected formatting.
 * Returned values/text own their storage and outlive the catalog/document. */
av_value *game_scene_document_to_value(const game_scene_document *document, ecs_component_catalog components);
game_scene_document *game_scene_document_load_value(av_value *value, char *error, size_t error_size);
char *game_scene_document_save(const game_scene_document *document, ecs_component_catalog components);
int game_scene_document_set_entity_retained(game_scene_document *document, int index, av_value *value);
game_scene_document *game_scene_document_load(const char *json, char *error, size_t error_size);

#endif
