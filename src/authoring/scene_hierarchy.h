#ifndef CHIBI_AUTHORING_SCENE_HIERARCHY_H
#define CHIBI_AUTHORING_SCENE_HIERARCHY_H

#include "ecs/ecs.h"
#include "runtime/components.h"

#include <stdbool.h>
#include <stddef.h>

/* A borrowed scene snapshot. Parents precede children; array order determines
 * sibling order. Entity handles identify objects across moves and replays.
 * Names are borrowed and unique among siblings, using the scene document's
 * name/path rules. A node may carry either, both, or neither local transform.
 * Orientation without Transform is retained but does not affect placement. */
typedef struct
{
    ecs_entity entity;
    const char *name;
    int parent; /* index into the snapshot, or -1 for a root */
    bool has_transform_2d, has_orientation, has_transform_3d;
    Transform position;
    Orientation orientation;
    Transform3D transform_3d;
} authoring_scene_node;

/* Validates identity, order, names, bounded depth and finite local transforms.
 * Returns 0, or -1 with a user-facing reason. Empty scenes are valid. */
int authoring_scene_validate(const authoring_scene_node *nodes, int count, char *error, size_t size);

/* Atomically moves the topmost selected subtrees, in existing scene order,
 * below parent (zero means root), immediately before before (zero appends).
 * before must be an unselected sibling at the destination. The result is
 * preorder with remapped parent indices; entity handles and names stay intact.
 * Duplicate selections and selected descendants are folded into their root.
 * An empty selection, cycle, duplicate sibling name, stale handle, or excessive
 * depth rejects the entire move and leaves nodes byte-for-byte unchanged.
 *
 * keep_world preserves both world placements, including the first transformed
 * descendants of a transformless group. It may add Orientation to a 2D node.
 * A singular destination or resulting local transform, non-finite result, or 3D local matrix that
 * cannot be represented by translation/rotation/scale rejects the move. With
 * keep_world=false, every local component is retained exactly.
 *
 * Object name paths change with the hierarchy. Stored entity handles remain
 * bound; callers own references outside this snapshot, including path strings.
 * No ECS world, assets or external references are mutated by this function. */
int authoring_scene_move(
    authoring_scene_node *nodes,
    int count,
    const ecs_entity *selected,
    int selected_count,
    ecs_entity parent,
    ecs_entity before,
    bool keep_world,
    char *error,
    size_t size
);

#endif
