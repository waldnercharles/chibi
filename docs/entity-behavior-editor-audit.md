# Entity behavior editor audit

Status: baseline findings and current implementation status for the entity behavior editor work. The former Prototype workspace and its scene-authoring inspector/debugger panels have been removed from the running editor. The Content Drawer now scans the configurable `content/` root and keeps source paths visible while recognized files feed the runtime registry.

## Completed in the current implementation

- `game_behavior_world` exposes borrowed indexed attachment inspection, named
  detach, asset registration lookup, and per-entity instance access.
- The editor tracks an active graph locally by stable graph ID, keeps per-graph
  view state, opens graphs from the Blueprint tree and asset-driven host paths,
  switches to the selected asset's editable Blueprint copy, and uses a selected
  entity activation for graph flow visualization.
- Runtime and authoring APIs list and attach registered behavior assets, open
  their graphs, and inspect runtime instances. The removed scene-authoring
  panels no longer expose activations, current nodes, errors, variables, call
  stacks, traversals, watches, controls, breakpoint overrides, or runtime logs.
- `Begin` runs for initial attachment as well as definition replacement.
- `game_scene_document` persists scene entity prototype names, sparse component
  overrides, behavior additions, and behavior removals. Public scene and
  prototype save/load APIs remain, and scene loading uses registered prototype
  assets through `game_prototype_spawn()`. The removed workspace no longer
  exposes those operations as editor controls.
- `test/prototype_authoring_check.c` covers prototype registration, prototype
  spawning, scene save/load, and prototype asset save.
- The Content Drawer recursively scans the configurable `content/` root,
  renders folders and files in path order, and loads recognized prototype and
  behavior JSON files. Its search matches source paths, and recognized assets
  retain saved, modified, and error state. The graph editor retains structured
  compiler diagnostics and stable node and pin IDs for graph navigation and
  runtime visualization.

## Current runtime path

The app loads prototype and behavior JSON files below `content/` through the
Content Drawer host. The default project contains `InputEntity` and
`StaticEntity`; neither has a player designation. New entities still spawn
through `game_prototype_spawn()` from the selected prototype. The editor does
not currently expose prototype defaults editing, scene-instance selection,
overrides, behavior attachment controls, save/apply UI, or runtime attachment
debugging.

Each scene actor stores its prototype name and a clone of the defaults last
applied to it. The clone preserves the baseline needed by scene persistence and
future override UI. The model still applies prototype updates without changing
local values.

`game_behavior_attach()` creates one `blueprint_instance` per entity and asset.
The instance owns the entity handle and runtime state, while the applied
Blueprint definition is shared. Runtime inspection reads attachment records
from the behavior world rather than maintaining a second Boolean source of
truth.

`Compile / Apply graph` copies the editor's working Blueprint into the live
working copy. `game_live_apply()` compiles a snapshot, replaces the registered
asset, destroys and recreates that asset's instances, and runs `Begin` on the
recreated instances. Initial attachment now uses the same `Begin` lifecycle.

The prototype format has a `behaviors` array and a spawn hook that attaches each
named asset. A prototype with behavior names now requires an attach hook rather
than silently dropping those attachments. Scene loading resolves registered
prototype assets, applies sparse component overrides without zeroing sibling
fields, and applies behavior additions and removals relative to the prototype.

## Remaining gaps

### Lower-priority follow-up work

1. **Behavior asset authoring in Content Drawer**

   The Content Drawer loads behavior definitions from source files and opens
   them in the Blueprint editor. Attachment controls, renaming, file creation,
   and managing a larger library still require host code. The removed workspace
   no longer provides behavior attachment or runtime debugging controls.

2. **Viewport selection**

   Scene-instance selection is temporarily unavailable in the editor. A future
   replacement should let viewport selection update the same entity and its
   behavior debug target.

## Acceptance checks

The feature is ready for this goal when the following are true:

- Selecting an entity shows its stable handle, all attached behavior asset
  names, applied definition state, and instance availability.
- The selected entity can open its behavior asset in the Blueprint editor. The
  editor can open the default, function, and macro graphs, and graph selection
  changes the canvas, details, evaluation, compile, and navigation target.
- An entity can attach and detach more than one registered behavior asset. A
  failed attach leaves the entity state and UI consistent and reports the
  missing asset.
- The debugger can target a selected entity instance rather than only the
  editor preview. It can show active runs, state, current node, errors, call
  stack, traversal records, watched values, and output originating from that
  instance. Supported pause, resume, step, and stop operations affect only the
  selected instance.
- The UI shows unapplied edits, compile/apply failures, and the applied
  snapshot boundary. Applying a definition recreates only its instances,
  preserves stable graph/node identities, and runs the documented lifecycle
  event.
- A saved scene or prototype can round-trip component values and behavior
  attachments, then spawn the same runtime composition.
- Headless tests cover attachment enumeration, per-entity instance lookup,
  multi-asset attach/detach, graph selection state, asset-correct Blueprint
  switching, apply/recreate behavior, lifecycle events, and debug inspection.
  Existing build, self-check, editor runtime, integration, registry, and
  specification checks remain green.

## Next implementation order

1. Add behavior asset rename and larger-library management to the host.
2. Add viewport entity picking.
3. Perform manual editor verification and expand regression coverage.

## UX prototype

`docs/behavior-editor-mockup.html` is a standalone clickable prototype for the
next interaction model. It keeps scene selection, behavior asset editing, and
runtime debugging visible in one context while preserving explicit asset and
runtime targets. The prototype covers entity selection, asset selection, graph
selection, node inspection, diagnostics, activation selection, and apply/save
feedback without changing the engine UI.

`docs/prototype-editor-mockup.html` is a second prototype for prefab-style
editing. It makes prototype defaults, inherited values, instance overrides,
behavior attachments, change impact, and Apply/Revert actions explicit.

`docs/prototype-editor-ab-mockup.html` compares a prototype tab inside the
entity inspector with a separate prototype workspace. It includes a small task
checklist and observer notes so the two flows can be compared without engine
changes.

The existing game language families that are explicitly Deferred, such as
async actions, timelines, Set/Map semantics, and typed delegate design, are not
prerequisites for this editor slice.
