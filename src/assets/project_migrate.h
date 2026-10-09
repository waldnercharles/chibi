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
/* Version 12 removes the `&` reference operator: a writable value passes to a
 * `ref` parameter directly. Returns an owned copy of `source` without its
 * unary `&` operators, binding a `fallback` through a leading `&x` as
 * `binding: x`, or NULL when the source does not change. */
char *chibi_migrate_references(const char *source);
/* Version 13 removes `result Name: T = e` declarations: each bare `return` of
 * their function returns the declared values in signature order, and a body
 * without a `return` ends with one. Returns an owned copy of `source`, or NULL
 * when it declares no results. */
char *chibi_migrate_results(const char *source);
/* Version 14 removes `value` declarations: each read of one becomes its
 * expression, which a typed declaration casts. Returns an owned
 * copy of `source`, or NULL when it declares none. */
char *chibi_migrate_values(const char *source);

#endif
