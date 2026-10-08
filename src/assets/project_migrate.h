#ifndef CHIBI_ASSETS_PROJECT_MIGRATE_H
#define CHIBI_ASSETS_PROJECT_MIGRATE_H

#include <stddef.h>

/* Upgrades the sources under `content` from project version `from` to
 * `from + 1`. Returns 0, or -1 with `error` set. */
int chibi_project_migrate_content(const char *content, int from, char *error, size_t error_size);
/* Version 11 starts a task for an unawaited call to a function that waits.
 * Given every version 10 source of a project, returns an owned array with an
 * owned copy of each source that gains `await` before the calls that blocked
 * under version 10, or NULL for a source that does not change. */
char **chibi_migrate_task_calls(const char *const *sources, int count);

#endif
