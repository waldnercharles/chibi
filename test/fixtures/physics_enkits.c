/* Standalone benchmark adapter; compile against the pinned EnkiTS source.
 * No dependency on this executor is added to the runtime build by the probe. */
#include "runtime/tasks.h"

#include <TaskScheduler_c.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

enum
{
    TASKS = 64
};
typedef struct
{
    enkiTaskSet *set;
    game_task_range *run;
    void *user;
    bool active;
} task;
typedef struct
{
    enkiTaskScheduler *scheduler;
    task tasks[TASKS];
} pool;

static void run(uint32_t start, uint32_t end, uint32_t worker, void *user)
{
    task *t = user;
    t->run((int)start, (int)end, worker, t->user);
}
static void *enqueue(game_task_range *fn, int count, int minimum, void *context, void *user)
{
    pool *p = user;
    for (int i = 0; i < TASKS; i++) {
        task *t = &p->tasks[i];
        if (t->active) {
            continue;
        }
        t->active = true;
        t->run = fn;
        t->user = context;
        enkiAddTaskSetMinRange(p->scheduler, t->set, t, (uint32_t)count, (uint32_t)minimum);
        return t;
    }
    assert(!"too many simultaneous physics tasks");
    abort();
}
static void finish(void *handle, void *user)
{
    if (!handle) {
        return;
    }
    pool *p = user;
    task *t = handle;
    enkiWaitForTaskSet(p->scheduler, t->set);
    t->active = false;
}
void physics_executor_create(game_task_system *out, int workers)
{
    assert(workers > 0 && workers <= 32);
    pool *p = calloc(1, sizeof *p);
    assert(p);
    p->scheduler = enkiNewTaskScheduler();
    enkiInitTaskSchedulerNumThreads(p->scheduler, (uint32_t)workers);
    for (int i = 0; i < TASKS; i++) {
        p->tasks[i].set = enkiCreateTaskSet(p->scheduler, run);
    }
    *out = (game_task_system){ workers, p, enqueue, finish };
}
void physics_executor_free(void *user)
{
    pool *p = user;
    for (int i = 0; i < TASKS; i++) {
        assert(!p->tasks[i].active);
        enkiDeleteTaskSet(p->scheduler, p->tasks[i].set);
    }
    enkiDeleteTaskScheduler(p->scheduler);
    free(p);
}
