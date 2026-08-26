# Prototype-first editor specification

Status: superseded UX proposal. The Prototype workspace and linked inspector
specified here are not available in the current editor. The Asset library is
still available, but it is not a replacement for prototype defaults editing,
scene-instance selection, overrides, behavior attachment controls, save/apply
UI, or runtime attachment debugging. Keep this document as a design reference
for a future replacement, not as a description of current editor behavior.

## Purpose

The editor gives users one place to author reusable prototypes, inspect scene
instances, edit instance overrides, and debug a runtime behavior instance.
Users should always know whether a value belongs to a prototype, a scene
instance, or a running instance.

The target layout is variant B from the A/B prototype. Prototype assets have a
dedicated workspace. Scene entities and their overrides appear in a linked
inspector instead of replacing the prototype workspace.

## Current constraints

The current application has four separate sources of state:

- `game_prototype` parses and spawns prototype JSON, but it has no mutable
  authoring API or asset registry.
- `app/main.c` resolves `PlayerPrototype` and `EnemyPrototype` from hardcoded
  JSON strings.
- `add_actor()` in `app/scene_authoring.c` creates entities directly instead
  of spawning a prototype.
- `game_scene_document` stores a prototype name, a full component JSON object,
  and behavior asset names for each scene entity. It does not store field-level
  override provenance.
- `editor` owns a preview instance. `editor_set_inspection_instance()` only
  changes what the canvas displays. It does not change the instance used by
  editor dispatch.

The implementation must preserve the current headless runtime APIs while
moving authoring state behind explicit prototype and instance boundaries.

## Domain terms

| Term | Owns | Persists | User action |
| --- | --- | --- | --- |
| Prototype asset | Component defaults and default behavior attachments | Prototype asset file or host asset store | Edit defaults, save, apply |
| Behavior asset | Reusable Blueprint graphs | Behavior asset file or host asset store | Edit graph, compile, apply |
| Scene entity | Prototype reference and local overrides | Scene document | Select, override, revert |
| Runtime instance | Active flow state, watches, logs, and diagnostics | No | Inspect, pause, step, resume, stop |

A prototype asset is not an entity. A behavior asset is not a runtime
activation. The UI must not use one word for two of these objects.

## Workspace layout

The prototype workspace has four visible regions:

1. **Asset library.** Lists prototypes and behavior assets. Selecting a
   prototype opens its defaults. Selecting a behavior opens its graph.
2. **Prototype workspace.** Shows the selected prototype's components, default
   values, attached behavior assets, parent or include relation, validation
   state, and change impact.
3. **Instance inspector.** Lists scene entities that use the prototype. It
   shows effective values, inherited values, local overrides, and behavior
   additions or removals.
4. **Status and diagnostics.** Shows unsaved asset edits, compile errors,
   apply errors, affected instance count, and runtime output.

The header always shows the current context:

```text
Assets / Prototypes / EnemyPrototype
Debug target: Enemy 2 · ChaseBehavior · OnStart
Asset state: Modified
Runtime state: Paused at SetTarget
```

The prototype workspace remains open when the user selects a different scene
entity. Entity selection changes only the instance inspector and debug target.

## Prototype editing workflow

1. Select a prototype in the asset library.
2. Edit component defaults or default behavior attachments in the prototype
   workspace.
3. Review the affected instance count and validation results.
4. Save the prototype asset.
5. Apply the saved prototype to the open scene or running world.
6. Keep local instance overrides unchanged unless the user explicitly applies
   or reverts them.

Prototype edits do not silently change the selected entity's local values.
The UI shows which fields remain inherited and which fields remain overridden.

## Instance override workflow

1. Select an entity in the scene or in the prototype's instance list.
2. Show the entity's effective component values in the instance inspector.
3. Mark values that differ from the prototype as local overrides.
4. Provide `Revert` for each override and `Revert all overrides` for the
   instance.
5. Provide `Apply selected to prototype` and `Apply all to prototype` as
   explicit actions.
6. Show behavior assets inherited from the prototype separately from behavior
   assets added or removed on the instance.

For example, if `EnemyPrototype.Health.current` is `100` and `Enemy 2` has a
local value of `40`, editing the prototype to `150` changes inherited entities
to `150`. `Enemy 2` remains at `40` until the user reverts its override.

## Behavior workflow

1. Select a behavior asset in the asset library.
2. Open its graph workspace with the graph tree and canvas.
3. Select an entity instance that uses the behavior.
4. Choose a runtime activation in the instance inspector.
5. Show that activation's current node, flow traversal, watches, variables,
   call stack, logs, and diagnostics on the graph and in the inspector.
