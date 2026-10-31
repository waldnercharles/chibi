#pragma once
#include "assets/project.h"
#include "runtime/native_capabilities.h"

/* Authoring entry points complete catalog-dependent migration before publishing
 * the current manifest version. The explicit native table is borrowed. */
int authoring_project_open(chibi_project *, const char *path, native_capabilities *, char *error, size_t size);
/* Opens content for an explicitly selected scene/prototype, not a launch.
 * Migration is identical; the manifest need not select a start scene. */
int authoring_project_open_content(
    chibi_project *,
    const char *path,
    native_capabilities *,
    char *error,
    size_t size
);
int authoring_project_migrate(const char *path, native_capabilities *, char *error, size_t size);
