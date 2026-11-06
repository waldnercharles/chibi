#ifndef CHIBI_AUTHORING_SOURCE_RENAME_H
#define CHIBI_AUTHORING_SOURCE_RENAME_H

#include "authoring/source_navigation.h"

#include <stddef.h>

typedef struct
{
    int document;
    char *path;
    script_range range;
    char *expected, *replacement;
} authoring_rename_edit;
typedef struct
{
    authoring_rename_edit *edits;
    int count;
} authoring_rename_plan;

/* Uses resolved identities throughout the supplied complete project index.
 * Returns owned edits ordered by document/range, or NULL with an error. Native
 * declarations, invalid drafts, collisions and semantic capture are rejected.
 * Caller must validate the complete candidate collection and verify expected
 * bytes before atomic publication. This function never writes to disk.
 * RML event spans accept identifier tokens only, preserving quotes, binding
 * separators and markup; complete project validation remains the caller's duty.
 * Limits: 1024 documents, 262144 references, 4096 name bytes and 64 MiB
 * aggregate source bytes/owned edit storage. */
authoring_rename_plan *authoring_source_rename(
    const authoring_navigation *,
    authoring_target,
    const char *name,
    char *error,
    size_t size
);
void authoring_rename_plan_free(authoring_rename_plan *);
/* Applies one document's edits to exact expected bytes. Does not publish the
 * candidate; callers validate all changed documents together before adoption. */
char *authoring_rename_document(
    const authoring_rename_plan *,
    int document,
    const char *source,
    char *error,
    size_t size
);

#endif