6. Route `Dispatch`, `Pause`, `Resume`, `Step`, `Step over`, and `Stop` to the
   explicitly shown runtime target. The editor preview is a separate target.

The behavior editor must not display one instance while dispatching another.

## Persistence rules

Prototype assets and scene documents have separate save operations.

- Saving a prototype writes its defaults and default behavior attachments.
- Saving a scene writes prototype references, entity names, component
  overrides, and behavior additions or removals.
- Runtime activations, logs, watches, and breakpoints do not enter either
  file unless the user explicitly saves editor-only debug settings.
- Loading a scene resolves prototype assets before applying local overrides.
- Missing prototype or behavior assets stop the load with a named diagnostic.
- Applying a prototype or behavior asset is explicit and reports the affected
  instance count.

The existing version 1 scene format remains readable. New override fields use
an explicit document version or a backward-compatible default that treats the
existing component object as a local override object.

## State and API shape

The authoring boundary needs one explicit state object rather than unrelated
selection flags:

```c
struct scene_authoring_selection {
    const char *prototype_name;
    const char *behavior_name;
    ecs_entity entity;
    int activation_index;
};
```

The persisted instance state needs a prototype reference plus local changes:

```c
struct scene_entity_override {
    const char *path;
    const char *value_json;
};

struct scene_entity_binding {
    const char *prototype_name;
    scene_entity_override *overrides;
    const char **behavior_additions;
    const char **behavior_removals;
};
```

The final C types may use owned strings and project arrays. The invariant is
that a scene entity stores changes relative to a prototype, not a second copy
of the prototype defaults.

The prototype authoring API must support these operations:

- enumerate and resolve prototype assets,
- load and save a mutable prototype definition,
- read and write component field defaults,
- enumerate and edit default behavior attachments,
- compare a prototype with an instance,
- apply prototype defaults without overwriting local overrides,
- apply or revert selected instance overrides.

The runtime behavior API remains responsible for attachment and activation
lifecycle. It must not own prototype editing or scene override policy.

## Acceptance checks

The implementation is ready for review when all of these checks pass:

- A prototype can be selected from an asset library and opened without first
  selecting an entity.
- Editing a prototype default marks the prototype modified and shows the
  affected instance count.
- Saving a prototype does not save the scene document, and saving a scene does
  not silently save prototype defaults.
- A scene entity shows inherited values and local overrides separately.
- Reverting an entity override restores the current prototype value.
- Applying an entity override to the prototype is explicit and updates the
  prototype asset state.
- Prototype behavior attachments are visible as inherited on instances.
- Entity-local behavior additions and removals survive a scene round trip.
- Selecting a different entity does not replace the prototype workspace.
- The runtime debugger names one entity, one behavior asset, and one activation.
  Its controls affect only that target.
- Existing behavior attachment, graph navigation, apply lifecycle, scene
  loading, prototype spawning, and self-check tests remain green.
- The real application can complete this manual flow without reading source:
  create an entity, assign a prototype, override a field, open the prototype,
  change a default, save, apply, inspect the override, and debug one behavior
  activation.

## Chosen implementation shape

`game_prototype` remains the owner of descriptor-backed component data and
prototype behavior names. Add a small mutable API for reading and writing
fields, cloning a prototype, and editing behavior names.

`app_scene_authoring` owns the prototype asset list for the demo host. Each
scene actor stores its prototype name and a clone of the prototype defaults
that were last applied to that actor. The clone lets the editor distinguish an
instance override from a prototype change.

The scene document keeps the existing component object as a sparse override
object. It also gains optional behavior addition and removal arrays. Existing
version 1 documents that contain a flat behavior array remain readable.

Prototype apply and behavior graph apply stay separate. Prototype apply updates
only fields and behavior attachments that still match the actor's previous
prototype snapshot. Behavior graph apply continues to replace the selected
Blueprint asset's runtime instances through `game_live_apply()`.

## Implementation order

1. Add a mutable prototype asset boundary and a normalized instance override
   representation.
2. Extend scene persistence with prototype references and explicit overrides.
3. Route entity creation and scene loading through prototype spawning.
4. Replace the Scene authoring window with the prototype-first workspace and
   linked instance inspector.
5. Route editor runtime actions through the explicit debug target.
6. Add focused tests and run build, formatting, diagnostics, and UI smoke
   checks after each unit.
