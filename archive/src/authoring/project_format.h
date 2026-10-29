#ifndef CHIBI_AUTHORING_PROJECT_FORMAT_H
#define CHIBI_AUTHORING_PROJECT_FORMAT_H

#include "runtime/native_capabilities.h"

/* Rewrites each authored asset of the project at root as its own saver writes
 * it: sources, KDL documents, the input map and component declarations. UI
 * markup is kept as written. Sources resolve against native. Each asset that
 * differs is named on stderr; with check none is written. Returns how many
 * differ, or -1 when the project, an asset or a write fails. */
int authoring_project_format(const char *root, native_capabilities *native, bool check);
/* Rewrites each .chibi file below the canonical directory root, the files
 * chibi test reads, in canonical form. Each must declare a package: a
 * project's sources resolve against the catalog authoring_project_format
 * builds. Files are named on stderr as by authoring_project_format, and checked
 * before any is written. Returns how many differ, or -1 when the folder cannot
 * be read as chibi test reads it, holds no package files, a file is not a
 * package or fails to format, or a write fails. */
int authoring_packages_format(const char *root, bool check);

#endif
