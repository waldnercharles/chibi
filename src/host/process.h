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

#ifndef _WIN32
#include <signal.h>

typedef enum
{
    CHIBI_PROCESS_OK,
    CHIBI_PROCESS_INVALID_INPUT,
    CHIBI_PROCESS_SPAWN_FAILED,
    CHIBI_PROCESS_TIMEOUT,
    CHIBI_PROCESS_CANCELLED,
    CHIBI_PROCESS_HOST_FAILED,
} chibi_process_error;

typedef struct
{
    char *data;
    size_t capacity; /* Includes the trailing NUL; zero discards all output. */
    size_t size;
    bool truncated;
} chibi_process_output;

typedef struct
{
    chibi_process_error error;
    int status; /* Valid on success: exit code, or negative signal number. */
    int system_error; /* errno value for spawn/host failure. */
} chibi_process_capture_result;

/* Same monotonic epoch as the production Python supervisor on each host.
 * Reads only clock_gettime and is async-signal-safe. Returns -1 on failure. */
double chibi_process_now();

/* Synchronously run an absolute executable, with explicit cwd and NULL-terminated
 * envp. argv[0] is nonempty and passed unchanged, independently of executable.
 * Each vector has at most 1023 strings; combined string bytes are at most
 * 1 MiB. No shell or PATH lookup. deadline is finite, positive, absolute
 * chibi_process_now() seconds. All borrowed inputs/buffers must remain alive until
 * return. cancelled may be NULL. Output buffers are caller-owned and always
 * NUL-terminated when capacity is nonzero; size excludes the terminator.
 * The child owns a new process group, killed on completion or failure, including
 * when the leader exits before its descendants. The direct child is reaped.
 * This is process-group supervision, not containment of programs that escape
 * the group or change credentials to prevent the caller from signalling them. */
chibi_process_capture_result chibi_process_capture(
    const char *executable,
    const char *const *argv,
    const char *cwd,
    const char *const *envp,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    chibi_process_output *out,
    chibi_process_output *err
);

/* One child of chibi_process_begin, until chibi_process_end reaps it. */
typedef struct
{
    pid_t pid;    /* Zero once ended. */
    int pipes[2]; /* Read ends of its stdout and stderr. */
    chibi_process_output *out, *err;
    bool leader_finished; /* Its direct child exited; descendants may remain. */
    chibi_process_capture_result result;
} chibi_process_job;
#define CHIBI_PROCESS_SERVICE_JOBS 64

/* Starts a child as chibi_process_capture does and returns at once. Unless the
 * result's error is CHIBI_PROCESS_OK nothing was started. The output buffers
 * are borrowed until chibi_process_end; other inputs only during the call. */
chibi_process_capture_result chibi_process_begin(
    chibi_process_job *,
    const char *executable,
    const char *const *argv,
    const char *cwd,
    const char *const *envp,
    double deadline,
    const volatile sig_atomic_t *cancelled,
    chibi_process_output *out,
    chibi_process_output *err
);
/* Drains the output of each running job and notes which have exited, waiting
 * at most 10 ms. Past the deadline, on cancellation or host failure, every
 * running job records that error. At most CHIBI_PROCESS_SERVICE_JOBS jobs. */
void chibi_process_service(chibi_process_job *, int count, double deadline, const volatile sig_atomic_t *cancelled);
/* Whether the job's child is still running without error. */
bool chibi_process_running(const chibi_process_job *);
/* Kills the job's process group, reaps its child and reads its last output.
 * Status is valid when the job finished without error. */
chibi_process_capture_result chibi_process_end(chibi_process_job *);

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
