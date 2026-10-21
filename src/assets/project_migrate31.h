#ifndef CHIBI_PROJECT_MIGRATE31_H
#define CHIBI_PROJECT_MIGRATE31_H

#include <stddef.h>

/* Version 32 names the array functions for what they do: `append(xs, x)` adds
 * an item, as `array_add` did, and `extend(xs, ys)` adds an array's items, as
 * `append` did. Sets `migrated` to an owned rewrite, or NULL when nothing
 * changes: a source that calls `extend` is already migrated. Returns -1 with
 * `error` set when the source declares one of those names itself, since its
 * calls would then mean something else.
 *
 * Text alone cannot tell a version 31 `append` from a migrated one, so a rerun
 * after a later step failed renames the calls of a source that only added
 * items again; the analyzer then rejects `extend(xs, x)` unless x is an
 * array. */
int chibi_migrate31_array_calls(const char *source, char **migrated, char *error, size_t size);

#endif
