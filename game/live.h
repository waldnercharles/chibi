#ifndef CBLUEPRINT_GAME_LIVE_H
#define CBLUEPRINT_GAME_LIVE_H

#include "behavior.h"

#include <stddef.h>

typedef struct game_live_world game_live_world;

typedef struct game_live_definition
{
    char *name;
    char *path;
    blueprint *working;
    blueprint *applied;
    game_live_world *world;
    int registered;
    int dirty;
    compile_report diagnostics;
} game_live_definition;

struct game_live_world
{
    ecs_world *ecs;
    game_behavior_world *behaviors;
    game_live_definition **definitions;
};

game_live_definition *game_live_definition_new(const char *asset_name, blueprint *working);
void game_live_definition_free(game_live_definition *definition);
game_live_definition *game_live_find_definition(game_live_world *world, const char *asset_name);
const char *game_live_definition_path(const game_live_definition *definition);
int game_live_definition_set_path(game_live_definition *definition, const char *path);
int game_live_definition_count(const game_live_world *world);
game_live_definition *game_live_definition_at(game_live_world *world, int index);
int game_live_sync_working(game_live_definition *definition, const blueprint *source);
int game_live_mark_dirty(game_live_definition *definition);

game_live_world *game_live_world_new(ecs_world *ecs);
void game_live_world_free(game_live_world *world);
int game_live_add_definition(game_live_world *world, game_live_definition *definition);
int game_live_duplicate_definition(game_live_world *world, const char *asset_name, const blueprint *source);
int game_live_apply(game_live_world *world, game_live_definition *definition, char *message, size_t message_size);

#endif
