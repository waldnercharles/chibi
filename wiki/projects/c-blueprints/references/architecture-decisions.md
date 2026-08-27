---
title: >-
  c-blueprints architecture decisions
category: references
tags: [c, architecture, decisions, project]
sources:
  - projects/c-blueprints
  - conversation:2026-08-27
  - https://dev.epicgames.com/documentation/unreal-engine/components-window-in-unreal-engine?lang=en-US
  - https://dev.epicgames.com/documentation/unreal-engine/outliner-in-unreal-engine?lang=en-US
  - https://dev.epicgames.com/documentation/unreal-engine/graphs-in-unreal-engine?lang=en-US
  - https://dev.epicgames.com/documentation/unreal-engine/ineditor-testing-play-and-simulate-in-unreal-engine?lang=en-US
summary: >-
  A compact map of the ADRs that constrain c-blueprints core, game runtime, live authoring, and editor ownership.
provenance:
  extracted: 0.78
  inferred: 0.20
  ambiguous: 0.02
base_confidence: 0.86
lifecycle: draft
lifecycle_changed: 2026-08-27
created: 2026-08-27T05:10:02Z
updated: 2026-08-27T19:07:40Z
---

# c-blueprints architecture decisions

The decision trail is append-only. The most important dependency chain is: the two evaluators require execution-local state; persistent instances make per-entity behavior possible; ECS components provide shared durable state; declared effects and deferred commands make that state safe to update; working/applied snapshots make live authoring scale without per-keystroke teardown.

## Decision map

| ADR | Decision | Practical constraint |
| --- | --- | --- |
| 0005 | Build-time libclang reflection emits C | Keep field layout and generated descriptors compiler-correct; use `type_desc` for generic access. |
| 0006 | Pull data evaluation plus imperative exec flow | DATA edges never define execution order; EXEC edges never define data dependencies. |
| 0011 | Persistent Blueprint instances | Variables, node scratch, owner handles, and activation queues belong to runtime instances, not one-shot events. |
| 0016 | Activation-owned runtime state | Runtime pin values, memoization, cycle flags, and diagnostics cannot live on shared nodes. |
| 0017 | Entity behavior is composed instances | One entity may attach several graph assets; behavior-local variables stay private and durable shared state is a component. |
| 0018 | Reflected flat-column components | Runtime IDs support generic graph access while flat columns support direct C-system iteration and stable addresses. |
| 0019 | Declared effects plus host parallelism | Parallelism is opt-in; cross-entity and structural effects are sorted deferred records. |
| 0020 | Working copy plus applied snapshot | Edit freely, then Compile/Apply one definition and recreate only its instances. |
| 0021 | Prototype shell and live runtime editor | Prototype owns component defaults and behavior names; Behavior remains reusable; Scene owns local field overrides. |
| 0022 | UE5-aligned entity authoring MVP | Treat Prototype as the reusable Blueprint-like entity asset; keep scene instances, asset authoring, and isolated preview as separate contexts. |

Sources: `docs/adr/0005-build-time-libclang-reflection.md`, `docs/adr/0006-two-evaluators-pull-data-and-imperative-flow.md`, `docs/adr/0011-persistent-blueprint-instances.md`, `docs/adr/0016-activation-owned-runtime-state.md`, `docs/adr/0017-entity-behavior-is-composed-blueprint-instances.md`, `docs/adr/0018-reflected-flat-column-component-storage.md`, `docs/adr/0019-parallel-activation-through-declared-effects.md`, `docs/adr/0020-editing-a-live-world-through-applied-snapshots.md`, and `docs/adr/0021-prototype-shell-and-live-runtime-editor.md`.

## UE5-aligned editor direction

The MVP authoring unit is a reusable entity asset, currently named `Prototype`. It is the equivalent of a UE5 Blueprint Class: it owns component composition and component defaults, while scene Actors are instances created from it.

Components are authored on the open Prototype asset. Selecting an instance in the World Outliner must not replace the open asset context or hide its Components panel. Instance selection belongs to the scene and runtime inspection context.

A separate Behavior asset is treated as reusable behavior attached to a Prototype, analogous to a UE5 Actor Component Blueprint. Its graph is edited as an asset, while the Prototype determines which behavior assets its instances receive. This preserves ECS composition and per-entity runtime instances without making the editor’s selection model asset-dependent. ^[inferred]

The implementation is phased so each slice can be validated independently:

1. Author a Prototype and add or remove its Components.
2. Edit Component defaults and verify them on new and existing instances.
3. Create and attach reusable Behaviors through the Prototype authoring context.
4. Preview one Prototype and its Behaviors in an isolated runtime world.
5. Add Prototype instances to a scene and inspect or override instance state.
6. Add the broader Blueprint graph workflow only where the MVP needs it.

## Superseded directions

ADR 0011 amends the one-run ownership assumptions in ADRs 0001, 0006, and 0008. ADR 0021 replaces the earlier mixed workbench direction and removes generic custom Event authoring from the current editor slice. Those older documents remain useful for history but do not define current ownership. (docs/adr/0011-persistent-blueprint-instances.md:71-83; docs/adr/0021-prototype-shell-and-live-runtime-editor.md:178-228)

## Related

- [[projects/c-blueprints/concepts/runtime-evaluation-and-identity]]: runtime ownership and evaluator split.
- [[projects/c-blueprints/concepts/ecs-prototypes-and-behavior-composition]]: consumer-layer decisions.
- [[projects/c-blueprints/concepts/live-authoring-and-runtime-targets]]: live editing consequences.
- [[projects/c-blueprints/skills/validation-workflow]]: acceptance checks.
