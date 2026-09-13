#include "runtime/task_pool.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

enum
{
    JOBS = 96,
    ITEMS = 4099
};
typedef struct
{
    int *values;
    int seed, workers;
} task_fixture;

static void fill_range(int start, int end, uint32_t worker, void *user)
{
    task_fixture *fixture = user;
    assert(start >= 0 && start < end && end <= ITEMS);
    assert(worker < (uint32_t)fixture->workers);
    for (int i = start; i < end; i++) {
        fixture->values[i] = (i + 1) * fixture->seed;
    }
}

void test_task_pool()
{
    game_task_pool *pool = game_task_pool_acquire();
    game_task_pool *other = game_task_pool_acquire();
    assert(pool == other);
    game_task_system tasks = *game_task_pool_tasks(pool);
    bool started = game_task_pool_started(pool);
    int *values = xcalloc((size_t)JOBS * ITEMS, sizeof *values);
    task_fixture fixtures[JOBS];
    void *handles[JOBS];

    game_task_pool_begin(pool, false);
    task_fixture serial = { values, 3, 1 };
    assert(!tasks.enqueue(fill_range, ITEMS, 64, &serial, tasks.user));
    game_task_pool_end(pool);
    assert(game_task_pool_started(pool) == started);
    for (int i = 0; i < ITEMS; i++) {
        assert(values[i] == (i + 1) * 3);
    }
    game_task_pool_release(other);

    /* Exceed retained task slots, join in reverse order, and verify every
     * range is complete before its argument storage is reused or freed. */
    game_task_pool_begin(pool, true);
    for (int i = 0; i < JOBS; i++) {
        fixtures[i] = (task_fixture){ values + i * ITEMS, i + 5, tasks.workers };
        handles[i] = tasks.enqueue(fill_range, ITEMS, 64, &fixtures[i], tasks.user);
    }
    for (int i = JOBS - 1; i >= 0; i--) {
        tasks.finish(handles[i], tasks.user);
        for (int j = 0; j < ITEMS; j++) {
            assert(fixtures[i].values[j] == (j + 1) * fixtures[i].seed);
        }
    }
    game_task_pool_end(pool);
    assert(game_task_pool_started(pool) == (tasks.workers > 1));

    game_task_pool_begin(pool, false);
    assert(!tasks.enqueue(fill_range, ITEMS, 64, &serial, tasks.user));
    game_task_pool_end(pool);
    free(values);
    game_task_pool_release(pool);
}
