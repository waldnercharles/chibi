#ifndef CHIBI_HOST_APPLICATION_H
#define CHIBI_HOST_APPLICATION_H

#include "authoring/program_service.h"

/* The catalog applications run against: the SDK's application, filesystem,
 * process, project and program services. The launcher's embedded programs
 * are compiled for it. Owned by
 * the caller (native_capabilities_free); NULL with the reason in `error`. */
native_capabilities *chibi_application_catalog_new(char *error, size_t error_size);

/* The programs the launcher embeds, which the SDK build compiles for the
 * catalog and writes (EmbedPrograms in tools/graph/engine/Engine.chibi). */
extern const embedded_program embedded_programs[];
extern const int embedded_program_count;

/* Runs the Main of the embedded program called "chibi", the public CLI, with
 * the catalog's services; the others are available to it through
 * program.embedded. Every program it starts uses the same session and
 * argument path. Programs are borrowed; paths are absolute; argv includes the
 * application's program name. */
int chibi_application_run(
    const embedded_program *programs,
    int program_count,
    const char *bin,
    const char *core,
    int argc,
    const char *const *argv
);

#endif
