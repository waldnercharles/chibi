#ifndef BLUEPRINTS_HOST_PROCESS_H
#define BLUEPRINTS_HOST_PROCESS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct
{
    pid_t pid;
    int status;
    bool finished;
    uint64_t stop_started_ms;
} bp_process;

/* argv is NULL-terminated and passed directly, without shell parsing.
 * Start returns false with errno; log output is truncated for each launch. */
bool bp_process_start(bp_process *process, const char *const *argv, const char *cwd, const char *logpath);
/* Poll reaps without blocking. Finished status is exit code or 128 + signal. */
bool bp_process_poll(bp_process *process);
/* Signal the entire group. Continue polling to reap and escalate after 2 s. */
void bp_process_stop(bp_process *process);

#endif
