#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "parallel.h"

#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef CHIBI_TEST_ENKITS
#include <TaskScheduler_c.h>
#endif

enum
{
    MAX_WORKERS = 8,
    BATCH_SIZE = 64
};

typedef struct
{
    const char *name;
    pu_test_fn run;
    pu_test_context context;
} test_job;

static test_job jobs[BATCH_SIZE];
static int count, workers = 1, requested;
static bool scope_open;
static _Thread_local bool running;
#ifdef CHIBI_TEST_ENKITS
static enkiTaskScheduler *scheduler;
static enkiTaskSet *tasks[BATCH_SIZE];
#endif

static double seconds()
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        abort();
    }
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void run_job(test_job *job)
{
    assert(!running);
    running = true;
    pu_run_test_context(&job->context, job->name, job->run);
    running = false;
}

#ifdef CHIBI_TEST_ENKITS
static void run_range(uint32_t start, uint32_t end, uint32_t worker, void *user)
{
    assert(start == 0 && end == 1);
    (void)start;
    (void)end;
    (void)worker;
    run_job(user);
}
#endif

static void replay(FILE *source, FILE *destination)
{
    assert(source);
    if (ferror(source) || fflush(source) || fseek(source, 0, SEEK_SET)) {
        abort();
    }
    char buffer[4096];
    size_t size;
    while ((size = fread(buffer, 1, sizeof buffer, source))) {
        if (fwrite(buffer, 1, size, destination) != size) {
            abort();
        }
    }
    if (ferror(source) || fclose(source) || fflush(destination)) {
        abort();
    }
}

static void flush()
{
    if (!count) {
        return;
    }
#ifdef CHIBI_TEST_ENKITS
    if (workers > 1) {
        for (int i = 0; i < count; i++) {
            enkiAddTaskSetMinRange(scheduler, tasks[i], &jobs[i], 1, 1);
        }
        for (int i = 0; i < count; i++) {
            enkiWaitForTaskSet(scheduler, tasks[i]);
        }
    } else
#endif
    {
        for (int i = 0; i < count; i++) {
            run_job(&jobs[i]);
        }
    }
    for (int i = 0; i < count; i++) {
        pu_test_context *context = &jobs[i].context;
        replay(context->output, pu_output());
        replay(context->errors, pu_errors());
        pu_merge_context(context);
    }
    count = 0;
}

static void enqueue(const char *name, pu_test_fn run, void *user)
{
    (void)user;
    assert(scope_open && !running);
    if (count == BATCH_SIZE) {
        flush();
    }
    test_job *job = &jobs[count++];
    *job = (test_job){ .name = name, .run = run, .context = pu_capture_context() };
    job->context.output = tmpfile();
    job->context.errors = tmpfile();
    if (!job->context.output || !job->context.errors) {
        fputs("Cannot create isolated test output\n", stderr);
        abort();
    }
}

bool test_parallel_init()
{
    pu_set_clock(seconds);
    const char *value = getenv("CHIBI_TEST_THREADS");
    if (!value) {
        return true;
    }
    /* Deliberately strict: zero, signs, whitespace and trailing text are not
     * silently interpreted as another execution mode. */
    if (strlen(value) != 1 || *value < '1' || *value > '8') {
        fputs("CHIBI_TEST_THREADS must be an integer from 1 to 8\n", stderr);
        return false;
    }
    requested = *value - '0';
    return true;
}

void test_parallel_begin()
{
    assert(!scope_open && !running && !count);
#ifdef CHIBI_TEST_ENKITS
    if (!scheduler) {
        scheduler = enkiNewTaskScheduler();
        struct enkiTaskSchedulerConfig config = enkiGetTaskSchedulerConfig(scheduler);
        uint32_t available = config.numTaskThreadsToCreate + 1;
        if (!available) {
            available = 1;
        }
        workers = requested ? requested : MAX_WORKERS;
        if ((uint32_t)workers > available) {
            workers = (int)available;
        }
        enkiInitTaskSchedulerNumThreads(scheduler, (uint32_t)workers);
        for (int i = 0; i < BATCH_SIZE; i++) {
            tasks[i] = enkiCreateTaskSet(scheduler, run_range);
        }
    }
#endif
    scope_open = true;
    pu_set_dispatch(enqueue, NULL);
}

void test_parallel_end()
{
    assert(scope_open && !running);
    pu_set_dispatch(NULL, NULL);
    flush();
    scope_open = false;
}

void test_parallel_finish()
{
    assert(!scope_open && !running && !count);
#ifdef CHIBI_TEST_ENKITS
    if (scheduler) {
        for (int i = 0; i < BATCH_SIZE; i++) {
            enkiDeleteTaskSet(scheduler, tasks[i]);
            tasks[i] = NULL;
        }
        enkiDeleteTaskScheduler(scheduler);
        scheduler = NULL;
    }
#endif
}

int test_parallel_workers()
{
    return workers;
}
