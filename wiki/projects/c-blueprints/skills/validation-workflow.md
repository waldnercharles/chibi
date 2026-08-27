---
title: >-
  c-blueprints validation workflow
category: skills
tags: [c, testing, editor, game]
sources: [projects/c-blueprints]
summary: >-
  Validate c-blueprints in layers: headless model checks, deterministic runtime checks, build-time coverage, and manual docked-editor checks.
provenance:
  extracted: 0.88
  inferred: 0.10
  ambiguous: 0.02
base_confidence: 0.59
lifecycle: draft
lifecycle_changed: 2026-08-27
created: 2026-08-27T05:10:02Z
updated: 2026-08-27T05:23:15Z
---

# c-blueprints validation workflow

Use headless checks for runtime contracts and a screenshot-driven manual pass for editor behavior. The project keeps the core, ECS, and most integration checks independent of a window host, while the app check covers the docked editor and viewport path. (docs/game-spec.md:101-107,336-360; docs/unreal-blueprint-editor-plan.md:287-320)

## Model and runtime checks

```sh
cmake --build build
./build/self_check
./build/integration
```

For the game/runtime slice, keep focused checks for fixed catalog Event signatures, Prototype component propagation, sparse Scene overrides and Revert, behavior attachment lifecycle, separate runtime restart state, Save plus Apply failure behavior, atomic writes, and dirty-state transitions. (docs/unreal-blueprint-editor-plan.md:287-307)

The Phase 10 acceptance path also checks command-buffer determinism, per-frame world hashes across thread counts, independent instance state, activation reaping, scoped Apply teardown, hot reload of a suspended `Delay`, and field-by-name component migration. The budget target is 2,000 live entities with roughly 500 per-frame behaviors under 2 ms of behavior time per frame. (docs/roadmap.md:765-793)

## Editor pass

Run:

```sh
build/app --headless --screenshot
```

Inspect `/tmp/c-blueprints-screenshot.png` and report its dimensions. Exercise Prototype, Components, My Blueprint graph, Scene instance Inspector context, Save, Save All, blocked Compile/Apply, Undo/Redo, Scene Runtime Restart, and bottom-host docking. Reset `imgui.ini` for a clean default-layout check, then confirm a customized layout survives restart. (docs/unreal-blueprint-editor-plan.md:309-320)

The manual document-context check is to select a scene instance, open a Behavior asset from Content Drawer, and select a Prototype without moving focus to an unrelated surface. Close and reopen Content Drawer from Window to verify its docked-tab lifecycle. (docs/editor-document-system.md:158-169)

## Known prerequisite

Do not treat the Phase 10 scene as complete until the operator catalog has real Add, Subtract, Multiply, Divide, equality, ordering, And, Or, and Not nodes. The roadmap records that the current `CAT-33` check does not prove those operations resolve. (docs/roadmap.md:621-649)

## Related

- [[projects/c-blueprints/c-blueprints]]: current project focus and known gaps.
- [[projects/c-blueprints/concepts/live-authoring-and-runtime-targets]]: behavior of Save, Compile, Apply, and Restart.
- [[projects/c-blueprints/references/architecture-decisions]]: requirements behind the checks.

## Sources

- `docs/unreal-blueprint-editor-plan.md`
- `docs/editor-document-system.md`
- `docs/roadmap.md`
- `docs/game-spec.md`
