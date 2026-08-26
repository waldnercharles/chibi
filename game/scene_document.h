#ifndef CBLUEPRINT_GAME_SCENE_DOCUMENT_H
#define CBLUEPRINT_GAME_SCENE_DOCUMENT_H

#include <stddef.h>

typedef struct
{
    char *name;
    char *prototype;
    char *components_json;
    char **behaviors;
    char **behavior_additions;
    char **behavior_removals;
    int has_behavior_overrides;
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
int game_scene_document_set_entity_behavior_overrides(
    game_scene_document *document,
    int entity,
    const char *const *additions,
    int addition_count,
    const char *const *removals,
    int removal_count
);
char *game_scene_document_save(const game_scene_document *document);
game_scene_document *game_scene_document_load(const char *json, char *error, size_t error_size);

#endif
