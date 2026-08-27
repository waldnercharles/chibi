---
title: >-
  c-blueprints
category: project
tags: [c, visual-scripting, game, architecture, editor]
source_path: /Users/cwaldner/Personal/c-blueprints
sources: [projects/c-blueprints]
summary: >-
  C23 visual scripting runtime and editor whose game consumer composes per-entity Blueprint behaviors over reflected ECS components.
provenance:
  extracted: 0.78
  inferred: 0.18
  ambiguous: 0.04
base_confidence: 0.59
lifecycle: draft
lifecycle_changed: 2026-08-27
created: 2026-08-27T05:10:02Z
updated: 2026-08-27T05:23:15Z
---

# c-blueprints

c-blueprints is a C23/CMake visual scripting runtime with a native dcimgui editor. The core graph language is engine- and ECS-agnostic. The in-tree game consumer adds an ECS, prototypes, behavior attachments, an engine frame loop, and a scene authoring host. `core/` must not include headers from those consumer layers. (CMakeLists.txt:116-176; docs/game-spec.md:7-11,388-400)

## Key concepts

- [[projects/c-blueprints/concepts/runtime-evaluation-and-identity]]: separates pull-based data evaluation, imperative flow, persistent instances, and activation-local state.
- [[projects/c-blueprints/concepts/ecs-prototypes-and-behavior-composition]]: puts durable entity state in reflected components and composes behavior from reusable graph assets.
- [[projects/c-blueprints/concepts/live-authoring-and-runtime-targets]]: explains working copies, applied snapshots, live propagation, and the editor's Scene, Prototype, and Behavior contexts.
- [[projects/c-blueprints/references/architecture-decisions]]: decision trail and the constraints that still shape the implementation.
- [[projects/c-blueprints/skills/validation-workflow]]: build, headless, determinism, and manual editor checks.

## Current focus

The core execution and graph editor baseline is in place. The current project direction is the Phase 10 game runtime vertical slice and the first Unreal-style editor shell. The shell treats a Prototype as the Blueprint-like asset, keeps Behavior assets reusable, and exposes Scene instances as local field overrides. (docs/roadmap.md:45-85,602-619; docs/adr/0021-prototype-shell-and-live-runtime-editor.md:19-49)

The first editor slice includes context-routed panels, fixed catalog Event creation, component propagation, Scene overrides, explicit Scene Runtime controls, atomic Save hooks, and an editor preview backed by a cloned applied snapshot. A full separate Prototype Preview world, Reload, Save As, and complete per-document Scene history remain unfinished. (docs/unreal-blueprint-editor-plan.md:322-342)

## Boundaries and current risks

- The Phase 10 scene depends on a real operator catalog. The roadmap records that `CAT-33` is currently covered by a vacuous check and that the required arithmetic, comparison, and boolean nodes are missing. (docs/roadmap.md:621-649)
- Slice 0 measures pooled per-entity activation cost before more game code is built. If the activation budget is too high, more work moves to batch systems. (docs/roadmap.md:675-690)
- Parallel execution is opt-in through declared node effects and a host-supplied callback. Cross-entity effects go through sorted deferred records so the world hash remains deterministic. (docs/adr/0019-parallel-activation-through-declared-effects.md:27-64)

## Sources

- `docs/spec.md`: engine-agnostic language contract.
- `docs/game-spec.md`: ECS consumer contract and implementation boundaries.
- `docs/roadmap.md`: current state, Phase 10 plan, and acceptance gates.
- `docs/unreal-blueprint-editor-plan.md`: current editor slice and deferred work.
- `CMakeLists.txt`: target and dependency layering.
