#ifndef CHIBI_SCENE_DEBUG_H
#define CHIBI_SCENE_DEBUG_H

#include "runtime/behaviors/behavior.h"
#include "runtime/behaviors/live.h"

typedef struct
{
    ecs_entity entity;
    uint64_t sequence;
    uint64_t session;
    char *asset_name;
    struct
    {
        const behavior_asset *working;
        const behavior_asset *applied;
        unsigned revision;
        bool valid;
        bool matches;
    } comparison;
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
/* Whether two linked snapshots execute alike; layout aside. */
bool scene_debug_matches(const behavior_asset *linked, const behavior_asset *applied);
/* Whether applied runs working: working linked as applying it would link it
   (live's definitions, lowered component nodes) matches applied. */
bool scene_debug_runs(game_live_world *live, const behavior_asset *working, const behavior_asset *applied);
bool scene_debug_runs_cached(
    scene_debug *debug,
    game_live_world *live,
    const behavior_asset *working,
    unsigned revision,
    const behavior_asset *applied
);

#endif
