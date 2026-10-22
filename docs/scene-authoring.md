# Scene hierarchy moves

The installed `Chibi::scene_authoring` package owns scene move planning through
`authoring/scene_hierarchy.h`. Editors capture an ordered snapshot of object
handles, names, parents and local transforms, call `authoring_scene_move`, then
apply the successful snapshot to their world. The operation never creates or
destroys an object. Editors retain the before and after snapshots for one
grouped undo/redo operation and collaboration replay.

Parent indices refer to the snapshot. Parents must precede their children, and
array order determines sibling order. The result uses preorder. Moving a
selection keeps the selected subtrees in their previous scene order; selecting
both an ancestor and a descendant moves that subtree once. A zero destination
handle means scene roots. A zero insertion sibling means append; otherwise the
insertion sibling must be an unselected child of the destination.

Validation rejects stale or duplicate handles, invalid names, duplicate sibling
names, cycles and hierarchies deeper than `SCENE_DEPTH_MAX`. Rejection leaves
the input snapshot unchanged. Names and entity handles stay unchanged, while
name paths follow the new hierarchy. For example, moving `A.Group` below `B`
changes its child's path from `A.Group.Child` to `B.Group.Child`. Existing entity
handles keep their identity. Path strings in behavior source or component values
remain authored text; this operation does not rewrite them. Callers must present
that distinction when exposing moves alongside path-based references.

Keep World is the default editor behavior. It preserves the 2D and 3D world
placements independently, using the same nearest transformed ancestor semantics
as runtime propagation. A group without a transform needs no new transform:
the first transformed descendants receive the compensation. In 2D a move may
add Orientation when the object needs a local rotation to keep its world pose.
Sibling reordering retains local components exactly.

The SDK's Keep Local mode retains every local transform component exactly.
Keep World supports rotated, scaled and mirrored transforms, but
rejects singular destination transforms, singular local results, shear and
non-finite or numerically unstable results. It checks reconstructed matrices
with a relative residual tolerance of `0.00001` before accepting a 3D pose, and
checks reconstructed 2D placement for finite precision loss.

`authoring_scene_validate` also validates a replayed snapshot before an editor
changes its ECS world. It does not check whether handles remain alive in that
world; the caller owns that check and the world mutation transaction.

## Owned read snapshots

`authoring/scene_snapshot.h`, in the same package, owns immutable copies of the
raw `authoring_scene_node` array and every name. Capture validates the existing
node contract and preserves exact 64-bit entity identity, parent indices, array
order and local transforms. It neither reads an ECS world nor derives UI rows.
No filter, selection, icons, rename, drop or history policy is included.

A producer creates one `authoring_scene_owner` lifetime/generation anchor for
its exposed raw scene. It calls `authoring_scene_owner_changed` before changing
that data, completes the change before capturing again, and calls
`authoring_scene_owner_free` before destroying or replacing the producer. These
calls and snapshot access must be serialized. Capture/change require a live
owner. Invalid node data rejects capture without changing the generation;
allocation or generation/reference-count exhaustion aborts.

Every snapshot retains its originating anchor and captured generation.
`authoring_scene_snapshot_current(snapshot, actual_owner)` rejects another
owner, a changed generation, a closed owner, or a NULL owner. A live consumer
must perform this check against its actual current owner within the same
serialization boundary as reading/publishing. An originating owner pointer
remains valid for this comparison while its snapshot retains the allocation;
an unrelated freed pointer must never be supplied.

`authoring_scene_snapshot_nodes` always exposes the historical immutable copy,
including after producer mutation or destruction. Its array and names remain
valid until `authoring_scene_snapshot_free`. Historical memory safety and
current-scene authority are separate guarantees: invalidation does not erase
old data or make old data current. The anchor is deallocated after producer
ownership closes and the last snapshot releases it. This boundary adds no
project format change and no authored record declaration.

The `scene-hierarchy` entry in `tools/test_policy.py` runs the finite C/pico_unit
14 contract cases in `test_tools --scene-hierarchy` through the production
supervisor. They cover grouped moves, stable handles, ordering, invalid moves,
depth, 2D transformless groups, mirrored 3D placement, singular/shear rejection,
local preservation, nested scene persistence, copied name/data lifetime, owner
isolation, invalidated generations and atomic capture rejection. Studio owns interaction,
undo/redo, runtime restoration and collaboration integration checks.
