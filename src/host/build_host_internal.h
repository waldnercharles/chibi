#ifndef CHIBI_HOST_BUILD_HOST_INTERNAL_H
#define CHIBI_HOST_BUILD_HOST_INTERNAL_H
#include "host/build_host.h"
#include "host/build_values.h"
#include "language/lower.h"

#define BUILD_ENVIRONMENT_CAPACITY 96

typedef struct
{
    const chibi_build_options *options;
    chibi_build_result *result;
    char *root, *program_directory;
    build_values values;
    pin_array *arguments;
    char last_stdout[CHIBI_BUILD_OUTPUT_CAPACITY];
    char last_stderr[CHIBI_BUILD_OUTPUT_CAPACITY];
    size_t last_stdout_size, last_stderr_size;
    bool last_stdout_truncated, last_stderr_truncated;
    char *environment[BUILD_ENVIRONMENT_CAPACITY];
    double deadline;
} build_host;

const char *build_host_ready(build_host *);
char *build_host_executable(build_host *, const char *);
const char *build_host_run_step(void *, const pin_value *, pin_value *);
const char *build_host_environment(void *, const pin_value *, pin_value *);
const script_catalog *build_host_catalog();
bool build_host_resolve(void *, uint64_t, script_lower_host *);
#endif
