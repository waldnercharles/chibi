#ifndef CHIBI_AUTHORING_PROGRAM_SERVICE_H
#define CHIBI_AUTHORING_PROGRAM_SERVICE_H

#include "authoring/program.h"
#include "types/array.h"

#define PROGRAM_SERVICE "application.programs"

typedef struct program_service program_service;
/* A program compiled ahead of time that the host embeds, as the launcher
 * embeds its CLI and formatter: `name` opens it, and `closure` holds `length`
 * bytes of its closure, compiled for the host's catalog. `source` is the root
 * of its files in diagnostics: the package's directory in the engine checkout,
 * such as apps/fmt, below which the SDK build laid out the packages it
 * imports from elsewhere (tools/compiler/source_parser as source_parser). */
typedef struct
{
    const char *name, *source;
    const unsigned char *closure;
    size_t length;
} embedded_program;

/* Construct the service before composing its native catalog, then configure
 * it once before opening sessions. Core is an explicit absolute collection
 * directory. The embedded programs are borrowed for the service's life.
 * Configure retains the catalog and copies service rows/names. */
program_service *program_service_new(const char *core, const embedded_program *programs, int program_count);
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
/* Opens the embedded program called `name` as open opens sources. Its first
 * update decodes and verifies the closure; once accepted, later ones report
 * it unchanged, and after a failure each decodes it again. 0 when the service
 * embeds no program of that name. */
uint64_t program_service_open_embedded(
    program_service *,
    const char *name,
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
