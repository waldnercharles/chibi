#ifndef CHIBI_AUTHORING_PROGRAM_SERVICE_H
#define CHIBI_AUTHORING_PROGRAM_SERVICE_H

#include "authoring/program.h"
#include "types/array.h"

#define PROGRAM_SERVICE "application.programs"

typedef struct program_service program_service;
/* Construct the service before composing its native catalog, then configure
 * it once before opening sessions. Core is an explicit absolute collection
 * directory. Configure retains the catalog and copies service rows/names. */
program_service *program_service_new(const char *core);
void program_service_configure(program_service *, const authoring_program_options *);
const native_capability_pack *program_service_pack();
/* Every open returns a distinct owned handle, including for invalid sources;
 * update diagnoses those sources. Handles are never reused, and cannot name
 * sessions of another service. The full argv vector is copied per session. */
uint64_t program_service_open(
    program_service *,
    const char *path,
    const char *entry,
    const char *const *arguments,
    int count
);
/* 1: accepted changed generation; 0: unchanged, including an identical failed
 * snapshot; -1: a new failure. Rejection preserves the accepted generation. */
int program_service_update(program_service *, uint64_t handle);
int program_service_call(
    program_service *,
    uint64_t handle,
    const authoring_program_call_options *,
    int *result
);
/* Text is borrowed until the next mutating operation on that session. Invalid
 * handles have a stable diagnostic instead of aliasing a later session. */
/* Calls a named zero-argument void/int function of the accepted generation;
 * `has` reports whether one exists. A missing name is an argument failure with
 * a diagnostic. */
int program_service_call_entry(
    program_service *,
    uint64_t handle,
    const char *name,
    const authoring_program_call_options *,
    int *result
);
bool program_service_has(program_service *, uint64_t handle, const char *name);
const char *program_service_diagnostic(const program_service *, uint64_t handle);
bool program_service_close(program_service *, uint64_t handle);
/* Borrowed for an application.arguments callback. An imported scope is found
 * through its root program's stable managed-heap family. */
const pin_array *program_service_arguments(const program_service *, const execution_program *caller);
void program_service_free(program_service *);

#endif
