#include "runtime/task_pool.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#ifdef CHIBI_HAS_ENKITS
#include <TaskScheduler_c.h>
#endif

enum
{
    TASK_CAPACITY = 64,
    WORKER_LIMIT = 4
};

#ifdef CHIBI_HAS_ENKITS
typedef struct
{
    enkiTaskSet *set;
    game_task_range *run;
    void *user;
    bool active;
} pool_task;
#endif

struct game_task_pool
{
    game_task_system tasks;
    int references, pending;
    bool open, parallel, started;
#ifdef CHIBI_HAS_ENKITS
    enkiTaskScheduler *scheduler;
    pool_task slots[TASK_CAPACITY];
#endif
};

static game_task_pool *shared;

#ifdef CHIBI_HAS_ENKITS
static void run_range(uint32_t start, uint32_t end, uint32_t worker, void *user)
{
    pool_task *task = user;
    task->run((int)start, (int)end, worker, task->user);
}

static void start_workers(game_task_pool *pool)
{
    if (pool->started) {
        return;
    }
    enkiInitTaskSchedulerNumThreads(pool->scheduler, (uint32_t)pool->tasks.workers);
    for (int i = 0; i < TASK_CAPACITY; i++) {
        pool->slots[i].set = enkiCreateTaskSet(pool->scheduler, run_range);
    }
    pool->started = true;
}
#endif

static void *enqueue(game_task_range *run, int count, int minimum, void *context, void *user)
{
    game_task_pool *pool = user;
    assert(pool->open && count > 0 && minimum > 0);
#ifdef CHIBI_HAS_ENKITS
    if (pool->parallel) {
        start_workers(pool);
        assert(enkiGetThreadNum(pool->scheduler) == 0);
        for (int i = 0; i < TASK_CAPACITY; i++) {
            pool_task *task = &pool->slots[i];
            if (task->active) {
                continue;
            }
            *task = (pool_task){ .set = task->set, .run = run, .user = context, .active = true };
            pool->pending++;
            enkiAddTaskSetMinRange(pool->scheduler, task->set, task, (uint32_t)count, (uint32_t)minimum);
            return task;
        }
    }
#endif
    /* Box2D explicitly permits inline completion. Bound outstanding task
     * storage even if a caller submits more than the retained capacity. */
    run(0, count, 0, context);
    return NULL;
}

static void finish(void *handle, void *user)
{
    game_task_pool *pool = user;
    assert(pool->open);
    if (!handle) {
        return;
    }
#ifdef CHIBI_HAS_ENKITS
    pool_task *task = handle;
    assert(task->active && pool->pending > 0);
    enkiWaitForTaskSet(pool->scheduler, task->set);
    task->active = false;
    pool->pending--;
#else
    assert(!"Serial tasks finish during enqueue");
#endif
}

game_task_pool *game_task_pool_acquire()
{
    if (shared) {
        shared->references++;
        return shared;
    }
    shared = xcalloc(1, sizeof *shared);
    shared->references = 1;
    int workers = 1;
#ifdef CHIBI_HAS_ENKITS
    shared->scheduler = enkiNewTaskScheduler();
    struct enkiTaskSchedulerConfig config = enkiGetTaskSchedulerConfig(shared->scheduler);
    workers = config.numTaskThreadsToCreate >= WORKER_LIMIT - 1
                ? WORKER_LIMIT
                : (int)config.numTaskThreadsToCreate + 1;
#endif
    shared->tasks = (game_task_system){ workers, shared, enqueue, finish };
    return shared;
}

void game_task_pool_release(game_task_pool *pool)
{
    assert(pool == shared && pool->references > 0 && !pool->open && !pool->pending);
    if (--pool->references) {
        return;
    }
#ifdef CHIBI_HAS_ENKITS
    if (pool->started) {
        for (int i = 0; i < TASK_CAPACITY; i++) {
            enkiDeleteTaskSet(pool->scheduler, pool->slots[i].set);
        }
    }
    enkiDeleteTaskScheduler(pool->scheduler);
#endif
    free(pool);
    shared = NULL;
}

const game_task_system *game_task_pool_tasks(const game_task_pool *pool)
{
    return &pool->tasks;
}

bool game_task_pool_started(const game_task_pool *pool)
{
    return pool->started;
}

void game_task_pool_begin(game_task_pool *pool, bool parallel)
{
    assert(!pool->open && !pool->pending);
    pool->open = true;
    pool->parallel = parallel && pool->tasks.workers > 1;
}

void game_task_pool_end(game_task_pool *pool)
{
    assert(pool->open && !pool->pending);
    pool->open = false;
    pool->parallel = false;
}
