#ifndef CBLUEPRINT_GAME_LIVE_H
#define CBLUEPRINT_GAME_LIVE_H

#include "behavior.h"

#include <stddef.h>

typedef struct game_live_world game_live_world;
typedef struct game_live_definition game_live_definition;

game_live_definition *game_live_definition_new(const char *asset_name, blueprint *working);
void game_live_definition_free(game_live_definition *definition);
blueprint *game_live_working(game_live_definition *definition);
int game_live_sync_working(game_live_definition *definition, const blueprint *source);
const blueprint *game_live_applied(const game_live_definition *definition);
const char *game_live_definition_name(const game_live_definition *definition);
int game_live_definition_dirty(const game_live_definition *definition);
int game_live_mark_dirty(game_live_definition *definition);

game_live_world *game_live_world_new(ecs_world *ecs);
void game_live_world_free(game_live_world *world);
int game_live_add_definition(game_live_world *world, game_live_definition *definition);
int game_live_apply(game_live_world *world, game_live_definition *definition, char *message, size_t message_size);
game_behavior_world *game_live_behaviors(game_live_world *world);

#endif
