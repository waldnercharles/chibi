#ifndef CHIBI_HOST_PROCESS_H
#define CHIBI_HOST_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
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

#ifdef _WIN32
typedef struct
{
    uintptr_t process, job, output;
    bool capturing;
    int status;
    bool finished;
    uint64_t stop_started_ms;
} chibi_process;
#else
typedef struct
{
    pid_t pid;
    int output;
    bool capturing;
    int status;
    bool finished;
    uint64_t stop_started_ms;
} chibi_process;
#endif

/* argv is NULL-terminated and passed directly, without shell parsing.
 * Start returns false with errno; log output is truncated for each launch. */
bool chibi_process_start(
    chibi_process *process,
    const char *const *argv,
    const char *cwd,
    const char *logpath
);
/* Captured stdout/stderr share a bounded OS pipe, providing backpressure.
 * Read is nonblocking: positive bytes, zero EOF, or -1 with errno (EAGAIN means
 * no bytes available yet). Close output separately after draining or cancel. */
bool chibi_process_start_capture(chibi_process *, const char *const *argv, const char *cwd);
int64_t chibi_process_read(chibi_process *, void *, size_t);
void chibi_process_close_output(chibi_process *);
/* Poll reaps without blocking. Finished status is exit code or 128 + signal. */
bool chibi_process_poll(chibi_process *process);
/* Signal the entire group. Continue polling to reap and escalate after 2 s. */
void chibi_process_stop(chibi_process *process);

#endif
