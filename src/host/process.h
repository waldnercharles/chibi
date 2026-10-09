#ifndef CHIBI_HOST_PROCESS_H
#define CHIBI_HOST_PROCESS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef _WIN32
#define CHIBI_EXECUTABLE_SUFFIX ".exe"
#else
#define CHIBI_EXECUTABLE_SUFFIX ""
#endif

/* Runs argv[0], a path or a program on PATH, with this process's standard
 * streams and waits for it. Returns its exit code or 128 + signal, or prints
 * why it could not start and returns 1. */
int chibi_process_run(char *const *argv);

#ifndef _WIN32
typedef struct
{
    pid_t pid;
    int status;
    bool finished;
    uint64_t stop_started_ms;
} chibi_process;

/* argv is NULL-terminated and passed directly, without shell parsing.
 * Start returns false with errno; log output is truncated for each launch. */
bool chibi_process_start(
    chibi_process *process,
    const char *const *argv,
    const char *cwd,
    const char *logpath
);
/* Poll reaps without blocking. Finished status is exit code or 128 + signal. */
bool chibi_process_poll(chibi_process *process);
/* Signal the entire group. Continue polling to reap and escalate after 2 s. */
void chibi_process_stop(chibi_process *process);
#endif

#endif
