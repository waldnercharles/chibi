#pragma once

#include <stdint.h>
enum
{
    TASK_WORKER_MAX = 32
};

typedef void task_range(int start, int end, uint32_t worker, void *user);

/* Borrowed executor. Worker zero is the submitting thread; simultaneous tasks
 * have distinct worker indices below workers. Submit/wait run on worker zero.
 * A NULL task means execution finished inside enqueue. finish must wait for
 * execution to finish, not merely for the work queue to become empty. */
typedef struct task_system
{
    int workers;
    void *user;
    void *(*enqueue)(task_range *run, int count, int minimum, void *context, void *user);
    void (*finish)(void *task, void *user);
} task_system;
