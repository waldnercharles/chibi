#ifndef CHIBI_AUTHORING_SCENE_SNAPSHOT_H
#define CHIBI_AUTHORING_SCENE_SNAPSHOT_H

#include "authoring/scene_hierarchy.h"

typedef struct authoring_scene_owner authoring_scene_owner;
typedef struct authoring_scene_snapshot authoring_scene_snapshot;

/* A lifetime/generation anchor, not a scene, world or document. Calls on an
 * owner and its snapshots are serialized. Invalidate before changing any
 * exposed raw data, and finish the change before capturing/publishing again. */
authoring_scene_owner *authoring_scene_owner_new();
void authoring_scene_owner_changed(authoring_scene_owner *);
/* Closes producer ownership before its data is destroyed/replaced. Existing
 * snapshots retain the closed anchor until the last snapshot is freed. Do not
 * capture, change or free this owner again after closing it. */
void authoring_scene_owner_free(authoring_scene_owner *);

/* Copies the existing ordered raw-node contract and every name. The producer
 * array/names are borrowed only during capture. A live owner is required.
 * Invalid nodes return NULL with authoring_scene_validate's diagnostic;
 * allocation failure aborts. Rejection leaves the owner generation unchanged. */
authoring_scene_snapshot *authoring_scene_snapshot_new(
    authoring_scene_owner *,
    const authoring_scene_node *,
    int count,
    char *error,
    size_t error_size
);
void authoring_scene_snapshot_free(authoring_scene_snapshot *);
/* Historical immutable data, valid until snapshot_free even after producer
 * mutation/destruction. count is required; empty scenes return NULL and 0.
 * Reading old data does not authorize using it as the current scene. */
const authoring_scene_node *authoring_scene_snapshot_nodes(
    const authoring_scene_snapshot *,
    int *count
);
/* False for a NULL owner, another owner, a closed owner or a changed generation.
 * owner must be live or the snapshot's originating retained anchor, never an
 * unrelated freed pointer. A live adapter checks its actual current owner and
 * keeps the same serialization boundary through use/publication of the data. */
bool authoring_scene_snapshot_current(const authoring_scene_snapshot *, const authoring_scene_owner *);

#endif
