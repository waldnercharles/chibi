#ifndef CBLUEPRINT_GAME_SCENE_DOCUMENT_H
#define CBLUEPRINT_GAME_SCENE_DOCUMENT_H

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
    game_scene_entity *entities;
} game_scene_document;

game_scene_document *game_scene_document_new(const char *name);
void game_scene_document_free(game_scene_document *document);
int game_scene_document_add_entity(
    game_scene_document *document,
    const char *name,
    const char *prototype,
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
