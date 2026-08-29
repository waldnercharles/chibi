# Agent handoff: unified Systems, Behaviors, queries, and component identity

## Objective

Continue the editor/runtime transition toward NeonSwarm. ECS Systems and prototype
Behaviors must use the same Blueprint graph language, node descriptors, graph IR,
and JIT backend. A System is the batch/query form; a Behavior is primarily
per-entity scheduling/lifecycle sugar. Everything authored as data must be
editable, saved, undoable, and revertible in the editor.

## Current state

Recent commits:

- `6c762015` Boolean ECS query expressions.
- `7c82e294` Authored filters applied to JIT Systems.
- `8ab786e1` Boolean query regression test.
- `f2380539` Query expression inspection API.
- `58794d64` Named query JSON serialization.
- `5243db3e` Math nodes and System graph Details.
- `173a7f3e` Authored System filter test.
- `4b71903e` Component identity manifest support.
- `59be9de8` Stable component keys in prototype assets.
- `4774fe8d` Automatic manifest registration for new components.
- `83104af9` Persisted System document model.
- `b9cac7ad` App query filter loading/editing/saving.

Current passing checks include:

```sh
./build/query_expression_check
./build/movement_system_check
./build/prototype_authoring_check
cmake --build build --target app
build/app --headless --screenshot --frames 3
```

Latest screenshot dimensions: `3840x2160` at `/tmp/c-blueprints-screenshot.png`.

The application is still the movement-demo host. `app/main.c` uses
`movement_authoring`; the richer `app_scene_authoring` path is excluded from the
shipped app in CMake. Do not mistake the existing Scene panel for the complete
Scene/Prototype/Behavior editor.

## Important architecture decisions

### Shared execution

`game/system.c` currently owns a System-specific compiled program with typed
bytecode and SLJIT paths. `game/behavior.c` still executes through
`blueprint_instance`. This is the principal runtime split to remove.

Target shape:

```text
Blueprint -> validation -> shared graph IR -> shared graph_program/JIT
                                      -> batch/query execution adapter
                                      -> entity/continuation execution adapter
```

Both forms must use the same node descriptors, pins, IR operations, direct
adapters, diagnostics, and JIT lowering. Runtime adapters supply the entity,
component access, event inputs, persistent state, command buffer, and debug
context. Retain an interpreter fallback only for unsupported graphs while
keeping it clearly a fallback.

Systems remain first-class. They are appropriate for uniform bulk work such as
movement, physics, projectile updates, lifetime, and rendering preparation.
Behaviors are appropriate for per-entity state, timers, latent flow, hit/death
reactions, and other continuations. Shared durable state belongs in ECS
components, not private Behavior variables.

### Queries

`ecs/ecs.h` and `ecs/ecs.c` now provide owned nested expressions:

- `ECS_QUERY_ALL`
- `ECS_QUERY_ANY`
- `ECS_QUERY_NOT`
- `ECS_QUERY_HAS`

Use `ecs_query_begin_expression()`. Simple component-list queries remain
optimized. Expression queries currently scan high-water-mark slots in
ascending order; add candidate-index optimization only after correctness.
Expression ownership is explicit; `ecs_query_expr_add()` transfers no documented
ownership except that the parent owns children when freed.

`game/query.c` serializes expressions as named JSON objects. Resolvers should
resolve both stable keys and legacy display names. New persisted data should use
stable keys.

`game/system_document.c` wraps a Blueprint and optional query. It currently
appends a `"query"` member to Blueprint JSON when saving and extracts the query
with yyjson when loading. Preserve backward compatibility with documents that
have no query.

The app currently exposes only one temporary authored filter (`GameHealth`) in
the System pane. Replace it with a recursive query builder.

### Component identity

Reflection historically generated IDs by hashing the current component name.
The reflector now accepts `--manifest`, and the game manifest is
`game/component_ids.tsv`. Entries contain:

```text
source-file line permanent-id stable-key current-name
```

Generated `ecs_component_desc` now has `.key`. Existing IDs are preserved in
the manifest. New components are automatically appended. Same-file/same-line
continuity preserves identity across ordinary renames; exact historical name
matching is also supported. Prototype serialization accepts names and keys but
writes keys when available.

This is intentionally tool-owned; consumers should not author redirects.
However, source-line continuity is still fragile for declaration moves and
ambiguous duplicate layouts. Improve it with declaration fingerprints and
manifest history before relying on it for large external consumers.

## Remaining implementation tasks (ordered)

### 1. Finish persisted System document integration

- Make the app retain a `game_system_document` as the System asset model instead
  of loading a raw Blueprint plus separately reparsing JSON.
- Load and save query state through the same document dirty/undo boundary as the
  graph.
- Ensure Save, Save As, Reload, Save All, Undo, Redo, and Revert preserve both
  graph and query.
- Keep atomic writes; do not replace them with direct `fopen` writes.
- Add explicit document versioning for System JSON.
- Add tests for absent query, nested query, invalid operators, unknown keys,
  Save/Reload, and dirty-state transitions.

### 2. Replace the temporary query checkbox with a real query builder

Implement an editor-owned query tree model or a small game/editor bridge with:

