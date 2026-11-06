#pragma once
#include "assets/project.h"
#include "runtime/native_capabilities.h"

/* Open a current compiled-source project. The explicit native table is
 * borrowed; opening does not modify sources or publish a generation. */
int authoring_project_open(chibi_project *, const char *path, native_capabilities *, char *error, size_t size);
/* Opens content for an explicitly selected scene/prototype, not a launch.
 * The manifest need not select a start scene. */
int authoring_project_open_content(
    chibi_project *,
    const char *path,
    native_capabilities *,
    char *error,
    size_t size
);
/* Validate the current source baseline; historical formats are refused. */
int authoring_project_migrate(const char *path, native_capabilities *, char *error, size_t size);
