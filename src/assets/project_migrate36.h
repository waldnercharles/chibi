#ifndef CHIBI_PROJECT_MIGRATE36_H
#define CHIBI_PROJECT_MIGRATE36_H

#include <stddef.h>

/* Version 37 replaces `ref` with pointers. A `ref T` type becomes `^T`, or
 * `^(T)` when T adds `[]` or `?`; a `ref value` argument becomes `&value`,
 * or plain `value` when it passes on a `ref` parameter; and each use of a
 * `ref` parameter in its callable becomes `name^`, except as the object of a
 * member access, which reads through the pointer. Sets `migrated` to an
 * owned rewrite, or NULL when nothing changes. Returns -1 with `error` set
 * for a `for x: ref in xs` loop or a local that shadows a `ref` parameter,
 * which an author rewrites first. */
int chibi_migrate36_pointers(const char *source, char **migrated, char *error, size_t size);

#endif
