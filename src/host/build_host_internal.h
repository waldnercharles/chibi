#ifndef CHIBI_HOST_BUILD_HOST_INTERNAL_H
#define CHIBI_HOST_BUILD_HOST_INTERNAL_H
#include "host/build_host.h"
#include "host/build_values.h"
#include "host/process.h"
#include "runtime/native_capabilities.h"

#define BUILD_ENVIRONMENT_CAPACITY 96
/* The environment snapshot's bytes: what exec accepts on macOS. */
#define BUILD_ENVIRONMENT_LIMIT (1024 * 1024)
/* Steps that may run at once through build.start. */
#define BUILD_MAX_JOBS CHIBI_PROCESS_SERVICE_JOBS
/* What build.wait_for returns for a step still running: past any exit code or
 * negative signal number. */
#define BUILD_STEP_RUNNING 65536
/* A step's own output: 1 MB, with room to tell when it was truncated. Each
 * running step holds two such buffers. */
#define BUILD_STEP_OUTPUT_CAPACITY (1024 * 1024 + 1)

typedef struct
{
    const chibi_build_options *options;
    chibi_build_result *result;
    char *root, *program_directory;
    build_values values; /* Native results, imported before the next callback. */
    build_values argument_values; /* Root arguments live for the invocation. */
    pin_array *arguments;
    char *last_stdout, *last_stderr; /* BUILD_STEP_OUTPUT_CAPACITY each. */
    size_t last_stdout_size, last_stderr_size;
    bool last_stdout_truncated, last_stderr_truncated;
    char *environment[BUILD_ENVIRONMENT_CAPACITY];
    double deadline;
    /* Steps build.start began. A slot is free while its handle is zero. */
    chibi_process_job jobs[BUILD_MAX_JOBS];
    chibi_process_output job_outputs[BUILD_MAX_JOBS][2];
    int job_handles[BUILD_MAX_JOBS], job_count;
    /* Trace intervals end when the host first observes completion, not at the
     * exact child exit. Parallel intervals must not be summed as wall time. */
    double job_started[BUILD_MAX_JOBS], job_observed[BUILD_MAX_JOBS];
    const char *job_categories[BUILD_MAX_JOBS];
    int job_steps[BUILD_MAX_JOBS];
} build_host;

const char *build_host_ready(build_host *);
char *build_host_executable(build_host *, const char *);
const char *build_host_run_step(void *, const pin_value *, pin_value *);
const char *build_host_run_probe(void *, const pin_value *, pin_value *);
const char *build_host_environment(void *, const pin_value *, pin_value *);
const char *build_host_environment_names(void *, const pin_value *, pin_value *);
const char *build_host_start(void *, const pin_value *, pin_value *);
const char *build_host_spawn(void *, const pin_value *, pin_value *);
const char *build_host_wait_for(void *, const pin_value *, pin_value *);
const char *build_host_kill(void *, const pin_value *, pin_value *);
void build_host_stop_jobs(build_host *);
/* The build.* natives, for the build host's catalog. */
const native_capability_pack *build_host_natives();
#endif
