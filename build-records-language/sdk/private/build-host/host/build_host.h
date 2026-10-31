#ifndef CHIBI_HOST_BUILD_HOST_H
#define CHIBI_HOST_BUILD_HOST_H

#include "execution/backend.h"
#include "execution/function.h"

#include <signal.h>
#include <stdio.h>

/* Raise with the Makefile's `build` gate whenever tools/toolchain/Build.chibi
 * needs a capability or syntax that an older host lacks. */
#define CHIBI_BUILD_HOST_PROTOCOL 12
#define CHIBI_BUILD_OUTPUT_CAPACITY 4096

/* A collection a program imports as name:path, as Odin's -collection names. */
typedef struct
{
    const char *name, *root; /* root is an absolute directory. */
} chibi_build_collection;
#define CHIBI_BUILD_MAX_COLLECTIONS 8

typedef struct chibi_build_compiler chibi_build_compiler;
/* Owns a validated current compiler generation, independent of a build
 * program's core collection. Matching recorded source hashes load the checked
 * closure directly; changed sources recover a fixed point. No files are
 * written. Reuse sequentially; reload after changing compiler sources. */
chibi_build_compiler *chibi_build_compiler_load(
    const char *sources,
    const char *core,
    char *error,
    size_t size
);
void chibi_build_compiler_free(chibi_build_compiler *);

typedef struct
{
    chibi_build_compiler *compiler; /* Borrowed for this synchronous invocation. */
    const char *root, *source_path, *source_text, *entry;
    const char *core; /* Absolute core collection directory, or NULL for none. */
    const chibi_build_collection *collections; /* Besides core. */
    int collection_count;
    const char *const *arguments;
    int argument_count;
    behavior_backend backend;
    uintptr_t instruction_budget; /* Zero for the host's default. */
    double deadline;              /* Absolute chibi_build_now() seconds. */
    const volatile sig_atomic_t *cancelled;
    /* Optional caller lifecycle hook, after compilation and binding but before
     * authored execution or build children. Not called on preparation failure. */
    void (*prepared)();
    /* Optional private TSV timing sink, borrowed. NULL skips trace IO/clocks. */
    FILE *timings;
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
