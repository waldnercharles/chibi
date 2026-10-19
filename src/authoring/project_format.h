#ifndef CHIBI_AUTHORING_PROJECT_FORMAT_H
#define CHIBI_AUTHORING_PROJECT_FORMAT_H

#include "runtime/native_capabilities.h"

/* Rewrites each authored asset of the project at root as its own saver writes
 * it: sources, KDL documents, the input map and component declarations. UI
 * markup is kept as written. Sources resolve against native. Each asset that
 * differs is named on stderr; with check none is written. Returns how many
 * differ, or -1 when the project, an asset or a write fails. */
int authoring_project_format(const char *root, native_capabilities *native, bool check);

#endif
