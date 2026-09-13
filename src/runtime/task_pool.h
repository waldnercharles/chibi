#pragma once

#include "runtime/tasks.h"

#include <stdbool.h>

typedef struct game_task_pool game_task_pool;

/* Runtime worlds share one pool on their controlling thread. Acquiring it
 * creates no workers. The last release joins and destroys the pool. */
game_task_pool *game_task_pool_acquire();
void game_task_pool_release(game_task_pool *pool);
const game_task_system *game_task_pool_tasks(const game_task_pool *pool);
bool game_task_pool_started(const game_task_pool *pool);

/* Only one submission scope may be open, and all its tasks must be finished
 * before ending it. Small workloads execute inline without waking workers. */
void game_task_pool_begin(game_task_pool *pool, bool parallel);
void game_task_pool_end(game_task_pool *pool);
