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
names, cycles and hierarchies deeper than `GAME_SCENE_DEPTH_MAX`. Rejection leaves
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

The `scene-hierarchy` entry in `tools/test_policy.py` runs the finite C/pico_unit
contract cases in `test_tools --scene-hierarchy` under the shared 60-second
deadline. They cover grouped moves, stable handles, ordering, invalid moves,
depth, 2D transformless groups, mirrored 3D placement, singular/shear rejection,
local preservation and nested scene persistence. Studio owns interaction,
undo/redo, runtime restoration and collaboration integration checks.
