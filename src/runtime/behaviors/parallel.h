#pragma once
#include "graph/runtime/update.h"
#include "runtime/behaviors/behavior.h"
#include "runtime/behaviors/entities.h"

typedef struct game_behavior_parallel game_behavior_parallel;
game_behavior_parallel *game_behavior_parallel_new();
void game_behavior_parallel_free(game_behavior_parallel *parallel);
void game_behavior_parallel_invalidate(game_behavior_parallel *parallel);
void game_behavior_parallel_prepare(
    game_behavior_parallel *parallel,
    const behavior_update_row *rows,
    const behavior_update_span *spans,
    uint32_t slots
);
bool game_behavior_parallel_run(
    game_behavior_parallel *parallel,
    int span,
    const ecs_execution_view *world,
    game_entities *entities,
    game_behavior_update_stats *stats
);
