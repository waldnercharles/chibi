#ifndef CHIBI_PROJECT_MIGRATE30_H
#define CHIBI_PROJECT_MIGRATE30_H

#include <stddef.h>

/* Version 31 has no step globals: `dt`, `delta` and `delta_time()` read the
 * frame step only through a Tick handler's list or a system's step clause.
 * In an `on Tick` handler, a read of a global adds `dt: float s` to the list
 * and becomes `dt`; in a system, it adds `on Tick :: (dt: float s)` or `dt`
 * to its clause. Sets `migrated` to an owned rewrite, or NULL when nothing
 * changes. Returns -1 with `error` set when a read lies anywhere else. */
int chibi_migrate30_step_globals(const char *source, char **migrated, char *error, size_t size);

#endif
