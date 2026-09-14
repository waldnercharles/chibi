#ifndef CHIBI_GAME_LIVE_H
#define CHIBI_GAME_LIVE_H

#include "render/particles.h"
#include "runtime/behaviors/behavior.h"
#include "runtime/behaviors/entities.h"
#include "runtime/scenes/gameplay.h"
#include "runtime/scenes/physics.h"
#include "runtime/visibility.h"

#include <stddef.h>

typedef struct game_live_world game_live_world;

typedef struct game_live_definition
{
    char *name;
    char *path;
    behavior_asset *working;
    behavior_asset *applied;
    game_live_world *world;
    int registered;
    int dirty; /* Working edits not yet running. */
    bool unsaved;
    compile_report diagnostics;
} game_live_definition;

struct game_live_world
{
    ecs_world *ecs;
    game_behavior_world *behaviors;
    game_entities *entities;
    game_physics *physics;
    game_particles *particles;
    game_visibility *visibility;
    game_live_definition **definitions;
};

game_live_definition *game_live_definition_new(const char *asset_name, behavior_asset *working);
void game_live_definition_free(game_live_definition *definition);
game_live_definition *game_live_find_definition(game_live_world *world, const char *asset_name);
const char *game_live_definition_path(const game_live_definition *definition);
int game_live_definition_set_path(game_live_definition *definition, const char *path);
int game_live_definition_count(const game_live_world *world);
game_live_definition *game_live_definition_at(game_live_world *world, int index);
int game_live_sync_working(game_live_definition *definition, const behavior_asset *source);
int game_live_mark_dirty(game_live_definition *definition);

game_live_world *game_live_world_new(ecs_world *ecs);
void game_live_world_free(game_live_world *world);
int game_live_add_definition(game_live_world *world, game_live_definition *definition);
int game_live_remove_definition(game_live_world *world, const char *asset_name);
int game_live_duplicate_definition(
    game_live_world *world,
    const char *asset_name,
    const behavior_asset *source
);
int game_live_can_preview_library(
    game_live_world *world,
    game_live_definition *definition,
    char *message,
    size_t size
);
int game_live_apply(game_live_world *world, game_live_definition *definition, char *message, size_t message_size);

/* Returns INSTANCE_UPDATE_* or -1 for invalid edits. Never recreates instances. */
int game_live_preview(game_live_world *world, game_live_definition *definition, char *message, size_t message_size);

#endif
