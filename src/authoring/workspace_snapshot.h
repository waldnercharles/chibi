#ifndef CHIBI_WORKSPACE_SNAPSHOT_H
#define CHIBI_WORKSPACE_SNAPSHOT_H
#include "authoring/workspace_documents.h"
/* Private frozen compiler input. Owns one bounded directory tree and deletes
 * it on free. Portable rooted filesystem operations refuse links, special files
 * and changing reads. No resident catalog or GPU work. */
typedef struct workspace_snapshot workspace_snapshot;
workspace_snapshot *workspace_snapshot_new(const char *root, workspace_documents *, char *error, size_t);
/* Private generated sources are workspace-relative and create-exclusive. The
 * returned snapshot owns their bytes; failure destroys the whole candidate.
 * Up to 16 UTF-8 sources, each <=1MiB and together <=4MiB. */
workspace_snapshot *workspace_snapshot_new_sources(
    const char *root,
    workspace_documents *,
    const char *const *paths,
    const char *const *texts,
    int count,
    char *error,
    size_t
);
const char *workspace_snapshot_root(const workspace_snapshot *);
void workspace_snapshot_free(workspace_snapshot *);
#endif