- `All`, `Any`, and `Not` groups.
- Add/remove child terms.
- Component/tag search picker.
- Stable selection and inline rename-free editing of operators.
- Empty-group validation (`Any` false, `All` true only if deliberately allowed).
- Query preview count against the active ECS world.
- Diagnostics for missing components/tags.
- Reorder children deterministically.
- Undo transaction per query edit.

Do not put ECS data into `editor/ui_shell.c`; the shell remains theme/docking
only. Use direct calls/switches rather than a generic C++ widget hierarchy.

### 3. Add prototype identity tags

- Add a generated stable prototype/tag identity mechanism.
- Do not derive persistent identity from the visible prototype name.
- Apply the tag at spawn and propagate prototype identity changes correctly.
- Make tags visible in the Inspector and query picker.
- Add tests for rename, duplicate, spawn, and query matching.
- Prefer a unit/tag component initially; consider a stable `PrototypeId`
  component or relationship model later if one-component-per-prototype becomes
  too expensive.

### 4. Integrate the full document-based editor

The shipped app currently has `System`, `Entity`, `Scene`, and `Node Canvas`
movement-demo surfaces. Migrate it to the existing document/context direction:

- Scene document: World Outliner, Scene viewport, universal Inspector, runtime.
- Prototype document: Components, attached behaviors, Preview, Inspector.
- Behavior document: graph, My Blueprint, Inspector, diagnostics.
- System document: query/details, graph, Inspector, diagnostics/profiling.
- Shared graph renderer and node shape across System and Behavior documents.
- Remove the hard-coded Player `Entity` panel after universal Inspector coverage.
- Retain System Details/Query, but remove demo-specific MovementSystem wording.

`app/scene_authoring.c` already contains much of the prototype/scene model but
is excluded from the app target. Wire it in only after resolving ownership and
lifetime carefully.

### 5. Unify System and Behavior compilation/JIT

- Extract common compilation/program storage from `game/system.c` into a shared
  graph program module, likely under `game/` or `core/` depending on dependency
  boundaries.
- Preserve current movement System JIT performance and batch SIMD path.
- Add an entity execution adapter using the same IR/JIT.
- Define resumable/latent program state; do not put a suspended continuation
  on the C stack.
- Make event inputs and owner entity explicit ABI values.
- Keep component access and command buffering consistent for both modes.
- Add a test executing the same graph through batch and entity modes and compare
  component results, diagnostics, and backend selection.

### 6. Improve query execution performance

After query semantics are stable:

- Compile expression plans into required/excluded terms and OR groups.
- Select the smallest candidate occupancy index.
- Union OR candidates with deterministic deduplication.
- Preserve ascending slot order.
- Ensure query planning does not invalidate the JIT column assumptions.
- Add benchmark coverage against simple AND queries.

### 7. Complete reflected identity migration

- Add a shape/declaration fingerprint independent of the component name.
- Match declaration moves using fingerprints and one-to-one candidate checks.
- Detect ambiguous matches and fail with an actionable migration report.
- Add alias/history fields to the manifest.
- Add registry validation for duplicate stable keys and IDs.
- Update generated node/reference resolution so renamed component nodes can load
  old graphs; stable component keys should be the resolution identity, not
  `Get_<display-name>` strings.
- Add hot-reload tests for rename, layout-compatible change, incompatible field,
  declaration move, and ambiguous match.

### 8. NeonSwarm data migration

Move gameplay data out of hard-coded C tables into reflected/editor assets:

- Player stats and components.
- Enemy prototypes and behavior parameters.
- Wave definitions and spawn schedules.
- Arena and spawn configuration.
- Weapons, projectiles, damage, XP, progression, and support definitions.
- Materials and visual parameters where they are data.

Runtime systems may remain C implementations, but their tunable data and
composition must be editor-owned and saveable/revertible.

### 9. Domain nodes after the foundation

Use the current movement node shape as canonical. Add only after shared
execution and query work:

- Vector math: length, normalize, distance, clamp, rotate.
- Entity/prototype spawn and destroy.
- Query iteration and target entity access.
- Timer/random/world-time nodes.
- Collision/contact events.
- Damage/death/XP operations.
- Projectile and wave-spawn operations.

Every new node must be added to its registry pack and every UI context
allowlist/filter. Verify spawn-menu discoverability by name.

## UX removals / cautions

- Do not reintroduce separate duplicate Prototype Details or Runtime Debugger
  windows; use the universal Inspector and bottom diagnostics.
- Do not expose scene-local Behavior composition if Prototype owns the inherited
  behavior/tag set.
- Do not expose generic fixed-pin type/container editing.
- Do not silently equate Compile, Save, Apply, Restart, and Reload. They are
  distinct boundaries and must be labeled.
- Do not show Prototype Preview labels on a Scene Runtime viewport.
- Do not silently auto-map ambiguous component renames.

## Validation requirements

For C changes:

```sh
cmake --build build --target <focused-target> -j4
./build/query_expression_check
./build/movement_system_check
./build/prototype_authoring_check
```

For editor/UI changes:

```sh
cmake --build build --target app -j4
build/app --headless --screenshot --frames 3
```

Inspect `/tmp/c-blueprints-screenshot.png` and report exact dimensions. Run
`./build/self_check` and `./build/integration` when changing shared core,
serialization, or registry behavior.

Before committing, check `git status`. Existing unrelated changes should not be
silently discarded. Make focused commits with descriptive messages.
