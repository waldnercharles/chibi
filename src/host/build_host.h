#ifndef CHIBI_HOST_BUILD_HOST_H
#define CHIBI_HOST_BUILD_HOST_H

#include "execution/backend.h"
#include "execution/function.h"

#include <signal.h>

/* Raise with the Makefile's `build` gate whenever tools/toolchain/Build.chibi
 * needs a capability or syntax that an older host lacks. */
#define CHIBI_BUILD_HOST_PROTOCOL 3
#define CHIBI_BUILD_OUTPUT_CAPACITY 4096

typedef struct
{
    const char *root, *source_path, *source_text, *entry;
    const char *const *arguments;
    int argument_count;
    behavior_backend backend;
    double deadline; /* Absolute chibi_build_now() seconds; also bounded by inherited supervision. */
    const volatile sig_atomic_t *cancelled;
} chibi_build_options;

typedef struct
{
    int value, flow_status, steps;
    behavior_run_counts counts;
    char stdout_text[CHIBI_BUILD_OUTPUT_CAPACITY];
    char stderr_text[CHIBI_BUILD_OUTPUT_CAPACITY];
    size_t stdout_size, stderr_size;
    bool stdout_truncated, stderr_truncated;
    char diagnostic[256];
} chibi_build_result;

/* Private build-only host. All inputs borrowed for this synchronous invocation.
 * Returns false for preparation/host/runtime failure; true also for nonzero
 * authored results. Output and diagnostics remain owned by result. */
bool chibi_build_invoke(const chibi_build_options *, chibi_build_result *);
double chibi_build_now();
#endif
