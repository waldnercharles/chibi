#pragma once

#include "runtime/tasks.h"

#include <stdbool.h>

typedef struct task_pool task_pool;

/* Runtime worlds share one pool on their controlling thread. Native workers
 * start on demand; threaded web builds prestart their configured pool.
 * The last release joins and destroys the pool. */
task_pool *task_pool_acquire();
/* Pools are shared by effective worker budget. Zero selects the default;
 * native budgets are capped to hardware. Serial web builds use one; threaded
 * web builds share their configured parallel budget or run explicitly serial. */
task_pool *task_pool_acquire_workers(int workers);
void task_pool_release(task_pool *pool);
const task_system *task_pool_tasks(const task_pool *pool);
bool task_pool_started(const task_pool *pool);

/* Only one submission scope may be open, and all its tasks must be finished
 * before ending it. Small workloads execute inline without waking workers. */
void task_pool_begin(task_pool *pool, bool parallel);
void task_pool_end(task_pool *pool);
