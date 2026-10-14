#ifndef CHIBI_WORKSPACE_SAVED_H
#define CHIBI_WORKSPACE_SAVED_H
#include "runtime/save.h"
#include "support/arena.h"
typedef struct
{
    const char *key, *value;
} workspace_saved_row;
/* Copies existing keys in insertion order. No world/save pointers escape.
 * The caller resolves and validates its session before each invocation. */
bool workspace_saved_query(
    const save *,
    int first,
    int count,
    arena *,
    workspace_saved_row **rows,
    int *total,
    int *returned,
    uint64_t *revision,
    char *error,
    size_t
);
/* Edits only an existing key at the exact observed save revision. A live view
 * does not create keys or commit a host's backing storage. */
bool workspace_saved_write(save *, uint64_t revision, const char *key, const char *value, char *error, size_t);
#endif
