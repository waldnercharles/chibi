#ifndef CHIBI_PROJECT_MIGRATE29_H
#define CHIBI_PROJECT_MIGRATE29_H

#include "support/text_edit.h"

#include <stddef.h>

/* Version 30 handlers name the payload fields they read: `on X(a)`, `on X as
 * e` and `on X` become `on X :: (F: T)`, listing the fields the handler read,
 * by their own names, with types, in payload order. Reads through `event`, an
 * alias or a renamed positional parameter become the field's name.
 * `declarations` are the texts of the project's event declarations (KDL);
 * other documents are skipped. Sets `migrated` to an owned rewrite, or NULL
 * when no handler changes, and, unless NULL, `applied` to the edits made,
 * each owning its text. Returns -1 with `error` set when a handler cannot be
 * rewritten exactly. */
int chibi_migrate29_handlers(
    const char *source,
    const char *const *declarations,
    int declaration_count,
    char **migrated,
    text_edit **applied,
    char *error,
    size_t size
);

#endif
