#ifndef CHIBI_PROJECT_MIGRATE26_H
#define CHIBI_PROJECT_MIGRATE26_H

#include "support/text_edit.h"

#include <stdbool.h>
#include <stddef.h>

/* Current-syntax snapshots after the version 25 spelling edits. NULL entries
 * are non-executable assets. Owned edit vectors refer to the supplied bytes. */
bool chibi_migrate26_references(
    const char *const *sources,
    int count,
    text_edit ***edits,
    char *error,
    size_t size
);
void chibi_migrate26_edits_free(text_edit **edits, int count);

#endif
