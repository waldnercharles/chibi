#include "runtime/task_pool.h"

#include "diagnostics/tracy.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#ifdef CHIBI_HAS_ENKITS
#include <TaskScheduler_c.h>
#endif

enum
{
    TASK_CAPACITY = 64,
    DEFAULT_WORKERS = 8
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

static game_task_pool *shared[GAME_TASK_WORKER_MAX + 1];

#ifdef CHIBI_HAS_ENKITS
static int default_workers(int available)
{
    int preferred = available;
#ifdef __APPLE__
    int cores = 0;
    size_t size = sizeof cores;
    int result = sysctlbyname("hw.perflevel0.physicalcpu", &cores, &size, NULL, 0);
    if (result != 0) {
        size = sizeof cores;
        result = sysctlbyname("hw.physicalcpu", &cores, &size, NULL, 0);
    }
    if (result == 0 && cores > 0) {
        preferred = cores;
    }
#endif
    return preferred < DEFAULT_WORKERS ? preferred : DEFAULT_WORKERS;
}
#endif

#ifdef CHIBI_HAS_ENKITS
static void run_range(uint32_t start, uint32_t end, uint32_t worker, void *user)
{
    pool_task *task = user;

#ifdef CHIBI_ENABLE_TRACY
    static _Thread_local bool named;
    if (!named) {
        char name[32];
        snprintf(name, sizeof name, "Chibi worker %u", worker);
        CHIBI_TRACE_SET_THREAD_NAME(name);
        named = true;
    }
#endif
    CHIBI_TRACE_ZONE_BEGIN(zone, "Task range");
    task->run((int)start, (int)end, worker, task->user);
    CHIBI_TRACE_ZONE_END(zone);
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
            CHIBI_TRACE_ZONE_BEGIN(zone, "Task submit");
            enkiAddTaskSetMinRange(pool->scheduler, task->set, task, (uint32_t)count, (uint32_t)minimum);
            CHIBI_TRACE_ZONE_END(zone);
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
    CHIBI_TRACE_ZONE_BEGIN(zone, "Task wait");
    enkiWaitForTaskSet(pool->scheduler, task->set);
    CHIBI_TRACE_ZONE_END(zone);
    task->active = false;
    pool->pending--;
#else
    assert(!"Serial tasks finish during enqueue");
#endif
}

game_task_pool *game_task_pool_acquire()
{
    return game_task_pool_acquire_workers(0);
}

game_task_pool *game_task_pool_acquire_workers(int requested)
{
    assert(requested >= 0 && requested <= GAME_TASK_WORKER_MAX);
    int workers = 1;
#ifdef CHIBI_HAS_ENKITS
    enkiTaskScheduler *scheduler = enkiNewTaskScheduler();
    struct enkiTaskSchedulerConfig config = enkiGetTaskSchedulerConfig(scheduler);
    int available = (int)config.numTaskThreadsToCreate + 1;
    workers = requested ? requested : default_workers(available);
    if (config.numTaskThreadsToCreate < (uint32_t)(workers - 1)) {
        workers = (int)config.numTaskThreadsToCreate + 1;
    }
#endif
    if (shared[workers]) {
#ifdef CHIBI_HAS_ENKITS
        enkiDeleteTaskScheduler(scheduler);
#endif
        shared[workers]->references++;
        return shared[workers];
    }
    game_task_pool *pool = xcalloc(1, sizeof *pool);
    pool->references = 1;
#ifdef CHIBI_HAS_ENKITS
    pool->scheduler = scheduler;
#endif
    pool->tasks = (game_task_system){ workers, pool, enqueue, finish };
    shared[workers] = pool;
    return pool;
}

void game_task_pool_release(game_task_pool *pool)
{
    int workers = pool->tasks.workers;
    assert(pool == shared[workers] && pool->references > 0 && !pool->open && !pool->pending);
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
    shared[workers] = NULL;
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
