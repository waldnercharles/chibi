# c-blueprints Editor/Runtime Transition Specification

## Purpose

Continue the NeonSwarm editor/runtime transition while preserving authored data, stable identity, and explicit document boundaries. Systems are batch/query assets; Behaviors provide per-entity lifecycle and resumable continuation behavior. Both use the same Blueprint graph language, descriptors, graph IR, diagnostics, and JIT backend.

## Product requirements

1. **System assets**
   - `game_system_document` is the authoritative System asset model.
   - Graph state and optional nested query state survive versioned atomic Save, Save As, Reload, Save All, Undo, Redo, and Revert.
   - Documents without queries remain backward compatible; invalid operators and unknown keys fail clearly.
2. **Query authoring**
   - The editor owns a recursive query tree supporting All, Any, Not, component/tag selection, search, validation, diagnostics, preview counts, deterministic reorder, serialization, and one undo transaction per edit.
3. **Stable prototype identity**
   - Prototypes/tags receive generated identity independent of display names.
   - Spawn, Inspector, query selection, rename, duplicate, and round-trip behavior preserve identity and matching semantics.
4. **Document workflows**
   - Scene, Prototype, Behavior, and System documents use their document models, shared graph presentation, universal Inspector, diagnostics, and explicit save/apply/reload boundaries.
   - No demo-specific MovementSystem wording or shell-owned domain state.
5. **Shared execution**
   - System and Behavior compilation share program storage, graph IR, descriptors, adapters, diagnostics, and JIT lowering.
   - Entity execution has explicit resumable state and never relies on a C-stack continuation.
6. **Query performance**
   - Expression queries plan candidate indexes and OR unions with deterministic deduplication and ascending slot order, without invalidating JIT column assumptions.
7. **Component identity migration**
   - Reflected components use declaration fingerprints plus manifest history/aliases.
   - Rename/move/layout fixtures produce stable resolution; incompatible, ambiguous, and duplicate identities produce actionable diagnostics.
8. **Authored gameplay data**
   - NeonSwarm tuning and composition data move from hard-coded C tables to reflected, editor-owned assets with explicit save, apply, reload, and revert behavior.
9. **Domain nodes**
   - Add vector/entity/query/timer/event/gameplay node families only on the shared foundation.
   - Every node is registered, allowed in every applicable UI context, stably resolved, and discoverable by name.

## Constraints

- Follow `AGENTS.md`; keep code Spartan, ownership explicit, and registries/allowlists complete.
- Preserve unrelated worktree changes and existing scene/prototype formats.
- Visible names are not persistent identity; use stable keys for components and prototypes.
- Keep ECS/domain data out of `editor/ui_shell.c`; separate shell/layout from models.
- Keep Compile, Save, Apply, Restart, and Reload semantically distinct.
- Do not silently map ambiguous renames or introduce generic future registries/widget hierarchies.
- Implement in small, reviewable changes. One focused item and commit per iteration.

## Delivery plan and acceptance gates

Implement in dependency order: persistence/query foundations, prototype identity, document workflows, shared execution, query planning, component identity migration, authored data migration, then domain-node breadth. Each slice must include focused tests and pass all repository gates:

```sh
cmake --build build --target app -j4
./build/self_check
./build/editor_runtime_check
./build/prototype_authoring_check
./build/integration
./build/registry_consumer
python3 tools/spec_coverage.py
build/app --headless --screenshot --frames 3
```

For UI changes inspect `/tmp/c-blueprints-screenshot.png` and record exact dimensions. A slice is complete only when its acceptance behavior is covered, gates pass, its progress entry records changed files and evidence, and the focused commit contains only that slice plus required Ralph state.

## Subagent protocol

- Before coding, inspect the current branch, `AGENTS.md`, `.ralph/items.json`, and recent progress.
- Claim one unfinished item only; do not edit immutable source documents or unrelated items.
- Use a worktree for independent slices. Report findings, changed files, tests, assumptions, and blockers.
- A coordinator reviews and integrates completed slices in dependency order, reruns the full gates, and updates `.ralph/items.json` and `.ralph/progress.md` only after evidence.
- If blocked by credentials, external services, or unavailable hardware, use a safe local fixture/mock where it honestly covers the requirement; otherwise document the blocker without weakening gates.

## Definition of done

All nine items in `.ralph/items.json` pass, the repository remains buildable, source documents remain unchanged, every required gate is green, UI captures are inspected where applicable, and each item has a focused implementation commit and progress handoff.
