---
title: >-
  c-blueprints live authoring and runtime targets
category: concepts
tags: [c, editor, runtime, architecture]
sources: [projects/c-blueprints]
summary: >-
  Live authoring mutates an unattached working copy and applies an immutable snapshot to selected runtime targets only on explicit Compile or Save.
provenance:
  extracted: 0.84
  inferred: 0.13
  ambiguous: 0.03
base_confidence: 0.59
lifecycle: draft
lifecycle_changed: 2026-08-27
created: 2026-08-27T05:10:02Z
updated: 2026-08-27T05:23:15Z
---

# c-blueprints live authoring and runtime targets

A live Behavior definition has an authored working copy and an applied snapshot. The editor mutates the working copy, which has no attached instances. A runtime target executes the applied snapshot, which remains immutable while instances use it. Compile is the explicit Apply operation when a world is live. (docs/adr/0020-editing-a-live-world-through-applied-snapshots.md:22-43; docs/game-spec.md:298-328)

## Apply behavior

`game_live_apply()` serializes the working graph, loads a new snapshot, refreshes diagnostics, and blocks replacement when the compile report contains a blocking error. On success, the behavior world replaces that asset and recreates its affected instances. `app_scene_authoring.c` refreshes the editor preview after a dirty definition is applied. (game/live.c:179-224; app/scene_authoring.c:774-800)

The replacement is scoped to one definition. Other behavior instances and entity components remain intact. Applying loses private instance variables and reruns `Begin`; durable component state survives. This is why transient instance state and component-owned durable state are coupled decisions. (docs/adr/0020-editing-a-live-world-through-applied-snapshots.md:36-68)

Prototype component edits take a different path. They live-patch matching runtime entities that still inherit the edited field. Scene edits are sparse local overrides, and Revert restores the current Prototype value. Scene instances cannot add or remove components. (docs/game-spec.md:135-153,324-328; docs/adr/0021-prototype-shell-and-live-runtime-editor.md:100-120)

## Runtime targets and documents

Prototype Preview and Scene Runtime are intended to be separate worlds with separate entities, behavior instances, activations, logs, and restart state. They share authored data and viewport surfaces, not runtime handles. Scene Runtime exposes Play, Pause, Step, Restart, and Stop and starts stopped. (docs/game-spec.md:237-253; docs/adr/0021-prototype-shell-and-live-runtime-editor.md:122-138)

The editor exposes three top-level document kinds: Scene, Prototype, and Behavior. The host owns the active document context and selection; the shell owns docking and theme; panels read domain state and submit commands. `set_document()` switches the context and requests the matching central surface without making `editor/ui_shell.c` own game data. (docs/editor-document-system.md:1-17,71-112,158-182; app/scene_authoring.c:383-400)

The current first slice has explicit Scene Runtime controls and an editor preview cloned from an applied snapshot. The full separate Prototype Preview world, Reload, Save As, and complete Scene history remain deferred. (docs/unreal-blueprint-editor-plan.md:322-342)

## Related

- [[projects/c-blueprints/concepts/ecs-prototypes-and-behavior-composition]]: source of Prototype defaults and instance state boundaries.
- [[projects/c-blueprints/concepts/runtime-evaluation-and-identity]]: explains why snapshots can be replaced without serializing runtime state.
- [[projects/c-blueprints/skills/validation-workflow]]: checks Save, Apply, failure, and runtime lifecycle behavior.

## Sources

- `docs/adr/0020-editing-a-live-world-through-applied-snapshots.md`
- `docs/adr/0021-prototype-shell-and-live-runtime-editor.md`
- `docs/editor-document-system.md`
- `game/live.c`, `app/scene_authoring.c`
