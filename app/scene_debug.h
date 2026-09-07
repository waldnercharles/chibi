#ifndef CBLUEPRINT_SCENE_DEBUG_H
#define CBLUEPRINT_SCENE_DEBUG_H

#include "game/behavior.h"

typedef struct
{
    ecs_entity entity;
    uint64_t sequence;
    uint64_t session;
    char *asset_name;
} scene_debug;

void scene_debug_clear(scene_debug *debug);
void scene_debug_select(scene_debug *debug, const game_behavior_attachment_info *info, uint64_t session);
bool scene_debug_resolve(
    const scene_debug *debug,
    ecs_world *world,
    game_behavior_world *behaviors,
    uint64_t session,
    game_behavior_attachment_info *out
);
bool scene_debug_matches(const behavior_asset *working, const behavior_asset *applied);

#endif
