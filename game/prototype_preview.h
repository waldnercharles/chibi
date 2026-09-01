#ifndef CBLUEPRINT_GAME_PROTOTYPE_PREVIEW_H
#define CBLUEPRINT_GAME_PROTOTYPE_PREVIEW_H

#include "behavior.h"
#include "prototype.h"

typedef struct game_prototype_preview game_prototype_preview;

game_prototype_preview *game_prototype_preview_new(const game_prototype_registry *registry);
void game_prototype_preview_free(game_prototype_preview *preview);
int game_prototype_preview_register_behavior(
    game_prototype_preview *preview,
    const char *asset_name,
    const behavior_asset *source
);
int game_prototype_preview_launch(
    game_prototype_preview *preview,
    const game_prototype *prototype,
    char *error,
    size_t error_size
);
void game_prototype_preview_stop(game_prototype_preview *preview);
int game_prototype_preview_tick(game_prototype_preview *preview, double monotonic_time);
ecs_world *game_prototype_preview_world(game_prototype_preview *preview);
game_behavior_world *game_prototype_preview_behaviors(game_prototype_preview *preview);
ecs_entity game_prototype_preview_entity(const game_prototype_preview *preview);
behavior_instance *game_prototype_preview_instance(game_prototype_preview *preview, const char *asset_name);

#endif
