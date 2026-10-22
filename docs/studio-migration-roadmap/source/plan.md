# Chibi Studio in .chibi

> Historical record: this document describes an earlier source format and toolchain.
> Use the current language and authoring guides for executable instructions.
> Suffixes in prose follow current conventions; linked Git revisions retain the exact historical files.
## Migration plan and language-readiness review

15 October 2026 · Revision 2 · Implementation authorized; staged readiness gates apply

**Reading guide:** section 1 gives the recommendation and decisions; sections 2–5 establish the baseline, parity contract and architecture; sections 6–10 cover language, performance, formatting and syntax; section 11 is the implementation sequence. Appendix A accounts for every native source/header/test boundary. All source links are pinned to the audited commits unless labeled as PR or upstream documentation.

## 1. Recommendation

Build the next Studio on current main, reuse its authored RML/RCSS and working Outliner integration, and migrate application behavior in independently reversible slices. Establish language semantics, ownership, host contracts and measured performance gates before translating the rest of the application.

The destination is `.chibi` first: attempt to author every applicable Studio responsibility in `.chibi`, and improve the language/compiler/runtime when a missing facility blocks a good translation. Studio's data, algorithms, product behavior, state, commands, document workflows and UI composition belong there. A wrapper around the old C implementation is not a completed migration. Retained C needs a concrete OS/foreign-ABI/crash-handling constraint or a measured low-level kernel reason, and only the irreducible portion is exempt. A filename, an existing SDK location or a general claim that C is faster does not establish an exception. The engine SDK remains a separate dependency, but its known operations should be compiler-visible and cheaply callable rather than automatically routed through a generic bridge. A wholesale engine rewrite is not required to start; any newly authored engine/app mechanism should follow the same `.chibi`-first direction.

“Like a professional React application” should mean cohesive feature modules, reusable components, typed inputs and events, clear state ownership, derived views, lifecycle cleanup and strong tests. Keep RML/RCSS as the rendering contract. A new JavaScript runtime, virtual DOM, hooks framework or JSX grammar is unnecessary to get these benefits.

The critical path is:

1. Freeze the parity oracle and inventory every native responsibility.
2. Settle a small language contract: application records, collection operations, module boundaries, error values and native-handle lifetimes.
3. Fix or disprove the concrete ownership and performance risks, and make measurement and formatting dependable.
4. Prove one complete authored feature with state, transactions, UI, reload and native/web backend parity.
5. Convert low-risk panels, then document workflows and complex surfaces, deleting their C policy as each slice passes.
6. Switch the default only after the full agreed product matrix and native-boundary audit pass.

### 1.1 Decisions recorded after the first plan

The owner authorized implementation on 15 October 2026 after asking that these decisions be incorporated into the plan. Begin the prerequisite work and staged migration below. This is not approval of unresolved syntax alternatives, altered product behavior, failed gates, quarantined tests or an unrelated held PR.

- **Authored-first endpoint:** try `.chibi` and the required language improvement before approving a native exception. The inventory's SPLIT/NATIVE_EXCEPTION labels are candidates to investigate, not permanent exemptions.
- **Ease and performance:** aim for the approachability of TypeScript/Lua and the performance of well-written C. These are design and measurement targets, not current whole-application performance claims. Ordinary authors do not select allocators, pass arenas, manually free values or add optimization annotations to make normal code usable. The implementation chooses storage, borrowing, reuse and safe parallelism internally.
- **Language readiness:** implement useful features before the first slice that depends on them or alongside a bounded specimen that proves them. Structs, capturing lambdas, efficient collections and AoS/SoA are active design work, with the dependency order in sections 6 and 10. Do not defer a demonstrated need merely because today's C facade can hide it.
- **Engine-aware compilation:** exploit engine type/layout/effect/ownership knowledge. Prefer direct checked operations, specialized query bodies and scalar/typed native calls over generic tagged-value bridges; keep the generic path for genuinely unknown or unsupported operations. See section 6.6 for exact current paths and first work.
- **Authoring style:** bare member names are the default; `self.` appears only when necessary to select the intended binding. Remove genuinely optional `state` spelling safely. Capture syntax, record defaults/layout, storage spelling, receiver resolution and persistent callable declarations still have explicit choices in section 10. No grammar-changing choice is silently approved by the implementation request.
- **Formatting:** one authoritative configurable API for Studio/CLI/check/editor integrations; separate layout formatting from semantic canonicalization. Compare extending the current printer with a real Prettier plugin or Tree-sitter formatter engine before choosing. A thin wrapper alone cannot supply missing layout controls. See section 8.
- **No speculative platform projects:** LLVM, a general package registry and JSX were examples of unproven future scope, not recommendations or migration requirements. They are not in this implementation roadmap.
- **Parity and evidence:** exact deterministic pixels, input/state/document behavior and the three-way SDK comparison remain the contract. Set numeric performance budgets from measured baselines on selected platforms; do not invent them.

### 1.2 Remaining owner decisions

Review the small, example-based syntax decisions in D1/D2/D3/D6/D9/D10 before implementing their visible syntax: nominal struct spelling/defaults/managed fields; dictionary/builder spelling if needed; import aliases if needed; lambda capture and callable-retention spelling; AoS/SoA selection; and the exact `self`/persistent-state resolver and strict migration policy. Internal compiler optimization, contract tests, source inventory, formatter comparison and the parity harness can proceed without choosing those alternatives. Any product correction or native exception beyond the concrete boundary justification must be recorded separately.

### 1.3 Revision 2 change log

This revision strengthens the authored-first endpoint and updates native-candidate interpretation; moves useful language work before/alongside dependent slices; makes allocator strategy internal; adds an engine-aware lowering plan with native/Wasm and publication safeguards; clarifies actual FFI ownership costs; specifies configurable formatter selection and integration; removes LLVM/registry/JSX from planned scope; and replaces the planning hold with an authorized, dependency-gated implementation start. The original complete file/branch inventory and source baselines are preserved. No repository implementation or product benchmark was performed while preparing this revision.

## 2. Baselines, evidence and branch choice

### Exact source snapshots

| Repository | Commit | Tree | Use |
|---|---|---|---|
| Chibi engine | `02f8453211dd4cc57965ba52c645f08a92e53c5e` | `2e568f24f2833dfeec3103f63a5068500b0fa4f8` | Audited main; original complete Git checkout plus targeted connector rechecks |
| Chibi Studio | `78cb4d4c47242db9f7af2b9573653d9756050e99` | `a91bebf8c9226c4ef974e8fd061db34082ac0755` | Current main, complete tree inventory plus targeted source reads |

Both main refs were rechecked through the GitHub connector on 15 October at approximately 03:45-03:48 UTC and are unchanged from the original audit. Studio's recursive tree was untruncated. The source-file manifest and CMake source lists reconcile to **155 product native translation units: 154 C and one C++ shim**. There are **82 additional native test/oracle units and 79 headers**, for 316 native source/header paths in the tree. These are path/build counts, not a claim that every file body has been fully audited. The detailed per-file inventory records that distinction. [Studio build ownership][S-build] [All library translation units][S-sources]

This review did not build Studio or Chibi, resume the paused build, execute a suite, benchmark a workload, install a formatter or modify either repository. All performance conclusions here are structural findings or proposed measurement gates. Existing test sources and historical PR reports are evidence of coverage intent, not current-head passes.

### What is already migrated

Studio's markup and styling are already extensively authored assets. The live project has ten `.chibi` behaviors: Dismiss, NavKeys, NewProjectKeys, OutlinerHeaderInput, OutlinerInput, OutlinerMenu, OutlinerRename, OutlinerRows, VirtualList and WidgetGallery. The Outliner is partly behavior-driven, but still calls native operations for scene queries, selection, rename, visibility, hierarchy moves and other policies. Most other panels still receive native view models and actions through a table of **44 panel sources**. Therefore this is primarily an application-logic and state migration, not a new UI toolkit or a markup rewrite. [Studio UI lifecycle][S-ui] [44 panel sources][S-source-table] [Current Outliner behavior][S-outliner] [Native Outliner operations][S-node-calls]

The Outliner source is a useful compatibility specimen, but is not the desired style template. It retains graph-generated expressions, quoted helpers, explicit conversions and legacy-looking continuation structure. Validate it against the current compiler, then demonstrate a clean, idiomatic version with the same traces before generalizing the pattern. Do not hand-simplify it without semantic comparison. [Current Outliner behavior][S-outliner]

### The earlier branch

The strongest identified historical line is engine PR **#147**, `cwaldner/editor-polish-design-4679ad`, head `e49b5fcc6e32c71d9a58eff0055bb8468166ac81`. It introduced authored RML/RCSS and the behavior-driven Outliner and was merged on 5 October. Its live branch ref still points to that head. Local ancestry confirms all its commits are already in current engine main: 1,244 commits behind, zero unique commits. PR **#169**, `codex/cut-58-migration-audit`, head `a19b1ff4ff3c6b7c9814379633d9025887a7a08c`, is also merged and an ancestor, 1,187 behind with no unique commits. The older ImGui-to-RmlUi branch is also an ancestor, 2,059 behind. [Engine PR #147][P147] [Engine PR #169][P169]

Studio's 11 current branches and all 65 returned PRs were inspected by name and metadata. None identifies a newer full C-to-`.chibi` migration. Two potentially confusing branches are unrelated: `cwaldner/seven-option-menus` is one menu-layout commit ahead/three behind main, and the closed Outliner fix branch `claude/project-thread-x5ac39-2` is one commit ahead/214 behind. The two pinned-editor branches are deployment-related. This establishes the likely historical work; it does **not** prove which branch the owner meant if another unpublished/local branch exists.

**Recommendation:** implementation is authorized; start a fresh Studio branch from the then-current Studio main and a separate engine branch for required language/SDK changes. Reuse the existing authored assets and proven contracts. Preserve the old branches as evidence; do not transplant a pre-extraction engine worktree or reintroduce removed modules. Before implementation, ask for the exact branch only if the owner meant a different, unpublished line.

The repository migration record still contains its earlier hold on other-panel conversion and labels old timings/checklists historical. The owner's latest request authorizes this staged migration and its prerequisites; update that record accurately when implementation begins, without treating the old checklist as current evidence. Broad panel conversion still waits for the readiness and vertical-slice gates, and unrelated explicit holds remain in force. Do not revive old full-suite commands, old bug claims, MCP tooling, or the old assumption that reparenting is missing. Reparent/reorder, grouped history and path-preserving undo already exist. [Historical migration and current hold][E-migration]

## 3. The acceptance contract

### What “identical” includes

- Layout, colors, fonts, icons, density, clipping, stacking, transitions, reduced motion and all visible interaction states.
- Focus and keyboard routing; standard/Vim editing; IME and Unicode; shortcuts and user overrides; pointer capture; touch/trackpad behavior; drag thresholds and cancel paths.
- The same command labels, availability, checked state, selection identities and ordering, filtering, picking, navigation and multi-window/area behavior.
- Source/graph round-tripping, diagnostic ranges, formatting, breakpoints, watches, stepping, source identities and the last-valid-generation behavior of invalid edits.
- Document bytes, dirty flags, undo/redo grouping, redo preservation after no-ops, checkpoints, recovery, project import/export, saved preferences and asynchronous browser persistence.
- Play/pause/step/stop, keep changes, scene hierarchy, entity-path references, physics resets, previews, materials, thumbnails and collaboration replay.
- Startup, idle scheduling, failure messages, retry behavior, clean teardown and supported native/browser packaging.

Use current main as the product oracle, not the older migration branch. Freeze the oracle's engine SDK, Studio source, assets, fonts and toolchain together. First verify that the baseline itself opens its projects correctly. A historical report on Studio #68 records a Studio-project font-variable/fallback preparation error; reproduce it on the chosen baseline and resolve or explicitly disposition it before relying on that project as a golden fixture. It is not a verified current failure from this review. [Studio PR #68][P68]

A known baseline defect is not permission to change product behavior during migration. Record it, reproduce it, and get approval for a separate correction or a deliberate revised oracle. Conversely, preserving undefined behavior, crashes or data loss is not a meaningful parity target: document the exception and its approved replacement contract.

### Three distinct comparison layers

1. **Domain parity:** feed identical commands/events to native and authored implementations in isolated worlds. Compare stable IDs, source bytes, selection, document/history state, mutations, event order, errors and persistence requests after each step.
2. **Interaction parity:** replay real platform input through the application boundary, not just direct command calls. Compare focus, hover, capture, popover stack, selected rows and action traces. Include repeated, interrupted and out-of-order work.
3. **Rendering parity:** capture the same deterministic frame states on the same platform/backend and compare exact pixels. Freeze clocks, animation progress, random seeds, viewport, DPI, fonts and GPU settings; record renderer/driver identity. Different platforms get separate goldens. Any nondeterministic mask or tolerance needs an explicit reason and approval.

Do not double-execute external effects during shadow comparison. The candidate uses isolated state and a recording/fake service for file writes, export, network/collaboration and OS dialogs. Replay the recorded results into both sides. Real persistence and platform effects receive separate single-owner acceptance tests.

## 4. Target architecture

### 4.1 Boundaries and dependency direction

Use a small, boring architecture whose modules can be read without understanding the entire editor:

- **Application composition:** startup, workspace lifecycle, feature registration, command registry and explicit service assembly.
- **Domain modules:** documents, assets, scene editing, selection, history, references/refactoring, preferences, playback, collaboration and jobs. They own invariants and do not depend on RML elements.
- **Feature controllers:** Outliner, Assets, Properties, Members, Output, code, graph, scene viewport, Canvas/Designer, Inspector, settings and shell. They translate typed intents into domain commands and derive view models.
- **UI components:** RML + RCSS + a small `.chibi` behavior where interaction logic is needed. Inputs and events have documented schemas; styling uses existing theme tokens and widgets.
- **Platform/SDK adapters:** narrow, typed service boundaries for filesystem/browser persistence, dialogs/clipboard, text surfaces, renderer resources, authoring/compiler services and transport. The adapter implements mechanism; authored code decides product behavior.

The dependency direction is UI → feature → domain → abstract service. Platform implementations satisfy services at composition. Domain code cannot import a panel or reach into a global `Studio` object. Shared game-capable components live in an explicitly packaged shared UI/runtime module; Studio-only document APIs stay in an editor capability pack that ordinary games cannot acquire implicitly. The existing SDK already explicitly separates capability catalogs, executable modules, live programs and application-bound services. Preserve that architecture. [Engine architecture][E-architecture]

### 4.2 Proposed source organization

This is a logical organization; phase 1 must verify the source catalog's discovery/namespace rules before moving files. Existing `content/behaviors`, `content/libraries`, `content/components`, `content/ui`, prototype and scene conventions remain valid until that contract changes.

```text
studio/
  app/              Bootstrap, WorkspaceLifecycle, CommandRegistry
  domain/
    documents/      DocumentStore, SaveState, History, Checkpoints
    scene/          SceneEdits, Hierarchy, Selection, Playback
    assets/         AssetIndex, Refactor, ThumbnailPolicy
    settings/       Preferences, Keymap
    collaboration/  Actions, Replay, Presence
  features/
    outliner/       Controller, Selectors, ViewState, Events
    properties/     Controller, FieldModels, EditSession
    assets/         Controller, Search, GridAndList
    code/           Controller, Navigation, Diagnostics, Debugging
    graph/          Controller, Interaction, Presentation, Layout
    canvas/         Designer, Hierarchy, Details, Inspector
    shell/          Menus, Tabs, Workspaces, Palette, Prompts
  ui/
    components/     TreeRow, Menu, Field, List, Dialog, Toast
    theme/          Existing RCSS tokens and roles
  services/         Typed contracts; no product policy
  tests/            Fixtures/scenarios consumed by C/pico_unit drivers
native/
  bootstrap/        Minimal process/window entry
  adapters/         Foreign ABI and platform mechanisms
```

Do not copy the entire C filename graph into `.chibi`. Preserve the proven responsibilities while splitting the current large `studio_sources.c` and central `studio_internal.h` responsibilities into narrow interfaces. Keep one implementation owner per module during conversion. A native header that disappears becomes a public authored module contract or is deleted; it must not become an equally large global script registry.

### 4.3 State ownership

| State | Owner | Persistence/lifetime |
|---|---|---|
| Document source, working semantic model, saved revision, undo/redo | Document domain | Document lifetime; checkpointed with exact IDs and value types |
| Applied executable generation | Engine/source publication service | Last valid generation; separate from invalid draft |
| Scene authoring objects and selection | Scene domain, represented with stable IDs/ECS handles | Workspace lifetime; no pointer identity in serialized history |
| Panel filter, collapse, scroll, rename/edit session | A concrete view instance | Explicit per-instance owner; any existing shared observable state remains a deliberate shared model until an approved change |
| Derived rows, field models, menu choices | Feature selector/cache | Revision-keyed, bounded, disposable; not a second authority |
| Preferences/keymap/theme choices | Preferences domain | Saved only through explicit persistence; retain unknown supported keys |
| Focus, hover, capture and popup mechanics | UI runtime | Element/mount lifetime; reconciled with view identity |
| Jobs and pending requests | Structured job owner | Cancel or discard on owner/generation change; completion cannot target a destroyed view |

ECS is first class where entities, components, relationships, queries and revision tracking fit. It should not force every string buffer or undo entry into a game entity. Prefer dense typed data for hot iteration, IDs for relationships, and owned document/collection stores for variable-sized state. A boxed object hierarchy or a JSON tree as the universal model would undermine the performance goal.

Current mounts select a panel entity by panel name and index; multiple mounts can therefore share that entity's behavior state. Make that ownership explicit. First trace which state is currently shared and which is already per view. The target architecture should support keyed per-instance view state and deliberately shared domain state, but changing observable coupling requires a separate approved behavior decision. Preserve the frozen multi-area contract; do not silently make a shared filter independent, or remove the ability to show two areas. Add duplicate-view filter, scroll, focus and rename parity tests. [Current mount identity][S-ui-mount]

### 4.4 Data flow and transactions

The default path is:

```text
platform input → typed UI intent → domain command/transaction
→ authoritative mutation + revisions → affected selectors
→ one batched model publication → retained UI update/layout/render
```

Commands are the shared entry for menus, shortcuts, palette, UI buttons and accepted collaboration actions. Availability and checked state are derived from the same domain facts the command validates. No handler may write history, dirty state and scene values independently.

Preserve the current edit protocol: intermediate `input` events update a live edit session; `change` commits one undo step; Escape/focus loss/hidden view cancels according to the existing contract. Empty/no-op changes preserve redo. A multi-field or multi-object operation validates its entire target set before committing, and failed replay leaves values and stacks unchanged. Existing `scene_history` contracts provide an unusually strong starting point. [Scene history invariants][S-history]

Prefer granular revision-driven selectors and bounded visible windows. The current source layer already suppresses hidden-panel work, dirties a source only when sync changes it, and uses distinct variables for independently changing selection and rows. Keep and improve these measured contracts. Do not replace them with full-tree cloning or per-frame JSON rebuilding. A selector declares its dependencies; invalidation is conservative if a native inspector lacks complete revision metadata. That limitation must remain observable until fixed. [Source synchronization][S-source-sync]

### 4.5 Components without hidden framework cost

Keep existing `<use ui="TreeRow">`/aliases, RML events, scoped RCSS and native retained elements. A component has a stable identity, typed props/view model, named events, an optional controller and explicit attach/detach cleanup. RML remains declarative; command logic belongs in `.chibi`, not long binding-expression strings.

Do not introduce render-time side effects, dependency-array conventions, implicit hook ordering, unbounded closure captures or an always-running reconciliation pass. Compile-time dependency checks and explicit revisions are preferable to runtime magic. Named functions and typed events already support reusable panels. Captured lambdas can improve local callbacks, selectors and algorithm APIs; assess that requested feature with explicit ownership and cost rather than requiring a closure allocation for every render or event.

## 5. Native boundary: what remains and what must move

The endpoint is defined by responsibilities rather than a percentage of converted lines. Every product translation unit is classified in the companion inventory and summarized below. All proposed native portions are provisional candidates: first attempt an authored design and identify the necessary language/compiler support. Confirm the exact function-level split immediately before implementing the slice. Where a real OS/foreign ABI or crash-safety boundary exists, retain only its irreducible adapter; move transferable orchestration and algorithms to `.chibi`.

| Current area | Authored destination | Candidate native boundary, requiring justification |
|---|---|---|
| `studio_*` panels, shell, commands, settings, models | Feature/domain `.chibi`; view composition, event policy, selectors and state | Thin data/model/render service adapters only |
| `documents/*` | Document transactions, save/dirty policy, histories, navigation, source editing and recovery | Shared SDK parser/compiler/reflection and efficient text/value buffers |
| `workspace/*` | Workspace replacement, scene editing, hierarchy, playback/keep-changes, selection, camera/gizmo interaction policy | OS event pump, engine world/renderer resources, low-level picking/math kernels where measured |
| `graph/*` and graph panel files | Search, selection, presentation, navigation, gestures, layout policy | Retained drawing/geometry primitives and shared graph/source SDK operations |
| `collab/*` | Action schema, permission/ordering policy, replay, checkpoint coordination and presence presentation | Socket/browser transport, serialization codecs where justified |
| `rmlui/kit*` | Reusable interaction/component policy and composition | Actual RmlUi handles/layout/draw submission; measured reusable geometry or text surfaces |
| `text/zep_bridge.cpp` | Editor product commands, completions, diagnostic/navigation policy outside the bridge | Pass-through C++ ABI adapter and existing Zep editing/rendering mechanism, if approved |
| `host/*`, registry, diagnostics | Launch configuration and application decisions | Entry ABI, crash/stack integration, OS primitives, capability registration and profiler bridge |
| Tests, held oracles, build/tooling | Keep independent test harnesses and shipping toolchain | C/pico_unit drivers remain C under current repository policy |

`studio_host.c` and `application.c` must not be exempted simply because their names say “host.” They contain routing, preferences, idle scheduling, workspace replacement and guest-Studio policy. Similarly, retaining all of `scene_runtime.c`, history or collaboration behind one opaque handle would leave substantial application behavior in C. Split mechanism from policy explicitly. [Application orchestration][S-app]

The inventory's baseline candidate disposition is **122 migrate, 29 split, four candidate whole-file native exceptions**. None of the latter 33 labels preapproves permanent C retention. The four are `src/host/state_file.c` (atomic file mechanism), `src/host/stacktrace.c` (crash diagnostics), `src/diagnostics/web_trace.c` (browser profiler ABI) and `src/editor/text/zep_bridge.cpp` (C++ pass-through). This does **not** mean the finished host contains only four native files: extracted adapters from the 29 split units, generated bindings and any retained SDK controls need their own allowlist. The full 155-file disposition and all headers/tests are in Appendix A; the 867-path CSV provides blob identities and coverage levels.

A native exception record must name: exact functions, the authored attempt or concrete reason an attempt cannot cross an OS/foreign ABI/crash boundary, language improvements considered, external dependency or measured cost that requires native code, exposed data/effects, ownership/thread contract, tests, and criteria for later reconsideration. Keep portable algorithm/control portions authored even when the final platform syscall or foreign call remains native. “It is faster” without a baseline is not evidence. If a pure algorithm remains native temporarily, identify it as temporary and retain a faithful authored implementation or reference specimen for comparison.

### Required host contract families

- **Documents/authoring:** immutable snapshots with source revision, diagnostics and stable symbol/node IDs; bounded text edits; prepare/stage/publish; reflection descriptors; canonical serialization. No raw graph pointer escapes.
- **UI:** generation-checked mount/element handles; typed model publication/patching; focus/capture/popover/list commands; batched geometry and text-surface operations. Borrowed element references cannot outlive a callback or survive reload silently.
- **Files/persistence:** project-root-scoped reads, atomic writes, file-change events, import/export streams, durable completion and retry. Browser memory writes and IndexedDB durability are distinct states.
- **Jobs/processes:** start/poll/cancel export, thumbnail and indexing work; typed success/failure/progress; owner and generation tokens; explicit concurrency policy.
- **Runtime/rendering:** create/dispose worlds, prepared generations, inspect/control playback, submit viewport work and retain GPU resources. App code never holds a component pointer across structural changes or suspension.
- **Platform:** window events, clipboard, dialogs, cursor, monotonic time, physical scale and accessibility/native integration. Platform callbacks rejoin the app's designated thread.
- **Collaboration:** authenticated transport is separate from deterministic domain actions. Preserve stable identities, revision checks, atomic replay, resync and checkpoint semantics.

Each engine/native operation needs type identity, size/layout where relevant, argument ownership, result lifetime, aliasing, thread affinity, read/write effects, suspension behavior and allocation/copy-cost metadata. Known engine operations should carry this information into shared typed IR and direct/specialized lowering; the generic capability ABI is the fallback, not a required tax on every engine operation. Unknown effects serialize conservatively. UI, OS and file side effects must never be automatically parallelized because the script syntax looks pure.

## 6. Language capability and readiness

The language is much further along than the original Studio spike. Current main already has a direct source compiler, verified modules, bytecode, native SLJIT and browser Wasm execution, tasks/cancellation, unions, ECS queries, inferred access/effects, reductions, optimization diagnostics and incremental preparation. It would be wasteful to design replacements before testing these implementations against Studio. [Engine architecture][E-architecture] [Current language guide][E-language]

“Implemented” means present in inspected source; it is not a fresh execution result. The following matrix summarizes the current head, including PRs 390–395. A partial feature must pass its missing contract before a migrated subsystem relies on it.

| Capability | Status at the audited head | Evidence and consequence |
|---|---|---|
| Source units and control flow | Implemented | One behavior/library/system/component root; ordinary loops, branching, functions, events and task operations. [Grammar](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/grammar.js#L119-L159). No standalone process entry is implied. |
| Numerics, units, ranges | Implemented | Fixed-width integers, float/double, bool, strings/names, handles; unit checking and reflected bounds. [Value kinds](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/value.h#L10-L63), [unit analysis](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_units.c). Preserve exact widths across UI/serialization. |
| Reflected records and authored ECS components | Implemented with restrictions | Authored components generate aligned native records; container/union fields are rejected. [Component validation](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/component.c#L198-L230), [layout generation](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/declared_components.c#L141-L244). |
| Standalone authored structs | Missing | No general `struct` root/declaration path in the inspected grammar. Add value records before forcing app data into ECS identities or new C registrations. |
| Arrays, sets and indexed maps | Partial for app needs | Arrays/sets exist; updates snapshot retained elements. `map<T>` has integer subscripts, not keyed dictionary semantics. [Array updates](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/arrays.c#L200-L336), [map indexing](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_expression.c#L773-L800). Real dictionaries, nested owned collections and amortized construction need decisions. |
| Value and `ref` semantics | Implemented; editor-scale proof needed | Ordinary values own snapshots; refs write through logical owners. [Snapshot implementation](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.c#L195-L259), [call snapshots](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/call.c#L293-L315). `ref` is not a promise of zero-copy mutation. |
| Named functions and authored generics | Functions implemented; generics partial | Defaults, named/multiple results and refs exist. Non-concrete authored generics are rejected by lowering rather than automatically specialized. [Call lowering](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L1158-L1188), [definition restriction](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L5369-L5370). |
| Lambdas and captured function values | Missing as a general feature | Nested named functions cannot implicitly capture surrounding locals. Internal task/continuation boxes are narrower machinery, not closures. [Capture rejection](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_expression.c#L555-L561). See D6 for the requested proposal. |
| Tagged/structural unions and narrowing | Implemented with restrictions | Exhaustive cases, `is`/`as`, optional values and literal unions exist. Recursive owned unions are rejected; they are not component fields. [Union ownership checks](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_union.c#L59-L100), [runtime specimens](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/program.c#L6798-L6847). |
| Error handling | Partial by API | Runtime failures/status and compile diagnostics exist; expected I/O errors should use concrete unions. No general exceptions/defer in grammar. [Host error contract](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/host.h#L15-L55). |
| Events, tasks and cancellation | Implemented | Cooperative tasks, typed results/cancellation, deadlines and owner teardown exist. They do not provide arbitrary OS threads. [Task implementation](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program_tasks.c#L64-L345), [event schema](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/events.h#L6-L35). |
| Modules and incremental dependencies | Implemented with restrictions | Immutable module generations and dependency-first libraries; cross-library cycles rejected. [Collection/link ordering](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_collection.c#L178-L221). Package visibility/aliases must be demonstrated for app scale. |
| Reload/publication | Implemented; caller-specific contract | Atomic staging and matching-state preservation exist; old generation tasks are destroyed at commit. Invalid drafts do not publish. [Staging](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/behaviors/source_publication.c#L89-L189), [replacement](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program.c#L2732-L2815). Do not promise continuation migration. |
| Typed native services and reflection | Implemented; direct lowering and Studio coverage partial | Registered stable typed ABI/effects and documented ownership, not arbitrary C FFI. Generic execution still uses tagged value banks; nonintrinsic allocation/copy/escape contracts are not yet verified by the cost analyzer (section 6.6). Existing scene services are a foothold; files/documents/jobs/text/rendering need narrow capability coverage. [Host ABI](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/host.h#L10-L134), [annotations](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/annotate.h#L4-L64). |
| ECS queries and effects | Implemented | Component/relationship queries, joins/change filtering, transitive inferred effects and scheduler conflict checks. [Effect analysis](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_effects.c#L293-L381), [scheduler](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/systems/scheduler.c#L231-L261). Unknown native effects remain serial. |
| Ordered reductions and automatic row parallelism | Implemented, proof-gated | PR395 actually runs eligible reducer chunks in parallel and merges in chunk order. Current fast path excludes observers and ordinary writes and needs at least two chunks. [Parallel runner](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program.c#L2025-L2123). Imported modules, invocation state/bindings/actions and unsupported helper effects also block the current proof; general arbitrary-loop parallelization is not implemented. |
| Borrowing, scalarization and storage reuse | Implemented, restricted | Inferred call-scoped plain scalar arrays only; runtime guard scans headers. Managed elements/escaping/suspending/opaque paths retain ownership. [Borrow guards](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/borrow.h#L14-L118), [optimization pipeline](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/opt.c#L39-L70). |
| AoS/field-wise SoA and packed buffers | Partial foundation; requested facility missing | ECS columns are separated by component, but each component has a strided record. General arrays hold tagged `pin_value`s. No demonstrated source-level field-SoA storage choice. [ECS storage](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ecs/ecs_internal.h#L10-L26), [column ABI](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ecs/execution.h#L7-L23). |
| Runtime backends, debugging and cost tooling | Implemented; whole-app performance unverified | Bytecode, SLJIT and browser Wasm; source inspection and structural cost diagnostics. C emission is separate, not proof of a shipping AOT Studio pipeline. [Backend selection](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/backend.c#L12-L37), [cost information](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/function.h#L73-L102). |
| Formatting and canonicalization | Implemented; configuration and contextual elision incomplete | Existing semantic minimization plus CST-preserving whitespace formatter; four-space indentation is hardcoded and there is no width reflow. UI formatter exists, CLI UI coverage incomplete. Preserve one authoritative API; select its layout engine by corpus evidence. See section 8 and D10. |


### 6.1 Blocking or near-blocking work

**A. Application records and stable identities.** Studio has many named data shapes: command state, field models, edit sessions, rows, history entries, jobs and document state. Reflected native structs and union payload records exist, but general authored record declarations are not yet a complete equivalent of C application structs. Add a small record facility with precise copy/equality/default/reflection/serialization rules, or prove an equally readable existing representation. Do not generate a native C struct for every new app model as the permanent authoring workflow.

**B. Collections that support actual application algorithms.** Arrays and sets exist, with value semantics and safe read-only borrowing optimizations. Current `map<T>` indexing is integer-based and does not establish a general associative dictionary. Native-looking `append` calls may copy a full value. Studio requires dynamic row buffers, lookup tables, stable-key indexes, sorting/searching, queues and bounded caches. Provide measured amortized builders/unique-owner reuse or another semantics-preserving strategy, plus a real keyed dictionary if workloads justify it. Define ordering, equality/hashing, missing keys, invalid indices and alias behavior. [Collection implementation][E-index] [Borrowing implementation][E-borrow]

**C. Module-scale composition.** Qualified library calls already exist. Before creating hundreds of files, verify symbol visibility, unique naming, cycle diagnostics, dependency invalidation, private state ownership and package boundaries. Add import aliases/export visibility only to solve demonstrated ambiguity or ergonomics; avoid a package-manager redesign. General unconstrained authored generics are not ready merely because type-parameter syntax parses: lowering explicitly requires concrete/monomorphized signatures. Decide whether a small fixed family of typed helpers is sufficient for the first slice; otherwise implement specialization with bounded code growth and source/debug mapping. [Generic call lowering][E-generics]

**D. Lifetime correctness under editor workloads.** Value semantics must remain predictable across function calls, ref parameters, task captures, FFI and reload. Audit the detached captured-local box storage: static inspection indicates repeated managed overwrites can retain old snapshots until its final task owner dies. Also measure world-lifetime string interning with unique edited text. These are retention risks to test, not measured leaks. Document-sized state should not be parked in a never-ending captured task as a workaround. Add bounded-lifetime/reclamation fixes where measurements confirm the risk.

**E. Typed editor service APIs and errors.** Expand the current Studio-only capability pack incrementally with narrow contracts. Use typed outcome/union values for expected user-facing failures. Runtime faults remain explicit and report source context, preserve valid documents/generations and cancel the right operation. No broad exception syntax is required until a concrete need is demonstrated.

**F. Reactive view publication and instance lifecycle.** Define one publication boundary for a coherent UI update, independent view instances, deterministic event ordering, mount/unmount cleanup and cancel behavior. The existing same-frame dispatch and bounded settle loop are a starting point, not permission to introduce unbounded reactive cycles. Detect/report a cycle or defer remaining work according to an explicit contract. [Authored dispatch and settling][S-ui-events]

### 6.2 Already implemented: strengthen rather than redesign

- Keep existing typed numerics, exact integer widths, unit/range metadata, source spans and diagnostic infrastructure; specify their edge cases.
- Keep named functions, qualified library calls, inferred results, ref parameters, tasks, `yield`, cancellation and typed `Cancelled` results.
- Keep ECS query headers, relationship sources/joins, change filters, access inference, safe scheduling and ordered reductions. Unknown or conflicting effects remain serial. Ordered reductions preserve the canonical fold; `unordered` is an explicit semantic relaxation and is inappropriate for strict parity unless separately approved.
- Keep bytecode as a diagnostic/reference backend and compare it against actual SLJIT/Wasm execution. Do not substitute a bytecode fallback for a required compiled-backend pass.
- Keep automatic allocation/copy/escape diagnostics and verified borrow/escape optimizations. They should explain slow code with source locations, not require authors to guess which annotations unlock speed.
- Keep staged source publication and last-valid-generation isolation. Reconcile documentation with the actual caller-specific state preservation/restart rules.

### 6.3 Useful language features belong before or alongside the rewrite

Treat the migration as a language proving ground. Implement the useful features it exposes before the first dependent conversion, or together with a bounded, reversible feature specimen. The priority is authored structs; efficient ordinary collections and stable identities; readable module composition and outcomes; nonescaping capturing lambdas followed by owner-safe stored callbacks when a real feature needs them; then packed AoS and SoA for the workloads that benefit. Section 10 records semantics, spelling alternatives and gates. Existing features should be strengthened rather than replaced without cause.

Allocator selection, arena lifetimes, stack/region placement, reuse and reclamation are implementation responsibilities. Ordinary app/game authors should work with values, typed identities and clear lifecycle ownership, without manual allocation/free or allocator parameters. Internal custom allocators are reasonable where measured; exposing them in normal source is not a migration requirement. APIs can describe retention/effects where needed for correctness, but ordinary call sites should benefit from inferred borrow, escape and uniqueness analysis.

LLVM, a general package registry and JSX-style templates are **not planned requirements or proposed implementation stages**. They appeared in the first review only as examples of scope that lacked a demonstrated need. Current SLJIT/Wasm and module/RML infrastructure are the starting point. Richer matching, traits, operators or macros may be evaluated when a concrete authored example proves a benefit, not as speculative prerequisites.

For sophisticated game code, prioritize robust ECS iteration, predictable values/ownership, exact numerics, typed events/tasks, efficient collections, deterministic reductions, resource handles, incremental compilation and debugging. Known engine operations should be optimized as part of the language/toolchain. Foreign/platform calls and proven kernels can remain narrow operations; their existence must not pull app/game algorithms back into C.

### 6.4 Ownership and representation rules to settle before broad translation

1. **Small values copy; identities do not.** Ordinary strings, collections, unions and structs recursively snapshot at owning boundaries. A document, native resource, entity or long-running job uses a typed identity/handle, not a giant recursively copied record. A task handle refers to the same owner-bound task when copied; its returned value is independently snapshotted for each awaiter. [Snapshots](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.c#L195-L259), [tasks](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program_tasks.c#L289-L345).
2. **A reference is logical access, not a persistent raw address.** Components may move or disappear. Reacquire checked references across structural changes, callbacks and suspension. An inferred borrowed view must not escape into a model, event, stored callback or task; opaque native calls require an explicit borrow/retain contract. Existing proven views cover plain scalar arrays and still scan headers, not all record/string buffers. [Borrow contract](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/borrow.h#L14-L118).
3. **Borrow within a proven lifetime; own retained values.** `pin_value_copy` intentionally leaves struct payload pointers borrowed; it is not an owning record snapshot. Current synchronous host inputs are already borrowed, so this review does not claim that every engine call deep-copies or that a particular Studio call is faulty. At stored/asynchronous boundaries, generated bridges/compiler/runtime must use ownership-preserving snapshots or a proven move/share/borrow strategy, invisibly to ordinary authors. Copying only the descriptor of a native record leaves it pointing at the producer; later mutation or destruction changes/dangles the supposed saved value. The existing owning snapshot copies the record bytes and registered managed fields, while a call-scoped borrow avoids that work. Cost is proportional to retained elements/payload bytes, not merely the argument count; an arena allocation is not necessarily a separate system allocation. The existing nested-record snapshot test demonstrates the intended contract in source, but was not run here. Add adversarial producer-destruction, document replacement, reload and cancellation cases. [API warning](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.h#L32-L56), [owning snapshot versus copy](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.c#L195-L333), [host borrowing](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/host.h#L15-L83), [existing test](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/tools/types.c#L498-L524).
4. **Do not equate an arena with bounded memory.** Managed activation arenas use safe-point nonmoving collection; synchronous peak allocation can still grow. Detached capture boxes use a different arena lifecycle. The task root collector, captured-box holder counts, module ownership and state-arena retirement are distinct mechanisms, not a single general tracing heap. Measure each separately. [Activation collection](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program_storage.c#L64-L110), [detached boxes](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program_tasks.c#L149-L180).
5. **Efficient mutation must preserve values.** Array/set updates currently rebuild snapshots; repeated appending can produce quadratic aggregate copy work. A uniqueness/COW optimization, builder or packed buffer must preserve aliases, event/task snapshots, error ordering, debug inspection and reload. Uniqueness is a compiler/runtime proof, not a promise the author can accidentally break. [Arrays](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/arrays.c#L200-L336), [sets](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/sets.c#L55-L108).
6. **Resource cleanup and admission are explicit.** A workspace/document/component scope owns registrations, jobs and resource handles; teardown deterministically cancels/releases them. Define bounded queues, task/payload admission and work limits. The current allocator aborts on allocation failure, so a recoverable `OutOfMemory` result must not be promised without new runtime work. [Allocator](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/support/xalloc.c#L17-L43).

### 6.5 Game/engine/application capability priorities

The language's long-term target is wider than this editor. Keep a capability backlog with a real consumer and measurable acceptance, rather than making every possible feature a prerequisite for Studio:

- **Before broad Studio conversion:** expressive records, safe values/refs, efficient collections, typed errors, module contracts, callbacks if selected, resource lifetimes, source debugging, correct formatting, incremental publication and the application service seams.
- **In parallel through representative game specimens:** ECS membership/change semantics, dense/sparse iteration, reliable spawn/despawn and relationship commands, fixed-step/event ordering, deterministic ordered reductions, task cancellation on entity removal, field-storage choices and native numeric/resource interoperability. Reuse current inferred effects and prove actual worker participation.
- **When a measured game/engine workload requires it:** packed binary/numeric buffers and slices, SIMD-friendly field streams, batched physics/render/audio/network services, portable serialization and schema evolution, generic algorithm specialization, optional validated cost assertions, and packaging/AOT for an identified platform. A foreign ABI is a concrete boundary; a kernel stays native only with a documented constraint or measured justification after considering an authored implementation and compiler support.
- **Not a prerequisite without a concrete example:** inheritance, a cyclic object heap, arbitrary pointer arithmetic, unrestricted FFI, exceptions, dynamic plugin loading or a new compiler backend. Each adds semantic and tooling obligations and must beat a simpler typed/ECS/service design.

**Modularity versus parallelism is a specific current gate.** The current executable proof sets `parallel_safe` false when `import_count` is nonzero, even if authored effect analysis can see a pure imported helper. The scheduler then serializes that system. Benchmark the same representative game update inline, through a local helper and through an imported library; report the exact fallback cause. Extend intermodule proof/verified summaries only if required by the chosen modular game workload, with generation/ABI invalidation and conservative native/callable fallback. Do not solve this by silently abandoning module boundaries or claiming transitive effect analysis already removes the restriction. [Module proof](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/module_effects.c#L1178-L1194), [scheduler fallback](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/systems/scheduler.c#L231-L261).

“Performance by default” means sensible representations, inexpensive ordinary operations, conservative inferred optimization and actionable diagnostics. It cannot mean every program automatically vectorizes or parallelizes. Shared-target updates, UI/OS effects, unknown calls, aliasing, suspension and observable ordering must remain correct and serial unless a proof covers them.

### 6.6 Use engine knowledge to eliminate avoidable bridge work

**Design decision:** Chibi's own engine is a known implementation target. Its operations should participate in compilation through checked semantic IR, type/layout information, effects, ownership and generation contracts. A generic FFI/tagged-value call is a fallback for an opaque operation, not the default architecture for every engine access. The objective is to remove avoidable descriptor construction, boxing, copies, repeated service lookup and dispatch, and to let optimization see the work. A direct C call can still carry those costs; a typed operation may legitimately retain a cheap helper call when that is the best implementation.

#### What current code actually does

| Path | Verified implementation | Consequence |
|---|---|---|
| Native ECS backend | SLJIT emits checked entity generation/occupancy and component layout/stride access, plus typed loads/stores. [jit_component.c:25-194](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/jit_component.c#L25-L194), [254-279](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/jit_component.c#L254-L279) | Direct engine-aware backend machinery already exists. Reuse it rather than start a new backend. |
| Authored component access | Presence conditions use `component_value` and emit BF_WORLD/BF_ENTITY/BF_COMPONENT. Ordinary get/set and component-value expressions still use logical-reference operations. [lower.c:2899-2976](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L2899-L2976), [1857-1879](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L1857-L1879), [3706-3707](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L3706-L3707) | Backend support does not prove ordinary `.chibi` fields already bypass the bridge. Connect safe authored accesses to it. |
| Logical references | Construction allocates a descriptor; field references add a descriptor/path. Reads snapshot the selected value. Scalar component writes already avoid copying the whole root, but still resolve through the reference and publication callback. [intrinsics.c:192-211](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/intrinsics.c#L192-L211), [292-310](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/intrinsics.c#L292-L310), [441-523](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/intrinsics.c#L441-L523) | Specialize nonescaping, uninterrupted scalar/fixed-layout accesses. Preserve logical identities wherever references escape or storage may move. |
| Native hostcall | SLJIT directly addresses some context/value helpers, while ordinary services use the bound invoke pointer. Both use argument/result `pin_value` banks. [jit_host_call.c:56-146](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/jit_host_call.c#L56-L146) | Replacing an indirect call with a direct call alone is insufficient. Typed ABI and better lowering must remove actual preparation/representation costs. |
| Browser Wasm | Generated code directly emits a scalar/control/load/store whitelist; other instructions call imported `execution_machine_step`. Modules share engine memory and import the existing step function. [wasm_emit.c:204-253](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/wasm_emit.c#L204-L253), [377-391](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/wasm_emit.c#L377-L391), [545-560](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/wasm_emit.c#L545-L560), [wasm_jit.c:49-71](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/wasm_jit.c#L49-L71) | BF_COMPONENT/BF_COLUMN/BF_ROW and hostcalls are not covered by that direct whitelist. Add matching checked lowerings or typed imports; do not assume each helper call serializes through JavaScript. |
| Query bindings and iteration | A bound query variable uses a value hostcall whose implementation checks an index and loads `context->query_variables`. Eligible systems stream reusable rows, but the outer loop still invokes `ecs_query_next` and `run_stream_row` in C. [lower.c:2838-2862](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L2838-L2862), [intrinsics.c:729-743](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/intrinsics.c#L729-L743), [program.c:2125-2157](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program.c#L2125-L2157) | The tiny binding load is an early intrinsic candidate. Hoisting stable metadata and tightening row bodies comes before a speculative whole-query rewrite. |
| Existing optimization and contracts | Verified union/snapshot intrinsics already support elimination/scalarization. Native definitions expose types/effects/access categories, but cost analysis treats nonintrinsic native calls as having no verified allocation/copy/escape contract. [scalarize.c:89-112](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/scalarize.c#L89-L112), [605-619](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/scalarize.c#L605-L619), [definition.h:45-64](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/capabilities/definition.h#L45-L64), [module_effects.c:1335-1342](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/module_effects.c#L1335-L1342) | Extend the verified-contract machinery, not a parallel handwritten whitelist with unverifiable promises. |

An intrinsic name alone does not guarantee inline machine code: several current intrinsics are still represented by value hostcalls. Likewise, historical query-fusion source/measurements are not proof of current authored-query coverage. At this baseline the inspected lowerer does not emit BF_ROW/BF_COLUMN query loops; the row runner above is the relevant authored execution path.

#### Prioritized implementation slices

1. **First, establish the lowering oracle and implement the narrow known loads.** Use a bound query variable and a nonescaping scalar component read as the smallest specimens. Preserve baseline source/effect/error traces, inspect emitted IR and actual backend execution, then remove the unnecessary reference/bank helper machinery. Negative cases must keep the correct fallback: stale/missing entity, absent component, incompatible layout, escape, unknown effect, observer, suspension and storage invalidation. No new visible syntax is needed.
2. **Then add checked component write/publication semantics.** A raw BF_WRITE is not a replacement for the current store path. Preserve type/layout/range handling, managed-value ownership, same-value publication behavior, version increments, dirty rows and deferred ON_SET, as well as compound-assignment evaluation/error order. The existing chain is reference write -> `store_component` -> `ecs_component_write_value` -> ownership/range validation and modification tracking. [program.c:1046-1068](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program.c#L1046-L1068), [ecs.c:1910-2008](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ecs/ecs.c#L1910-L2008), [384-427](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ecs/ecs.c#L384-L427). Start with plain scalar writes; extend managed/fixed-layout records only with their ownership and publication proof.
3. **Add verified engine operation/call descriptors.** Extend the existing single-source capability definitions with checked borrowing/retention, allocation/copy, alias, thread/reentrancy and lowering contracts. Generate backend bindings and validation from them. Prefer direct IR for simple semantics; use scalar/unboxed typed calls for worthwhile helpers; prebind stable services. Retain a generic checked path for unsupported operations and genuinely opaque foreign calls. Metadata must be verified against implementation/tests and included in compatibility identities, not trusted solely because a function is labeled pure. Current generic native instruction bindings accept only BF_DELAY/BF_OWNER/BF_DELTA, so expanding this is real implementation work. [binding restriction](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/native_capabilities.c#L331-L357).
4. **Optimize complete hot paths next.** Hoist stable ECS/query metadata once per safe region; specialize row bodies; extend intermodule effects so ordinary modular code can retain proven scheduling/borrow optimizations; remove redundant record/collection snapshots using proven uniqueness/lifetime; batch UI/model/render work. Compare end-to-end Studio and game specimens. Do not require authors to flatten modules, choose allocators or annotate ordinary loops to escape an avoidable runtime tax.

SLJIT emits the instructions/calls that Chibi supplies; knowing a C function's signature does not expose its implementation for automatic inlining. To eliminate a helper completely, represent its verified semantics in shared IR or provide a specific lowering. When retaining a typed call, define its native calling convention and Wasm import/signature explicitly. The current generated Wasm module imports shared memory and step, so typed engine imports require linker/emitter work. There is no LLVM prerequisite. Shared semantic IR with native, Wasm and bytecode implementations keeps results consistent without assuming identical machine layouts or optimization mechanisms.

#### Publication, versioning and lifetime safeguards

- Key optimized artifacts by the actual engine/runtime ABI, native capability semantics/ownership/effects, target layout and authored dependency generation. Existing prepared modules validate context offsets, pointer width, type size/layout and capability ABI; extend those contracts when optimization relies on more facts. Effects already participate in capability hashing. [prepared.c:272-300](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/prepared.c#L272-L300), [388-435](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/prepared.c#L388-L435), [prepared_function.c:120-166](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/prepared_function.c#L120-L166), [catalog.c:442-489](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/capabilities/catalog.c#L442-L489).
- Cache raw addresses only within a proven stable execution region. Hoist validation where safe instead of adding a redundant guard to every field load; invalidate/reacquire at structural changes, unknown effects, suspension, callbacks and publication. Keep entity generation and schema/layout checks; direct code must never preserve an old column pointer merely because the field name is unchanged.
- Publish a complete compatible authored generation at the existing safe boundary. Current source commit requires no execution in flight; program replacement checks the expected old module. Backend code belongs to immutable definitions. Preserve last-valid-generation behavior, debug/source mappings, error locations, budgets and cancellation. [source_publication.c:261-277](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/behaviors/source_publication.c#L261-L277), [program.c:2779-2806](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program.c#L2779-L2806), [artifact.h:8-18](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/artifact.h#L8-L18).
- Browser function-table entries are realm/thread-local; retain/retire the correct generated module and never share a realm's table index as a global code identity. Current native C changes require rebuild/restart, not a new live-C replacement mechanism. Authored hot reload remains required. [Wasm ownership](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/wasm_jit.c#L49-L51), [retirement](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/wasm_jit.c#L316-L330), [native lifecycle](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/reload.md#L3-L21).

**Acceptance:** semantic differential tests against the current checked path; positive and rejection/fallback cases; actual SLJIT/Wasm execution evidence; generated-operation and helper-call counts; allocation/copied-byte attribution; cold compilation/reload and steady-state workload measurements; no ownership/publication/debug regression. These are prospective gates. This source assessment establishes opportunities, not measured speedups or passing tests.

## 7. Performance gates before translation

### 7.1 Baseline protocol

Freeze engine + Studio SHAs, SDK package/checksum, assets, compiler flags, backend, build type, hardware, OS, driver, display size/DPI, fonts, power mode and background load. Measure cold preparation/startup separately from warmed interactive work; report JIT/Wasm compilation separately from steady-state execution. Capture native and browser serial baselines, plus threaded browser only where it is a supported target.

Use a three-way baseline: (1) frozen native Studio with its original SDK, (2) the same native Studio with the candidate SDK, and (3) authored Studio with that candidate SDK. Record original outputs before changing engine capabilities. Any difference between (1) and (2) is engine/SDK-induced drift that must be explained and approved independently; only (2) versus (3) isolates the application-language port. If native Studio needs an adapter to consume the candidate SDK, audit that adapter as part of the baseline. Use the same deterministic fixture and action trace for every comparison. Preserve observable output checksums/state assertions so a faster incorrect result cannot pass. Record median/tail/max latency where the sample count supports it, allocations and copied bytes, retained/peak memory, UI nodes/layout/restyle counts, query scans, host crossings, compilation/reload time and frame pacing. Show raw distributions and provenance; do not extrapolate a tiny selector into whole-application speedup.

Set budgets after baseline: (a) no new work while idle; (b) no unexplained asymptotic regression as data size grows; (c) no retained-memory growth after a workload returns to the same live state; (d) latency and tail-frame thresholds agreed from measured native behavior and product requirements. A single arbitrary “within 10%” threshold would hide both meaningful stalls and harmless noise.

### Instrumentation accounting

Reuse existing APIs before adding probes. `execution_program_observe` samples inclusive process CPU time, so nested samples cannot be summed as separate frame costs. `execution_program_memory_usage` includes logical payload/capacity, task/query/reduction/cache and worker counts but excludes some nested/shared storage; it is not a complete heap census. `xalloc_stats` counts allocator traffic, not live/peak/RSS, and direct library allocators and worker-local counters need separate accounting. Source allocation/copy/escape warnings are structural possibilities, not timings. Preserve RmlUi model/layout/release phase profiling. [Observation](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/module.h#L151-L168), [memory](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/module.h#L31-L74), [allocator counters](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/support/xalloc.c#L8-L15), [UI profile](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ui/rml.h#L425-L439).

Add only missing attribution: input-to-visible-frame timestamps, per-phase compiler/reload timing, native crossings and batch size, exact copied bytes, retained captures/strings/generations, process peak memory and dirty/layout counts. Profiling/inspection can disable fast paths, so use paired uninstrumented runs for headline claims and instrumented runs to explain them. Assert the backend really emitted/executed native or Wasm code rather than silently accepting an inspection fallback.

### 7.2 Required workload matrix

| Workload | What it proves | Primary counters |
|---|---|---|
| Empty/starter and representative real-project startup | Load/compile/link/native-service overhead | Cold time, peak memory, compiled size, imports, module count |
| Idle shell and visible/hidden panels | No polling/rebuild regression | Wakeups, allocations, panel syncs, dirty variables, layouts, CPU time |
| Large Outliner filter/select/reorder/scroll | Collection/index complexity and virtualization | Rows visited/materialized, copies, host crossings, frame/input latency |
| Assets grid/list, search and thumbnail arrival | Stable keys, async invalidation, cache bounds | Rebuilds, nodes, jobs, retained thumbnails, scroll tails |
| Multi-select Properties and continuous scrub | Typed reflection and transaction costs | Field reads, copies, preview/commit count, undo bytes, latency |
| Code typing, completion and diagnostics | Incremental semantic work | Reparsed/reanalyzed modules, analysis/compile time, text copies |
| Library edit affecting many callers | Dependency invalidation and reload | Reused/rebuilt definitions, peak old+new generations, pause duration |
| Large graph pan/zoom/selection/live debug | Geometry and inspection boundaries | Visible nodes/wires, draw batches, layout/restyles, debug overhead |
| Designer/Inspector live edits | DOM inspection invalidation | Tree walks, fields read, source edits, stale-handle failures |
| Play/pause/step/stop and keep-changes | Runtime generations/ownership | Activations, task retention, snapshots, world counts, deterministic state |
| Save/reload/import/export/failure/retry | Durable state machines | Outstanding writes, bytes, job lifetimes, exact exported bytes |
| Long session of edits, opens/closes and reloads | Reclamation and fragmentation | Live/retained/peak memory by category, handles, interned strings, caches |
| Many moving entities with sparse/dense queries | Ordinary game ECS throughput | Rows, matching cache work, columns touched, allocations, scaling |
| Projectiles sharing targets and ordered damage | Conflict/reduction semantics | Contributions, log bytes, publication order, worker overhead |
| AI/task swarm with cancellation and despawn | Structured concurrency at scale | Live tasks, resume cost, payload/capture bytes, cancellation cleanup |
| Spawn/despawn and hierarchy churn | Structural boundaries and stale refs | Query invalidations, reacquisition cost, handle generation checks |
| Strings/arrays/records/dictionaries in hot helpers | Value semantics and specialization | Allocations/copies/retained bytes, algorithmic slopes, generated code size |

Small/typical/stress sizes must come from observed project distributions and a clearly labeled scaling series. Current source includes performance APIs and benchmark/test machinery, but the old aggregate benchmark is disabled under the temporary test policy. Historical ratios from a heavily loaded machine are not acceptance budgets.

### 7.3 Test-policy constraint

Current repository policy enforces a hard 60-second production-supervisor ceiling on test invocations, with known slow/unmeasured work quarantined. Full native/render/Studio suites and the historic long capture remain disabled; chunking them to reconstruct the suite is also prohibited. Build/configure time is separate. [Current test policy][E-policy]

First add or adapt independently meaningful bounded C/pico_unit selectors under that policy, with coordinator admission and exact fixture provenance. No product execution was performed while preparing this document; implementation validation must use the admitted route. If a needed long-run soak, representative benchmark or platform capture cannot be honestly established within admitted coverage, make it an explicit authorization/admission blocker for readiness. Do not lower the acceptance standard, bypass the supervisor or call quarantined coverage passed.

## 8. RML, RCSS and .chibi formatting

A conservative formatter already exists at `src/assets/ui_source_format.c`, backed by Tree-sitter HTML/CSS. RML preserves bytes between tags and wraps long attribute lists; RCSS preserves source token/value slices rather than normalizing CSS semantics. Studio's buffer API already applies formatting as a single undo step. The gap is broad correctness coverage and consistent project-wide CLI/check integration: `chibi_ctl --format` currently skips UI assets. [Shared UI formatter][E-ui-format] [CLI formatting path][E-format-cli] [UI buffer formatting contract][S-buffer]

**Recommendation:** keep one authoritative configurable formatting API used by Studio, CLI, pre-commit/check mode and authoring services. Use the current parser/semantic pipeline and conservative UI formatter as the baseline. Select its layout engine by corpus correctness, configuration support and integration evidence, rather than committing in advance to maintaining a bespoke printer or replacing it. The current C dependencies are Tree-sitter 0.25.10, HTML grammar 0.23.2 and CSS grammar 0.25.0; retain exact version pins and test any upgrade. [Dependency pins](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/cmake/tree_sitter.cmake#L1-L30). Evaluate established external tools as comparators on a fixture corpus, not as an automatic replacement:

- **Prettier HTML and its XML plugin:** mature layout infrastructure; evaluate HTML `htmlWhitespaceSensitivity: strict` plus `embeddedLanguageFormatting: off`, or XML whitespace/quote preservation with attribute sorting disabled. Those settings still do not establish RML support. Generic rewriting can change significant whitespace, custom elements, self-closing forms and template/event attributes. [Prettier options](https://prettier.io/docs/options#html-whitespace-sensitivity), [official XML plugin](https://github.com/prettier/plugin-xml#configuration).
- **Prettier CSS/PostCSS:** established parsing/printing infrastructure, but RCSS is not CSS. RmlUi uses 0–255 `rgba` alpha, and Chibi adds expansion syntax; preserve the original numeric/color lexemes, custom properties, functions and at-rules. PostCSS raw fields can preserve author formatting, but a compatible printer still needs dialect-aware tests. [PostCSS raw fields](https://postcss.org/api/#declaration-raws), [RmlUi RCSS syntax](https://mikke89.github.io/RmlUiDoc/pages/rcss/syntax.html).
- The existing Tree-sitter adapter: already aligned to the current toolchain and conservative byte-preservation needs. Extending it is a candidate, not a permanent C implementation requirement; measure maintenance and layout capability against established engines.

The current adapter masks complete `{{…}}` bindings before HTML parsing, and RCSS import/nesting expansion is a separate transformation. A formatter must preserve authored imports/nesting rather than emit flattened expansion output. [RML adapter](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/assets/ui_source.c#L446-L514), [RCSS expansion contract](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/support/rcss.h#L10-L28).

Required formatter corpus: every live Studio RML/RCSS asset; inline text and mixed content; Unicode/entity references; comments; `use`/slots; `data-for`/`data-when`/`data-template`; binding and event expressions; attributes containing quotes; selectors; custom properties; gradients/decorators; media queries; numeric units; colors; malformed/incomplete drafts; trailing comments and empty documents.

Acceptance: parse before/after; formatting is idempotent; significant text, values, selector/declaration order, source-derived bindings and RmlUi computed styles are unchanged; the app's pixel/input parity corpus passes; invalid drafts are non-destructive; check mode writes nothing; write mode is atomic; editor formatting is one undo step with valid caret/source mappings. Do not sort declarations, shorten numeric/color literals, remove apparently redundant properties or substitute HTML semantics.

For `.chibi`, retain semantic reanalysis as the guard for canonical simplification. Add readable examples for every accepted new construct and verify formatter round-trip, analyzer results, lowered semantics, source maps and graph projection together. Formatting should make ordinary authored code clean without turning it back into graph-generated scaffolding.

### 8.1 Configurable .chibi formatting and shared integration

Tree-sitter supplies an incremental concrete syntax tree, not an automatic configurable pretty-printer. Chibi's existing whitespace formatter preserves leaf text and verifies the reparsed CST, including comments and field relationships. Its indentation is currently hardcoded to four spaces and it preserves existing line-break choices; there is no configurable width-reflow engine. The semantic minimizer is separate and reparses/reanalyzes candidates using the catalog/document identity before comparing the program. The controller already composes minimization -> reparse -> layout, and skips minimization when identity cannot be resolved. [Tree-sitter](https://tree-sitter.github.io/tree-sitter/), [source_format.c:180-314](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_format.c#L180-L314), [source_minimize.c:1164-1242](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_minimize.c#L1164-L1242), [controller.c:5868-5896](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/automation/controller.c#L5868-L5896).

Evaluate three bounded layout implementations against the same corpus:

1. **Extend the current printer.** Add a stable options contract (indent width/tabs policy, print width and line endings, with dialect-safe defaults) and real grouping/line-break decisions if width reflow is required. Preserve the parser/catalog/source-map integration. This minimizes integration change but owns the layout implementation.
2. **A real Prettier .chibi plugin.** Prettier supports a custom language, parser returning an AST-compatible representation, and a printer returning its document layout representation. A proper printer supplies grouping/indentation/break opportunities; Prettier handles width-sensitive layout. A thin plugin can instead delegate to the authoritative Chibi engine through a native tool/binding or Wasm, but then the engine must implement and receive configuration. Returning already formatted text does not magically enable `printWidth`/`tabWidth`. Do not build two divergent authoritative printers. [Plugin API](https://prettier.io/docs/plugins), [printing model](https://prettier.io/docs/technical-details), [Doc representation](https://github.com/prettier/prettier/blob/main/commands.md#prettiers-intermediate-representation-doc).
3. **Topiary over the existing Tree-sitter grammar.** This established formatter uses query-driven formatting rules over a Tree-sitter CST. It can reuse parsing work rather than require a new handwritten parser, but grammar compatibility, layout policies, configuration surface, native/browser packaging, comments and dialect preservation still need an evidence-based comparison. Its existence does not prove drop-in Chibi/RML/RCSS support. [Topiary documentation](https://topiary.tweag.io/book/), [upstream](https://github.com/topiary/topiary).

Choose one authoritative layout implementation after the comparison; retain a single versioned API and configuration resolution path. Check precedence (project configuration versus explicit editor/CLI options), unknown-option diagnostics, deterministic defaults and matching output in Studio/CLI/check mode. The implementation language of the formatter is not exempt from the `.chibi`-first direction, but using an established dependency or native parser is not a reason to delay basic integration. Do not install a candidate solely to settle a preference before the comparison scope is approved.

**Separate operations:** layout formatting changes whitespace/layout only; canonicalization performs a small documented set of semantic-aware keyword/qualifier simplifications. A user-facing Format command may compose them under a chosen project policy, but canonicalization requires enough project context and the approved syntax/resolver. On incomplete source or missing catalog, perform safe layout only or report why semantic simplification is unavailable; never guess. Removing `state`, `self.` or named-function keywords is not a regex printer rule. Preserve binding IDs, types, effects, capture modes, storage lifetime, comments and stable source identity, then reparse and format. Prettier's API cautions that AST-mutating codemods can invalidate positions/comment handling, which reinforces keeping this pass separate. [Prettier API](https://prettier.io/docs/api#custom-parser-api-removed).

Add configuration cases and real new-language constructs to the corpus, alongside all existing `.chibi`, RML and RCSS fixtures. The selected engine must satisfy section 8's semantic/idempotence/undo/source-map gates, not merely produce attractive examples. Missing required configuration or unsupported dialect constructs are selection failures, not reasons to silently normalize syntax.

## 9. Semantic specification, documentation and developer experience

Treat the language contract as a product deliverable, not a final cleanup task. Write a versioned normative core separate from a friendly guide and historical design notes. Every rule needs positive, negative and cross-backend conformance examples.

| Specification area | Required decisions and tests |
|---|---|
| Lexical/syntax | Identifiers/quoted names, Unicode, comments, line continuation, precedence, semicolons, source byte/line positions |
| Names/modules | Scope/shadowing, visibility, qualification, identity, cycles, ambiguity, exports and incremental dependencies |
| Types/numerics | Inference, widths, promotion/casts, overflow, bounds, ranges/units, NaN/infinity/signed zero, exact 64-bit serialization |
| Values/ownership | Assignment/call/ref/alias semantics, arrays/sets/maps/records/unions, nested managed values, equality/hash/order, invalid references |
| Execution | Evaluation order, short-circuiting, loop bounds, recursion/depth/resource limits, host error propagation |
| Tasks/events | Sync start, suspension/resume order, payload ownership, capture lifetime, cancel tree, deadlines/clock, completed results, reentrancy |
| ECS | Query membership/order, optional/read/write terms, relationships, change filters, structural mutation, reference reacquisition, reductions |
| Effects/concurrency | Inference through imports/native calls, unknown effects, safe cohorts, ordered publication, debug fallback, determinism |
| Native boundary | Capability identity/ABI, owned/borrowed results, thread affinity, handle generations, serialization and sandboxing |
| Publication/reload | Working/applied split, all-or-nothing staging, compatible state, schema changes, tasks/breakpoints, last valid generation, rollback |
| Tooling | Formatter, diagnostics/source spans, navigation/refactor, graph projection, breakpoints/watches, profiling/performance diagnostics |

Reconcile known stale claims before teaching them: `docs/code-language.md` describes reducer rows as sequential while current code includes proof-gated row parallelism; its broad system-reload restart statement does not describe every current publication path. Verify the exact APIs/callers, then update normative text and cross-reference tests. The architecture introduction also still names project format 10 while the current manifest contract is version 24; distinguish historical introduction text from the active version policy. Historical migration records remain clearly historical. [Current language guide][E-language], [architecture introduction](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/architecture.md#L9-L13), [current project version](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/assets/project.h#L11-L17).

Documentation set:

1. “Learn Chibi” with small realistic examples and a precise current capability index.
2. Core language/reference specification and a compatibility/change log.
3. “Build a Studio feature” recipe: state, controller, selector, RML/RCSS component, command, transaction, test and reload.
4. “Write fast game code” guide: ECS/data layout, collection costs, tasks, effects, ordered reductions, measurement and optimization reports.
5. Generated native API reference with schemas, ownership/effects, errors and platform availability.
6. Authoring/tooling guide: formatting, diagnostics, source/graph equivalence, navigation, testing and debugging.
7. Cookbook examples for a large virtual list, typed inspector, cancellable background job, deterministic combat reduction and a complete small game feature.

Compile/check examples under the bounded C test dispatchers. Do not create a second JavaScript/Python test framework. Track generated API documentation drift in checks. Show real current syntax prominently; proposed syntax stays in decision records until approved and implemented.

## 10. Syntax decision catalog

The examples below are **design alternatives, not code claimed to compile today** unless explicitly marked “current.” Bare member names with `self.` only when disambiguation is necessary are the user-directed style. The exact receiver/storage rules still need the scoped decision below. Choose constructs individually rather than approving a bundle implicitly. The Jai comparison is treated as conceptual inspiration for selectable layout; this plan makes no claim about Jai’s exact syntax, implementation or current features.

### D1. Authored value structs - foundational

#### Proposed syntax alternatives

**A. Recommended: named definition consistent with existing unions/functions.**

```chibi
// PROPOSED
library EditorModel {
    Row :: struct {
        key: uint64
        label: string
        selected: bool = false
    }

    MakeRow :: (key: uint64, label: string) -> Row {
        return { key: key, label: label }
    }
}
```

This extends the existing `Name :: union { ... }` family, uses existing `name: Type` fields and context-typed literal syntax, and initially keeps type declarations inside libraries. It avoids introducing a fifth source-root kind merely to add records. Export/import names should resolve through library-qualified nominal identities, with useful short names in the declaring/importing scope.

**B. Alternative:** `struct Row { ... }`. It is visually familiar and easy to search, but adds another declaration ordering convention. It is a legitimate style decision; do not accept both forever if the goal is a smaller canonical language.

**C. Alternative:** treat every authored record as `component Row`. This reuses more plumbing initially but gives ordinary values unnecessary ECS registration and identity semantics. Do not choose this as the permanent model for list rows, command payloads, edit results or geometry values.

#### Proposed initial contract

- **Nominal types:** two separately declared records with identical fields are distinct. No inheritance, implicit structural conversions, user-defined copy operators or method-dispatch system in the initial feature. Ordinary functions may take/return records; existing statically resolved facilities can be extended later if useful.
- **Value semantics:** assignment, return, by-value argument and value capture produce an independent logical value. Mutating one must not observably mutate the other. An implementation may use inline storage, copy elision or copy-on-write only if it preserves that guarantee. A handle field copies its identity, not its referenced entity/resource.
- **Explicit mutation:** use the existing `ref` parameter model for write-through access where a live addressable value is required. A field reference cannot silently outlive its owner or become a raw native pointer. Mutation effects must remain visible to purity/parallel-access analysis.
- **First field set:** fixed-width scalars, booleans, enum/nominal handle types, strings/names, and acyclic nested value records are useful targets. Split plain fixed-layout and managed-string cases in implementation and validation. Arrays/maps/union payloads inside records require descriptor, recursive ownership, equality, tracing, serialization and reload support; add them only with those paths complete. Reject unsupported fields clearly rather than routing them through borrowed pointers.
- **Defaults:** recommended for new authored structs: declaration-time pure field defaults; require all fields without defaults at construction. Preserve written initializer evaluation order. This catches accidentally omitted application data. Existing reflected-type literal behavior must remain its own documented contract until a separate approved migration changes it. Alternative: implicit zero/default for every field, which is concise and familiar to current record paths but can hide omissions. This is an owner decision.
- **Equality:** compare declared field values, never record padding or allocation addresses. Define behavior for floats, handles, managed fields and any future containers explicitly; retain current numeric/equality conventions unless intentionally changed. Hashing, if provided, must be consistent with the chosen equality and ordering rules. Existing struct equality is already field-wise. [Equality][s-equality]
- **Cycles:** reject inline recursive ownership cycles. Model identity graphs with typed handles/IDs; do not introduce a tracing heap merely to admit recursive records. An explicit managed graph/collection can be considered separately when a real model needs it.
- **Layout:** declaration order and target-correct natural alignment are a reasonable initial implementation rule. Do not promise identical native/WASM memory bytes, stable offsets across schema revisions, or C ABI compatibility from ordinary `struct` syntax. Logical field identity and physical layout fingerprints are separate concepts.
- **Reflection:** every authored struct must be visible to the existing catalog, inspector, completion, hover, rename/find-references, graph projection, debugger and field/range/unit tooling. Generate descriptors once through the shared compiler/SDK path. No native C registration should be required for each app record. Keep descriptors generation-scoped so an old value is never decoded using a newer incompatible layout.
- **Serialization:** persist fields/schema identity, not raw memory bytes, padding, pointers, callable environments or ephemeral handles. Explicitly classify fields that cannot be saved. Add/remove/rename/default/type changes need documented migration behavior; a field rename is not automatically the same as removal plus insertion. Undo/checkpoint snapshots must retain the values they actually promise to retain.
- **Reload:** preserve state only with a compatible nominal/schema contract or an explicit migration. Reject an incompatible staged publication without damaging the last valid program. Current replacement matches state IDs/shared status/types and frees the old execution generation on commit; authored struct schema compatibility must be part of the relevant matching checks. [Reload][s-reload]

#### Native boundary / FFI

Keep the typed ownership/effect contract, while selecting the cheapest verified implementation: direct engine IR, specialized typed native call, or the generic host fallback. Current synchronous host calls borrow plain records/arrays for the call and forbid retaining them or invalidating runtime storage. That is a suitable fallback contract for authored record inputs; retained managed results need owned storage. Compiler-generated bridging must avoid redundant marshaling when exact layouts and lifetimes are already known. [Host ABI][s-host]

Do not make all authored structs implicitly C-layout. A future opt-in interoperable layout, whether spelled `extern struct`, `repr(C)` or otherwise, needs owner approval and generated/native compile-time checks for size, offsets, alignment, scalar widths, enum representation and target ABI. C strings, owned strings and native handles must not become interchangeable because their fields fit in pointer-sized storage. Native/WASM adapters should marshal when layouts differ. Preserve a high-level reflected path for debugging even when a hot path can borrow unboxed bytes.

**Owner decisions:** choose A or B syntax; required fields versus implicit defaults; which managed field kinds are in the first deliverable; and whether a concrete consumer requires opt-in C layout now. Recommended: A, explicit defaults/required remaining fields, the small field set above, and reflected adapters before a general C-layout promise.

### D2. Keyed collections and builders — high priority if baseline proves the need

```text
A: rows: dict<ObjectId, Row>       // new, unambiguous associative type
B: rows: map<ObjectId, Row>        // expands/redefines current map spelling

A: rows = array_add(rows, row)    // current value-style operation; optimize proven unique ownership
B: builder.push(row); rows = builder.finish() // proposed explicit bounded construction object
```

Prefer unambiguous dictionary naming over silently changing existing `map<T>`. Prefer automatic reuse under preserved value semantics for ordinary code; add a builder only where ownership and capacity need an explicit phase. Specify deterministic iteration policy, key validity, duplicate handling and allocation behavior. A builder may not escape, suspend or leave dangling views accidentally.

### D3. Modules and visibility — prove existing capability first

Current library qualification includes `"MovementActions"."Apply Force"(...)`.

```text
A: import "studio/documents" as documents; documents.Open(...)
B: "Documents".Open(...)          // retain qualified catalog libraries, add only export/private metadata
```

A gives conventional namespace ergonomics but requires discovery, alias and dependency rules. B is smaller and closer to current code. Prefer B for the first slice if names remain clear; add A only after a multi-module specimen demonstrates a real problem. Neither should introduce hidden runtime service lookup.

### D4. Error values — use existing unions where possible

```text
A: SaveOutcome :: union { Saved | Failed(message: string) | Cancelled } // existing union grammar, new app type
B: generic Result<SaveReceipt, SaveError>                  // depends on complete generic specialization
```

Recommend concrete outcomes first, with exhaustive handling where supported or an explicit default diagnostic. Expected cancellation remains distinct from failure. Do not add exceptions, `try`/`catch` or a propagation operator until examples show that explicit outcomes are too cumbersome.

### D5. Component authoring — keep current rendering assets

A: `.rml` + `.rcss` + named `.chibi` controller, typed inputs/events and explicit lifecycle.

JSX-like inline templates are not part of this roadmap. The earlier comparison did not recommend introducing them.

Use the existing RML/RCSS/controller composition. It preserves existing assets/tooling, separates layout and behavior and avoids a second UI grammar. Co-location and module ownership provide professional structure without embedding markup into the language.

### D6. Lambdas with captures - bounded capability proposal

#### Proposed syntax and capture alternatives

**Recommended initial spelling:** retain `fn` as a required introducer for an anonymous callable; require a capture list even when empty.

```chibi
// PROPOSED: enclosing function/handler body
threshold := 10
accept := fn [threshold](value: int) -> bool {
    return value >= threshold
}

sum := 0
add := fn [ref sum](value: int) {
    sum += value
}

double_value := fn [](value: int) -> int {
    return value * 2
}
```

`[threshold]` snapshots the value when the lambda is created. The captured copy is immutable in the initial model. `[ref sum]` aliases the existing writable location under a checked lifetime. `[]` is explicitly noncapturing. Free local variables absent from the list are errors; static definitions need not be captured. Implicit reads of behavior state or host services must still contribute to effects and owner binding; they must not accidentally make a closure owner-independent.

Alternatives:

| Choice | Benefit | Cost / risk |
|---|---|---|
| Explicit value/ref list | Reviews expose copying and aliasing; mutation/cost is unsurprising | More source text; refactoring free variables may require list edits |
| Inferred by-value captures, e.g. a future `[=]` form | Concise predicates and UI callbacks | Large arrays/records may be captured accidentally; compiler/debugger must show inferred captures and cost |
| Inferred capture mode from use | Minimal notation | An innocent body edit can change aliasing, allocation or lifetime. Harder to audit |
| Capture everything by reference | Cheap-looking source | Does not resolve escaping storage, owner teardown or reload; inappropriate safe default |

Prefer explicit lists first. If inference is later adopted, infer **immutable by-value captures only**; require explicit `ref` for shared mutation. Do not let a formatter remove capture modes or replace an explicit list with inferred capture merely because the current body happens to behave the same.

#### Callable types and allocation must be observable

The following is an illustrative type design, **not existing syntax**; names can change, but the three semantic categories should not be blurred:

```chibi
// PROPOSED type spellings
plain_fn: fn(int) -> bool
temporary_predicate: borrow fn(int) -> bool
stored_callback: owned fn(int) -> bool
```

- **`fn(...)`:** a noncapturing callable. It needs code identity but no capture environment. A statically known call can be direct/inlined; a dynamic function value can still have an indirect-call cost. Noncapturing does not mean always inlined.
- **`borrow fn(...)`:** a scoped, non-escaping view of callable code plus an environment. It can support stack/inlined captures and `ref` aliases; the compiler must prove it is not stored, returned, queued, or retained by a host API. Synchronous consumers can be concrete overloads first rather than depending on unsupported general executable generics.
- **`owned fn(...)`:** a stored/escaping callable with owned by-value captures and an explicit owner lifetime. Environment creation/copying can allocate; this needs cost reporting and profiling. If assigned as a value, its logical captured values follow the same snapshot semantics as structs; do not quietly introduce shared mutable closure-object identity.

An alternative single callable type with inferred escape behavior is simpler at call sites, but public API signatures would conceal retention and allocation guarantees. The recommendation is distinct borrowed/owned contracts, whether expressed as these qualifiers or as equally explicit parameter effects. These describe an API/type lifetime guarantee; ordinary call-site borrowing, allocation elision and safe parallelism remain inferred, consistent with D7. A callback parameter annotation must be checked against the callee implementation and imported ABI, not just trusted documentation.

#### Ownership, escaping, tasks and references

1. **Start with authored-to-authored synchronous non-escaping lambdas.** Predicate/comparator/visitor APIs are useful pilot cases. Prove zero environment heap allocation on the chosen hot path; do not promise all lambdas are allocation-free or assume a native host may synchronously call back into execution.
2. **Owned value captures may escape within their explicit owner region.** A document callback may retain a copied query, row ID or typed resource handle. Its owner/cancellation subscription must determine teardown. Copying a handle does not guarantee that the resource still exists when the callback runs; resolution must validate it.
3. **Reject escaping local `ref` captures in the initial feature.** The existing runtime can box locals passed to suspending tasks, but general closure cycles, shared cells, mutation, retained native callbacks and closure copies add more contracts. If a later requirement needs this, choose explicit owner-bound `cell<T>`/boxing or a well-defined automatic promotion, with retention and cycle policy, instead of silently extending a stack address's lifetime.
4. **Capture component/state references as validated logical identities, not retained addresses.** Re-resolve after structural mutation/resumption. A reference to a field in a resized/reordered buffer is similarly unsafe without a stable storage identity and checked lifetime.
5. **Do not allow borrowed environments across suspension.** An `await`/yielding lambda must be represented as an explicitly task-producing callable with owned/safely promoted captures. Invocation must preserve the current task owner, result snapshot, cancellation and teardown rules. Whether to add an explicit `task fn` spelling is a later owner decision; a synchronous callback must never begin suspending invisibly.
6. **Propagate effects.** Purity, host access, state reads/writes, reference aliases, structural effects and suspension belong in callable compatibility and optimizer/scheduler analysis. A captured mutation must not evade the restrictions on parallel/unordered work.
7. **Host callbacks need a new checked invocation contract, not just a pointer adapter.** The current host ABI expressly forbids graph execution during a host call. Native callbacks should initially enqueue work for an approved execution boundary. Any direct synchronous native-to-authored callback needs a separately designed/approved reentrancy entry, activation ownership, cancellation behavior, thread/context rules and structural-storage protection. A native API must also specify retention and unregistration. Do not hand out an arbitrary language code pointer/environment as an untyped C function pointer or weaken the existing host restriction implicitly. [Host ABI][s-host]

#### Debugging and reload are part of the feature

- Give every lambda a source-stable identity, signature and environment-schema identity. Source byte offsets alone cannot be durable identities across formatting. Expose capture name, type, capture mode, current value/owner and allocation/retention size to the debugger and profiler.
- Step into/out of lambdas and display captured locals consistently in bytecode and compiled backends. Preserve breakpoint/source mapping, stack traces, runtime failure locations and graph/source navigation.
- **Recommended initial reload policy:** callbacks are owner-generation-bound; replacement unregisters/cancels old callbacks and creates current-generation registrations. A stale invocation fails predictably rather than jumping into freed code or silently using new code with an old environment. Existing execution replacement frees old work, so persistent cross-generation closures cannot be assumed. [Reload][s-reload]
- Alternative: pin the old immutable module generation until all closures release it. This can preserve callbacks through reload, but has real memory retention and state/service compatibility costs. Another alternative is rebinding compatible signatures/environments, which requires explicit migration. Choose one policy, measure it, and test owner close/reload during queued invocation.

**Owner decisions:** explicit versus inferred captures; distinct borrowed/owned callable contracts; initial acceptance of only synchronous non-escaping lambdas versus a named stored-callback use case; and stale-callback policy on reload. Recommended defaults are explicit capture, distinct lifetime contracts, non-escaping first, owner-generation invalidation/re-registration. This is a scope decision, not a claim that closures are unhelpful.

### D7. Borrowing, effects and parallelism — keep inference

A: infer read-only call-scoped views, access/effects and safe parallel work; report why code is serial/copying.

B: require authors to write explicit `borrow`, `noalloc`, `parallel` or effect annotations in ordinary code.

Recommend A, which aligns with current implementation. Any optional assertion syntax must validate a claim and fail clearly rather than bypass proofs. Keep explicit semantic choices such as unordered reduction opt-in; do not infer permission to change floating-point order.

### D8. Typed UI intents and handles — API before syntax

A: registered event schemas and nominal generation-checked handles in the capability catalog.

B: new language-level event/opaque-type declarations and callback syntax.

Recommend A for the vertical slice, then assess how much hand-written registration remains. Typed events should carry real IDs and values without lossy conversion through stringified JSON/numbers. If authored declarations remove repetitive C schema work, introduce them as a focused feature with generated bindings and conformance tests.

### D9. AoS/SoA - logical type versus physical storage

#### Proposed storage alternatives

```chibi
// PROPOSED logical type: unchanged by collection layout
Particle :: struct {
    x: float
    y: float
    vx: float
    vy: float
    life: float
}

// PROPOSED built-in storage types, not current generic implementation
particles: buffer<Particle, aos>
hot_particles: buffer<Particle, soa>
```

Here `buffer<T, Layout>` is a proposed built-in storage family with a constrained layout argument, not a promise to implement arbitrary value-parameterized generics. `buffer<T>` could canonically mean AoS. `Particle` is the same nominal logical value in both containers.

| Alternative | Trade-off |
|---|---|
| **Recommended:** layout in the collection type, `buffer<T, aos/soa>` | Signature/ABI exposes physical representation; explicit conversion and borrowing rules. Needs new container/compiler/runtime support |
| Separate built-ins, `aos<T>` and `soa<T>` | Easier initial specialization with current generic limitations, but doubles names and can make layout-neutral APIs clumsy |
| Per-variable annotation on `T[]` | Attractive notation, but risks hiding layout differences in a type with existing snapshot/alias semantics. Requires explicit rules at function boundaries |
| `T :: soa struct { ... }` | Conflates a logical record with one use's storage; the same type cannot naturally be AoS in an inspector and SoA in a simulation. Do not select this model |

**Packed typed storage** here means contiguous typed payloads without one `pin_value` header per element. It does **not** mean unaligned, byte-packed C structs. Honor alignment, checked allocation arithmetic and target widths.

#### Proposed representation and API contract

- **Start with plain records.** Derive eligibility from complete field descriptors; reject managed strings, owned collections, union payloads, callable environments and unsupported nested types until their lifetime paths are explicitly supported. The existing `type_is_plain` predicate is a useful baseline, not an automatic proof of every future type's eligibility. [Plain records][s-plain]
- **AoS:** contiguous aligned `T` records, with count/capacity/stride and one owner. **SoA:** same logical count/capacity with one typed column per top-level field. Initially keep a nested record within its field column rather than recursively flattening an arbitrarily deep type. Recursive flattening and AoSoA/block layouts are separate future decisions.
- **Keep current arrays' value semantics unchanged initially.** The new buffer is an explicit owner-backed mutable collection. A by-value record read gives a snapshot; a buffer mutation targets the owner. Copy/clone/share of a buffer must be explicit, rather than making an ordinary array assignment unexpectedly share mutable storage. Owner decision: move-only owning buffers with explicit clone versus handle-backed service-owned buffers; the latter fits a narrow initial host service, the former is a larger language ownership feature.
- **Layout-neutral access:** a record read assembles a `T` snapshot in field order; a record write scatters fields with validated types. Do not advertise a SoA row proxy as an addressable contiguous `ref T`. If mutable row views are exposed, use a distinct scoped proxy with declared field effects.
- **Column access:** typed spans expose contiguous field data for bulk work. Scope them to a borrow region; prevent owner resizing, conversion, reordering, destruction, concurrent conflicting writes or suspension during the borrow. Multiple mutable spans require disjointness proofs or runtime rejection. A read-only view must not be mutated through another alias behind the optimizer's assumptions.
- **Structural operations:** growth/appending/removal must keep all SoA columns in lockstep; stage failure-prone work before publication. Specify order-preserving removal versus swap-remove as different operations. Length/capacity arithmetic, alignment and zero-sized/empty behavior need bounds checks and consistent failure semantics.
- **Identity/lifetime:** an array index is a position, not a durable object identity. Initially prohibit retaining row references across structural changes. Durable element handles require a collection identity plus stable slot/generation mapping and validated lookup; merely combining a row index with a collection pointer is insufficient after reorder. Use ECS/entity IDs or explicit application IDs when identity is the real requirement.
- **Explicit conversion:** AoS↔SoA allocates/copies or consumes a uniquely owned buffer under a documented operation. Preserve logical field values and iteration order. Do not bit-cast/reinterpret one layout as the other, silently convert on every function call, or claim zero-copy conversion. Retained views are invalidated/rejected according to their borrow contract.
- **Reflection/debug:** describe logical fields and physical layout separately. Inspectors should still show records; profiling should expose boxed versus packed storage, allocated/retained bytes, gathers/scatters, conversions and per-column costs. Serialization remains logical/schema-based so it does not depend on the in-memory storage choice.
- **FFI:** expose a typed descriptor containing schema/layout identity, length, field addresses/strides/alignment, access rights and lifetime. Borrow only for the call unless an explicit retained-owner API exists. A native function expecting `T *` can borrow compatible AoS only; it must use a SoA-aware adapter or an explicit materialization for SoA.
- **ECS integration is a separate decision.** The first storage facility can serve app data and bulk gameplay buffers without changing ECS. Replacing component internals with field-SoA would affect native component pointer APIs, storage-preserving spans, query execution, reflection, serialization, layout hashes, schema reload and every host assuming contiguous component records. Do not conceal that change behind a declaration annotation.

#### Performance and SIMD gate

Use paired measurements, not “SoA is faster” as a requirement. Candidate workloads: field-only particle/motion passes, full-record random access, editor result sorting/filtering, inspector row materialization, append/delete churn and boundary conversion. These are hypotheses about useful workloads, not measured results.

Compare three baselines independently: current boxed value arrays, packed typed AoS, and packed typed SoA. This separates the benefit of eliminating per-element tags/indirection from the benefit of transposing fields. Retained buffer reuse and batching may win before a new SIMD backend is needed.

For each supported target and representative size, measure the whole operation including conversions, allocation, host boundaries and record materialization. A useful measured crossover condition is:

`conversion cost + repeated SoA passes + required materialization < equivalent repeated AoS passes`

Require a reproducible end-to-end gain outside baseline uncertainty and no forbidden latency/memory/correctness regression. Derive size/pass-count thresholds from that experiment; do not invent a universal element count or percentage. An editor's small random record edits can have a different result from a large field-only game loop.

SIMD is an additional optimization gate: prove alignment or safe unaligned access, bounds/tails, alias freedom, effect safety, feature detection and scalar fallback. Preserve existing floating-point behavior and deterministic reduction order unless the owner explicitly chooses a different contract. SoA eligibility alone is not proof that vectorization occurs or that it is profitable.

**Owner decisions:** explicit buffer layout type versus separate built-ins; owning-buffer model; initial plain-field scope; and whether the first demonstrated workload requires SoA or only packed AoS. Recommended: keep logical structs storage-neutral, preserve current arrays, start with a narrow owned-buffer contract and measure all three representations before broadening ECS.

### D10. Stricter syntax, bare names and canonical formatting

#### Remove the optional declaration keyword precisely

**Current equivalent root declarations:**

```chibi
behavior Counter {
    state count: int = 0
}
```

```chibi
behavior Counter {
    count: int = 0
}
```

**Proposed canonical rule:** omit `state` at behavior/library/system root; after the coordinated grammar migration, reject this redundant spelling there with a specific fix-it. Preserve persistent-state semantics, including system-root declarations. Do not reinterpret every root `x := ...` as an ephemeral local as a side effect of this cleanup.

**Important current distinction:** inside a callable body, `state count: int = 0` declares persistent state; `count: int = 0` is a local declaration. The current grammar intentionally separates these. The recommended strict language therefore keeps `state` **mandatory where it selects that storage kind**, rather than banning the token everywhere. `shared` similarly changes storage and must not be removed. [Grammar][s-state-grammar] [Semantics][s-state-semantics]

If the owner wants the declaration keyword gone everywhere, there are two materially larger alternatives: prohibit callable-scoped persistent state and require moving it to an enclosing owner record/root, or introduce an explicit persistent-state block/annotation. Both change source organization, identity and possibly initialization/reload behavior; neither is a formatter simplification. The first can also create name collisions and change scoped state IDs. Decide it explicitly before migration.

**Preserve the disambiguation capability, using the user's target spelling.** Today an argument/local `count` can shadow root `count`, and `state.count` denotes persistent storage. The existing semantic minimizer may shorten a particular `state.x` only when re-analysis proves it resolves to the same symbol. The target style is bare `count` normally and `self.count` only when that qualification is necessary. Making `self.count` resolve to that persistent member is a separate resolver change; current `self` instead denotes the owner handle, whose member lookup resolves components/relationships. Retire old `state.x` through a semantic migration only after that target is implemented. [Qualifier][s-state-qualifier] [Minimizer path rewrite][s-rewrites] [Owner symbol][s-self] [Owner members][s-self-members]

#### Bare names first; `self.` only to select the intended member

```chibi
// PROPOSED target binding semantics
behavior Counter {
    count: int = 0

    Increment :: () {
        count += 1
    }

    SetCount :: (count: int) {
        self.count = count
    }
}
```

Here the unqualified `count` in `Increment` is the persistent member. In `SetCount`, bare `count` is the parameter and `self.count` selects the persistent member. Do not require `self.` throughout the file merely because one function has a collision. `self` alone is an expression/receiver identity; it does not declare storage or turn a local into persistent state. Root declaration context and any required callable storage selector continue to determine lifetime.

**Recommended resolver precedence, explicitly proposed:**

1. Nearest lexical local, parameter, result binding or explicit lambda capture, then enclosing eligible lexical bindings, with existing capture restrictions enforced.
2. The current lexical owner's own declared members/persistent state and explicitly available component names. Do not recursively flatten every component's fields into a single namespace: `Health.current` can select an owner component while arbitrary bare `current` cannot search all components.
3. Module/library-level static definitions, then explicitly imported names and available native catalog names. Multiple equally eligible imported/global candidates are an ambiguity error, not a choice based on registration/import order.

`self.member` skips ordinary lexical locals and selects the current lexical owner's member namespace. It never falls back to an unrelated module/global if the member is absent. A collision between an owner persistent member and an owner component/relationship name should be rejected with both declarations identified; do not silently let the new state-member resolver take over an existing component path. Keep function/type/value namespace details explicit and apply the same documented binding policy wherever the syntax admits the name, rather than changing precedence when a value is called.

**Receiver semantics remain an owner decision; the bare-name style does not settle this table.**

| Scope | A. Recommended lexical persistent-state qualifier | B. Entity/record receiver only |
|---|---|---|
| Behavior/system per-owner state | `self.x` selects the current lexical instance's slot, independent of local shadowing | `self.x` selects only an actual entity/record member; persistent state must live in an explicit receiver record or use a separately specified owner-qualified path |
| System `shared` state | `self.x` resolves to the registration-owned slot and keeps its shared storage/effects | An explicit registration-state receiver or qualified registration namespace is required |
| Callable-scoped persistent state | `self.x` selects the nearest lexical persistent declaration, then enclosing persistent declarations | Explicit callable-state receiver/qualification, or refactor persistent state out of the callable |
| Library persistent state | `self.x` selects the library execution instance's own namespace, never the caller's same-spelled slot | Explicit module/state-owner qualification or a receiver parameter; `self` is not invented for the library |
| Existing entity `self.Position` / bare `self` | Preserve entity lookup/value where available; reject name collisions rather than silently rebind | Preserve the existing entity/record receiver concept, with less overloading but additional state-access notation |

**Option A's proposed complete scope rule:** make `self.` a compiler-resolved lexical owner qualifier, not a new implicit heap object. In a behavior it selects that behavior instance's persistent state; in a system it selects the declared per-owner or registration-shared slot according to the declaration's existing storage kind. Within a callable it first selects the nearest callable-scoped persistent declaration, then enclosing persistent declarations; it bypasses ordinary parameters/locals but does not promote them. For a library it selects that library execution instance's existing persistent namespace, never the caller's same-spelled field or process-global storage. This is a deliberate extension because today's library has no automatically introduced `self` symbol. The import/owner binding must be explicit in lowering and reflection, with owner teardown/reload tests. Library state already appears in checked-in execution tests. [Library-state test][s-library-state]

Under option A, existing entity access such as `self.Position` remains an entity-component path when that owner capability actually exists, with the collision diagnostic above. Bare `self` retains its existing entity-handle meaning where available; the new qualification syntax does not invent a first-class entity for a library or alter a function's receiver type. A receiver-free library using an explicit record/entity argument continues to use that argument. Option B is also viable, but its explicit state-owner notation/refactor must be specified for every row before retiring the old spelling. Do not claim that all `state.x` references can become `self.x` until this owner decision and per-occurrence binding checks are resolved. Preserve old syntax only as needed during migration, rather than choosing two permanent canonical aliases by accident.

**Shadowing diagnostics:** reject duplicate declarations in one scope and true ambiguities; report local/parameter/capture shadowing of a member with both definition locations and the necessary qualified spelling. Deliberate `SetCount(count)` remains legal. Highlight a binding change introduced by a refactor/import/member addition before applying automatic edits; do not silently reinterpret an existing use. A rule that requires owner qualification everywhere would contradict the requested style.

**Capture binding:** resolve each explicit capture source at the lambda creation site and record the resolved symbol/member ID plus capture mode. A captured `count` means that resolved value, not whichever same-spelled local/member is present when the callback executes. If a local shadows the intended member, qualify the capture source with `self.count` and use an explicit capture alias where the chosen capture grammar requires one. The lambda's owner is lexical, not a dynamically rebound receiver. Owner reads/writes still need recorded capture/effect/lifetime contracts; bare spelling must not hide a reference capture or an entire retained owner.

**Canonicalization:** remove `self.` only when re-resolving the shortened expression under the same catalog/scope selects the identical owner/member identity with identical type, optional-presence conditions, access/effect and capture/lifetime semantics. Preserve it in `SetCount`. Never add it routinely to unambiguous uses, and never delete a receiver expression with evaluation effects merely because its result happens to equal `self`. Add tests for local/parameter/capture/import shadowing, component-name collisions, nested lambdas, ref arguments, renamed members, newly added imports, and reload identities.

#### Proposed small canonical surface

- `name: Type` for annotated declarations/parameters; `name := value` when inference is intentionally permitted. Type-first spellings are another optional future strictness decision, not required to remove `state`.
- `Name :: (parameters) -> Result { ... }` for named functions. The current optional `fn` can be omitted canonically; keep `pure` when present because it expresses a checked contract. A future lambda still needs its `fn` introducer and capture list.
- Newline-separated statements with semicolons retained when necessary to preserve parsing. The scanner's continuation rules inspect following operators, `(` and `[`, and even newlines inside comments; semicolon deletion is not a global text edit. [Scanner][s-scanner]
- Do not conflate optional **syntax** with semantically redundant **contracts**. Keep `shared`, `ref`, `pure`, `disabled`, effect/suspension/ownership qualifiers, explicit units/ranges, and necessary type/capture/layout annotations. Removing one because the current optimizer could infer a similar result can change API guarantees or future edit behavior.

#### Use the existing formatter/minimizer boundary safely

The current whitespace formatter's complete-CST equality intentionally rejects token deletion. Keyword canonicalization should be a small syntax/semantic rewrite stage before that printer, with a documented allowlist. Do not weaken the whitespace formatter's safety check to permit arbitrary different trees.

The existing semantic minimizer is a useful foundation: it compares typed symbols, storage kinds, calls, arguments in evaluation order, function effects/purity and statements; it reanalyzes candidates using the same catalog and identity. It already attempts broader changes such as inferred declarations, argument simplification, cast removal, state-path shortening and metadata removal. [Comparison][s-minimize-compare] [Candidates][s-rewrites] [Reanalysis][s-minimize]

**Recommendation:** provide a canonical-only rule set for ordinary Format. Keep broader simplification explicit or separately governed, rather than automatically adopting every current minimizer reduction as the new formatting contract. In particular, a human-written description/comment or useful explicit type is not disposable just because execution is unchanged. The CLI currently composes the broad minimizer with formatting; align editor/CLI/check policy deliberately through one shared high-level implementation, not divergent printers. This changes policy, not a claim that the current implementation is broken.

#### Required proof/validation contract for canonicalization

1. Parse original text with the correct accepted source version. Invalid/incomplete source is unchanged and gets a clear result; never “repair” meaning by deleting tokens until it parses.
2. Identify a specific syntax node and scope. For root `state`, prove it is the redundant storage token on a root state declaration, not a literal, comment, member qualifier or callable-scoped storage selector. For named `fn`, prove it is that declaration's optional token, not a future anonymous-callable introducer or type.
3. Apply token-range edits preserving comments, literal bytes, order and line-boundary semantics. A comment attached to a deleted token must remain sensibly attached. No regex/global replacement.
4. Reparse and compare an explicitly normalized AST/CST that permits only the intended canonical differences. Then compare resolved typed meaning under the same dependency/catalog snapshot and document identity: symbol bindings, storage/lifetime kind, initialization/evaluation order, type widths, nominal/schema identities, default arguments, units/ranges, reference/capture modes, callable effects, suspension and layout contracts.
5. Strengthen the current equivalence checker as new types/closures/layouts are added. It intentionally ignores some reference-marker differences today; that is not permission to ignore future ownership or capture differences. A semantic comparator is an engineered safety oracle, not a formal proof of arbitrary transformations.
6. Verify stable runtime state/callable identities and reconcile source/graph anchors, diagnostics, breakpoints, selection/caret and undo. Symbol IDs currently depend on document identity, scope and name; moving declarations is materially different from deleting a redundant token. [Symbol IDs][s-symbol-ids]
7. Require idempotence, equivalent lowering/execution and unchanged diagnostic behavior for accepted source. Cover every parse context, shadowing, necessary/redundant `self.`, capture binding, quoted names, comments, adjacent statements, multiline expressions, `shared`, callable state, pure/ref declarations, units, generic signatures, malformed drafts and new constructs. Existing tests already exercise formatter idempotence, argument-order preservation and invalid-source preservation; extend that corpus rather than replacing it. [Idempotence tests][s-format-tests] [Order/invalid-source tests][s-minimize-tests]
8. One SDK implementation serves Studio Format, format-on-save if enabled, CLI format and non-writing check mode. Apply one editor undo transaction and fail without partial source/metadata publication. The normal formatter should not silently perform a project-version upgrade.

**Owner decisions:** the bare-name/necessary-`self.` style is already directed by the user. Choose receiver option A or B and confirm the detailed resolver/collision rule, canonical omission only or rejection of redundant root `state`, required callable `state` versus its larger removal, canonical omission/rejection policy for named `fn`, and the default boundary between Format and broader Simplify. Recommended: option A and the resolver above, strict root omission after migration, preserve required callable `state`/`shared`, migrate necessary `state.x` to correctly bound `self.x` only after option A is approved/implemented, omit named `fn`, and a narrow default canonical rule set.

### D11. Language migration and version policy

The repository already requires direct migration of used contracts rather than permanent legacy aliases. `CHIBI_PROJECT_VERSION` is currently 24. Its documented criterion is a named existing engine/Studio/Celeste project that would load incorrectly or fail, where a migration step rewrites it. Additive features and semantically unchanged formatting alone do not qualify. [Version policy][s-version] [Repository rules][s-rules]

There is concrete usage: the `test/fixtures/studio-workspace` project is version 24, and `content/behaviors/Weapon.chibi:5` contains root `state elapsed`. Rejecting optional root `state` would break an existing project, so that strict parser change qualifies for a version bump plus migration under the current rule. Simply making formatting omit `state` while continuing to accept old source would not by itself qualify. [Fixture manifest][s-fixture-manifest] [Fixture source][s-fixture-source]

Proposed rollout, without implementing it in this assessment:

1. Approve the canonical grammar/capture/record/storage contracts independently; do not bundle all language changes into one implicit owner choice.
2. Inventory engine, Studio and Celeste source, generated templates/examples, parser corpus, metadata and any third-party SDK consumers. Classify every keyword occurrence by parsed context. Audit callable-scoped state rather than assuming root-only usage.
3. Implement the safe canonicalizer and migration against the previous grammar. Historical migration parsers are already a supported pattern: the current project migrator keeps a version-23 parser separate from the live language. [Historical parser][s-old-parser]
4. For a breaking used contract, bump the then-current version once for the coordinated contract change; do not hard-code an assumed future version in this plan. Migrate engine-owned projects in the same PR; other repositories upgrade when opened according to repository policy. Do not open version-number-only PRs for Studio/games.
5. Verify transformed source and metadata under the new parser/analyzer, preserving storage and behavior. Stage files before replacement; preserve failure recovery/idempotence, and write manifest completion only after successful source migration. Existing migration staging is a foundation, not a guarantee that a new rewrite is automatically safe. [Staging][s-migrate-staging]
6. Remove obsolete production aliases once the migration and authored assets are ready. Keep historical parsing only in the migration layer. Provide a focused diagnostic/fix for standalone hand-authored source outside a versioned project.
7. New `struct`, lambda or buffer syntax is additive if it does not alter existing source interpretation or stored contracts. Do not bump project versions just because compiler/backend representations change. Do invalidate relevant build/cache/layout fingerprints, and require a project migration if a real persisted value/schema becomes incompatible.

### Requested-feature priorities and gates

| Priority | Bounded deliverable | Must be true before claiming it ready |
|---|---|---|
| P0 | Owner decision record and canonical syntax specification | All distinctions above are explicit; bare names by default and `self.` only where binding requires it; no accidental lifetime/binding changes; migration trigger names an actual affected project |
| P0/P1 | Shared canonical keyword removal and strict-root migration | Context-aware edits, same typed meaning and stable identities, source/metadata round-trip, idempotence, useful invalid-source behavior, shared editor/CLI/check path |
| P1 | Authored nominal value structs | Complete type/ownership/default/equality/reflection/serialization/reload contract; native and WASM/bytecode parity; no borrowed backing escapes; ordinary Studio record and a separate game example |
| P1/P2 | Explicit non-escaping capturing lambdas | Capture diagnostics, checked noescape ABI, effect/alias analysis, source/debug support, measured allocation/call cost, real synchronous callback examples |
| P2, only if needed | Stored/escaping callbacks and task-producing closures | Named owner/cancellation policy; no dangling local/component refs; no unbounded cycles/retired generations; reload/close/queued-invocation cases; explicit allocation accounting |
| P2, measured | Packed typed AoS buffers | Checked ownership/borrows and typed boundary ABI; snapshot compatibility; representative boxed-versus-packed measurements |
| P3, measured | Opt-in SoA plus optional SIMD optimization | End-to-end crossover including conversion/materialization; identical results/order; lifetime/alias/resize proof; target fallback and no hidden ECS ABI change |

Priority is dependency/risk ordering, not a claim that the entire Studio migration must wait for every feature. A disposable language/SDK specimen can use existing named functions, reflected records and typed host services while contracts are being decided. It is evidence for Stage 1, not production conversion or the completed Stage 3 vertical slice; Stages 0–2 remain prerequisites. Conversely, a feature cannot be called complete just because its sample parses: authoring, lowering, debugger, serialization, reload, host ABI and admitted validation must agree.

Implementation is now authorized, with validation through the repository's C/pico_unit dispatchers and current admitted-command/60-second policy. The new implementation work may prepare and run its admitted checks/builds through the coordinating worker; it must not resume the separately paused build, bypass quarantine, install an unapproved formatter or treat this request as release of unrelated explicit holds. Missing affected-contract evidence remains a gate; it is not a pass.


## 11. Dependency-sequenced roadmap

Implementation is authorized in this order. Each stage produces reviewable evidence on an exact engine/Studio pair. No stage is complete because it compiles. Source, semantic, runtime, interaction and performance acceptance are separate checks. Preserve unrelated explicit holds, the paused build, admitted-test policy, exact-head independent review and branch retention. A missing tool/environment or inadmissible test is a specific blocker to report, not permission to bypass the gate.

### Stage 0 - Freeze the oracle and record the endpoint

- Record exact sources/SDK/assets/toolchain and supported platform matrix; inspect any additional branch the owner identifies.
- Reconcile every native file with build ownership, public API, migration destination and temporary/permanent exception.
- Inventory all commands, panel states, documents, history operations, async workflows and current acceptance selectors.
- Establish baseline health and decide any current errors/defects separately.
- Record proposed native boundaries as candidates; require the authored attempt/concrete irreducible-boundary justification per slice. Confirm parity policy and present the first unresolved syntax decisions, without blocking syntax-neutral compiler/evidence work on their answers.

**Exit:** a versioned parity manifest, exhaustive ownership inventory, agreed endpoint and a baseline that can be exercised reproducibly. No conversion yet.

### Stage 1A - Remove proven engine-boundary overhead without syntax changes

Start after the relevant Stage 0 fixtures/ownership baseline is pinned; independent source/spec preparation can proceed immediately.

- Add bounded, admitted differential specimens for bound query-variable loads and nonescaping scalar component reads. Record current IR/helper/allocation behavior and checked errors before changing lowering. Build shared semantic IR/lowerings that exploit existing entity/layout knowledge; compare bytecode, actual SLJIT and actual browser Wasm paths.
- Preserve negative-case fallback and source/debug/budget semantics. Inspect whether the intended fast path is actually reached rather than inferring success from backend support or a benchmark label.
- Add the narrow typed component write/publication operation only after tests cover ranges, managed ownership exclusions, change versions/dirty rows/ON_SET, same-value writes, missing/stale entities and compound-assignment ordering. Do not replace the publication contract with a raw store.
- Extend verified engine descriptors and generated bindings where the specimens need them; add semantic-contract/ABI/layout/generation invalidation. Measure native typed calls versus generic value-bank calls without claiming arbitrary C-body inlining.
- Keep allocator/storage decisions internal and logical-reference fallback correct across captures, suspension, effects and reload. Do not change visible syntax or persistence formats in this slice.

**Exit:** admitted semantic and rejection/fallback cases pass; each claimed backend emits/executes its intended implementation; allocation/copy/helper attribution supports the claim; publication, ownership, error/debug and reload behavior are unchanged. Native-only optimization is partial evidence, not browser readiness. A small optimization may land independently once its exact-head review/tests pass, but does not unlock broad Studio conversion by itself.

### Stage 1B - Specify and prove the minimum authored language surface

- Present the unresolved syntax examples in section 10 to the owner before grammar changes. Record selected alternatives explicitly. Implement contextual canonicalization separately from strict grammar/project migration; honor bare names and necessary-only `self.` without changing binding or storage lifetime.
- Write executable conformance examples for records, collections, modules, outcomes and resource lifetimes. Authored value structs are foundational; add the useful missing facility before the dependent production feature instead of manufacturing a new C model for each case.
- Implement capturing lambdas first with authored-to-authored nonescaping consumers, then owned/escaping callbacks when a named feature needs them. Retained callbacks require the owner/reload/reentrancy gate. Keep ordinary call-site allocation/borrow choices automatic.
- Implement efficient ordinary collection construction/mutation under preserved value semantics. Complete generic specialization when readable typed module composition requires it; do not force unmaintainable concrete duplication to avoid language work.
- Compare boxed arrays, packed AoS and SoA on the paired Studio/game specimens. Develop the requested storage facility alongside relevant migration work; do not require every UI feature to wait for optional SIMD/storage refinements. Do not change ECS storage ABI silently.
- Audit managed capture retention, string lifetime, collection copying and task result reclamation. Reconcile spec/docs for effects, reductions and reload. Include inline/local/imported-helper game specimens, then extend verified intermodule proof where required so good modular structure does not impose unexplained serialization.
- Run the formatter comparison/configuration design in parallel; choose one layout engine and authoritative API from evidence. Required dialect/context/canonicalization behavior is not optional.

**Exit:** visible syntax used by the next slice has owner approval; positive/negative conformance tests pass on admitted paths; backend semantics agree; ownership is automatic at ordinary call sites and well defined; no critical gap remains in the planned state model. Later features may proceed alongside later slices, but a dependent slice cannot bypass its feature's gate.

### Stage 2 — Build the application-facing SDK and evidence tools

Depends on the relevant Stage 1A/1B data, ownership, effects and lowering contracts; independent API design and recording adapters can proceed earlier.

- Add narrow editor operation families with generated/type-checked schemas and verified ownership/effect/cost metadata. Known engine operations use compiler-visible lowering or typed calls where justified; generic capability calls are fallback boundaries. Avoid new opaque C product-policy facades.
- Define document transactions, durable saves, job completion tokens, UI instance lifecycle and batched model publication.
- Add independent recording adapters and differential traces; instrument host crossings, copies, live/retained memory and UI work.
- Integrate the selected configurable layout engine through the shared `.chibi`/RML/RCSS format/check API and corpus. Keep semantic canonicalization separate and scope-aware; add optional Prettier/editor adapters without duplicating formatting policy.
- Establish native/web compiled-backend packaging and tests for the exact SDK pair.

**Exit:** a synthetic feature can own state, call services, publish UI, cancel, fail and reload without leaking or corrupting the native application; ordinary game builds cannot gain editor services accidentally.

### Stage 3 — One complete vertical slice

Use Outliner as the migration specimen because it already has authored behavior, typed native operations, virtualization and hierarchy acceptance. Include its header, filtering, selection, rename, context menu, drag/reorder, undo/redo and per-view state; do not count a static header as the proof.

- Rewrite its source idiomatically using current/approved language features.
- Pull forward the minimum shared scene-command and history subset needed by this feature: selection/toggle/range anchors, filtering/order, rename/path updates, visibility/lock/isolate, add/drop/reparent/reorder, edit-session completion, permission checks and their undo/redo behavior. `.chibi` owns these decisions and transaction grouping. Stages 5 and 7 extend this authored foundation to the rest of documents/runtime; they do not defer this slice's existing product policy.
- Pin an exact temporary native allowlist: versioned scene snapshots, typed ID lookup, engine ECS/world primitive operations, compiler/serializer services and generic owned snapshot/storage mechanisms only. The adapters must not call the old controller operations to choose or execute the product command. In particular, `select_object`, `set_view`, `drop_on_object`, `studio_rename_commit`, `studio_isolate`, `studio_add_object` and `studio_outliner_drop` behind `studio_node_run` are migration inputs, not acceptable permanent “transaction primitives.” [Current policy-bearing dispatch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_node_calls.c#L249-L358).
- If the shared command/history subset cannot yet be authored safely, label the result an incomplete UI/controller prototype, keep the stage gate closed and fix its prerequisites. Do not call a native-backed command facade the completed vertical slice.
- Exercise two view instances with the agreed shared/independent state contract, cancellation during gestures, reload with live state, invalid draft and large-list scaling.
- Compare native/authored traces and identical captures; benchmark both.

**Exit:** complete functional/input/pixel parity for the slice; measured budgets met; retained memory returns to its bound; no hidden call-through to the old controller. If this fails, fix language/SDK before translating more panels.

### Stage 4 — Shared components and lower-risk panels

Convert Members, Output, Settings/Shortcuts, header/chrome controls, palette/pickers, menus and dialogs in small feature groups. Use reusable typed list/field/menu components. Preserve first-open menu freshness, keyboard focus, notification deduplication and closed-menu work suppression.

**Exit per feature:** its C policy is removed, all command paths share the new implementation, goldens/interaction traces agree, idle work does not regress, and formatting/docs/examples are updated.

### Stage 5 — Assets, Properties and document authority

Extend the scene transactions/history established in stage 3 to all document types. Convert asset indexing/filter/grid/list policy, reflection-driven field models, multi-selection/mixed values, live edit sessions, histories, tabs, save/revert/close prompts, refactoring and checkpoint coordination. Use shared compiler-visible parsing/compilation and efficient text/value-buffer operations where their concrete boundary is justified; portable new document algorithms remain authored.

Explicitly separate source draft, applied runtime, saved bytes and durable browser storage. Preserve atomic multi-file edits, exact integer values, unknown supported fields, undo grouping and stale-generation rejection.

**Exit:** documents survive edit/save/reload, failures, undo/redo, checkpoint restore and collaboration replay with exact state/bytes; no duplicate authority remains in C.

### Stage 6 — Code, graph, Designer and Inspector

Move code/graph product policy, navigation, diagnostic presentation, debugging controls, graph interaction/layout policy, Designer edits and Inspector view models. Attempt authored graph composition/geometry and low-level orchestration with the required compiler support. Retain the existing foreign Zep/RmlUi ABI and any measured kernel only behind a narrowly justified interface; their C implementation is not a blanket exemption for surrounding editor logic. Measure representative workloads and improve the language/runtime before accepting a performance-based native exception.

**Exit:** source/graph editing stays one truth; exact source identities and breakpoints survive intended edits; no stale DOM/component handles; pointer and keyboard parity; unsupported projection shapes fail honestly rather than silently dropping semantics.

### Stage 7 — Scene/workspace/runtime orchestration and remaining services

Move workspace lifecycle, camera/gizmo interaction, scene edit commands, play/pause/step/stop/keep-changes, guest Studio, thumbnail scheduling, export and collaboration policy. Preserve existing renderer/physics/platform behavior through compiler-visible operations; keep native only where a concrete external/OS or measured-kernel boundary requires it. This is the highest-risk authority/lifetime slice and follows proven transactions, records and services.

**Exit:** hierarchy/path/transform/physics and gameplay input remain correct; focus-loss/cancel cases pass; workspace replacement stages before committing; failures preserve the previous workspace and pending user work; platform/browser workflows pass.

### Stage 8 — Remove transitional C and switch the default

- Reconcile the native inventory again; no C product-policy fallbacks or duplicated source tables remain.
- Delete migration-only adapters and runtime switches after evidence is accepted; preserve the oracle as a pinned test artifact/branch, not permanently shipped duplicate application code.
- Review native exceptions one by one; retain only the agreed mechanisms.
- Validate clean installed-SDK builds and packaged assets on supported native and browser targets; publish release evidence and complete docs.

**Exit:** all agreed acceptance/measurement/platform gates pass on the exact shipping pair, the native-boundary audit is signed off, and rollback/recovery has been exercised. Only then make the authored application the default.

### Parallel work without integration chaos

After the relevant Stage 0 baseline is pinned, syntax-neutral engine lowering, semantic/spec decisions, formatter comparison/integration, engine-operation contracts and measurement work can proceed independently with narrow ownership. Broad panel conversion waits for the vertical slice. Keep one integration owner for cross-repository SDK changes; land engine capabilities first, then pin the exact SDK in Studio. A combined candidate must be reviewed/tested after integration. Do not reuse predecessor evidence after merge/conflict changes or leave the build selecting whichever engine release happens to be newest.

## 12. Parity and regression matrix

| Surface | Minimum scenarios beyond happy path |
|---|---|
| Shell/menus/palette | Pointer and keyboard first-open, disabled/check states, shortcut remaps, nested popovers, Escape/outside press, focus return, reduced motion |
| Outliner/scene | Filtered range selection, hidden/locked/spawned objects, rename/path updates, hierarchy cycles, multi-drag, reorder, cancel, world transforms, duplicate views |
| Assets | Grid/list switches, modifier selection, filter retention, virtualization, thumbnail completion after close/filter, batch delete/undo, import failures |
| Properties | Scalar/vector/color/enum/reference fields, exact integers, mixed values, computed read-only fields, numeric scrub/typing, cancel, overrides, one-step undo |
| Code/source | Unicode/IME, standard/Vim input, completion, hover, diagnostics, format/save, invalid drafts, tab close/reopen, separate typing undo groups |
| Graph | Pointer/keyboard selection/navigation, search/Find, framing, wires/literals, source synchronization, rename/refactor, hidden panel relinquishing input |
| Debugger | Actual breakpoints, source/graph alias identity, step/resume/stop, invalid/valid edits while paused, watches, old/new generation ownership |
| Designer/Inspector | Text edits and visual edits in one history, hierarchy/slots/style rules, live DOM changes, stale inspected target, source mapping, scrolling |
| Runtime/viewport | 2D/3D camera, pinch/wheel/pan, gizmos, collider handles, focus loss, hidden scene, singular/nonuniform parents, keep changes, preview isolation |
| Persistence | Save vs durable flush, newer save during in-flight flush, retry, quit warning, import/export exact bytes, project revision isolation, checkpoint remapping |
| Collaboration | Accepted/rejected/replayed actions, stale revision, reconnect/resync, stable IDs, checkpoint restoration, concurrent UI closure |
| Lifecycle | Panel mount/unmount/reload, workspace replacement, guest Studio, long-lived tasks, cancellation during shutdown, resource disposal order |

Existing Studio selectors supply valuable cases: source edit, unit warnings, scene hierarchy/move, Outliner move, graph visibility/pointer/keys, asset pointer/selection, debugger source history, panel idle, code keys, unsaved prompts, play keys, export, viewport, camera, gizmo and collider tests. Preserve their assertions while adapting the target under test. Their availability and current admission must be rechecked; the inventory does not authorize running a quarantined aggregate. Browser serial persistence evidence is explicitly narrower than cross-browser/threaded acceptance. [Studio acceptance corpus][S-acceptance] [Browser persistence coverage][S-browser]

## 13. Risks, stop conditions and rollback

| Risk | Guard and stop condition |
|---|---|
| Attractive `.chibi` veneer leaves the old application in C | Per-file responsibility audit; no slice passes with old policy called through an opaque bridge |
| Collection copies create quadratic work | Scaling workloads and copy counters before translation; fix language/representation, do not bury the cost in native wrappers |
| Captures, interned strings or old generations accumulate | Long-session ownership accounting and owner teardown tests; block broad conversion on unexplained retained growth |
| Reactive updates change same-frame behavior or loop forever | Explicit event ordering, bounded settling and trace comparisons; cycle diagnostics and well-defined deferral |
| New syntax outruns compiler/debugger/formatter support | One decision record and conformance matrix per construct; no implementation before owner review |
| API exposes stale pointers or editor authority to game scripts | Generation-checked handles, borrowed scopes, catalog isolation and negative tests |
| Native and Wasm semantics diverge | Actual backend differential tests for numerics, managed values, tasks, refs and errors |
| Parallel optimization changes order/results | Conservative effects, stable ordered publication, explicit unordered opt-in; compare scalar/serial reference |
| Reload loses work or crosses generation boundaries | Stage all changes before commit; explicit state compatibility; cancel/discard stale jobs; preserve invalid drafts and last good runtime |
| Screenshot parity misses functional loss | State/event/input/durable-bytes assertions alongside pixels; real platform input and separate platform goldens |
| Test policy makes required evidence unavailable | Report an admission/authorization blocker; never declare readiness from source review alone |
| Cross-repo drift or old extracted code returns | Exact SDK pin, clean build provenance, extraction-manifest handling, no engine source includes in Studio |

During migration, choose the native or authored implementation per feature at startup in a development build. One side owns mutations. Keep data formats stable where possible; if an actual project contract must change, follow the engine's migration/version policy and test forward/recovery behavior on copies. Do not promise that an older binary can read a newly migrated project without proving it.

On a failed slice, switch that feature back to the pinned native implementation without rewriting user documents. On a failed source publication, retain the prior valid generation. On a failed workspace replacement, retain the existing workspace. On a failed durable save, retain pending state and a reachable Retry. Before deleting the native path, retain a reproducible oracle artifact and verify checkpoint/project recovery. Feature switches are transitional validation tools, not permanent legacy architecture.

## 14. Completion checklist

The migration is complete only when all are true:

- Studio product behavior, portable algorithms and composition are authored `.chibi`; every remaining native responsibility has a concrete, reviewed exception after the authored/language alternative was considered.
- Existing RML/RCSS appearance and interaction contracts match the pinned oracle, including edge and platform cases.
- Language semantics are documented and tested; current examples compile, format and debug correctly.
- Native and browser execution agree on supported semantics, including failures and lifecycle boundaries.
- Representative Studio and game workloads meet measured budgets, scale appropriately and reclaim memory; ordinary authors do not manage allocators and known engine operations avoid unnecessary generic bridge costs.
- Source/graph authority, history, reload, persistence and collaboration invariants survive the migration.
- Shared formatting is non-destructive, deterministic and integrated into the actual save/CLI/check workflows.
- Exact-head independent review, admitted tests, applicable builds and platform acceptance are recorded; unavailable coverage is explicitly blocking or deliberately scoped by approval.
- The old C product implementation and transitional adapters are removed, the SDK boundary stays clean, and recovery is demonstrated.

The authorized next action is the Stage 0 baseline and Stage 1A engine-aware lowering/ownership specimen, with syntax decisions and formatter comparison proceeding in parallel. Broad conversion follows the readiness and complete vertical-slice gates. It is not a mechanical repository-wide translation.



[E-architecture]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/architecture.md#L9-L114
[E-borrow]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/borrow.h#L14-L118
[E-format-cli]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/automation/controller.c#L6105-L6131
[E-generics]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L1158-L1188
[E-index]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/arrays.c#L200-L336
[E-language]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/code-language.md
[E-migration]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/docs/studio-rml-migration.md#L1-L81
[E-policy]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/AGENTS.md#L13-L18
[E-ui-format]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/assets/ui_source_format.c#L1-L277
[P147]: https://github.com/waldnercharles/chibi/pull/147
[P169]: https://github.com/waldnercharles/chibi/pull/169
[P68]: https://github.com/waldnercharles/chibi-studio/pull/68
[S-acceptance]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/README.md
[S-app]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/application.c#L153-L267
[S-browser]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/BROWSER.md#L48-L90
[S-buffer]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/ui_buffer.h#L118-L131
[S-build]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/CMakeLists.txt#L64-L142
[S-history]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_history.h#L19-L177
[S-node-calls]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_node_calls.c#L108-L352
[S-outliner]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/projects/studio/content/behaviors/OutlinerRows.chibi#L1-L90
[S-source-sync]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_sources.c#L1332-L1442
[S-source-table]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_sources.c#L2382-L2456
[S-sources]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/studio_sources.cmake#L1-L153
[S-ui-events]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_ui.c#L542-L618
[S-ui-mount]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_ui.c#L794-L874
[S-ui]: https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_ui.c#L148-L262

[s-grammar-roots]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/grammar.js#L124-L206
[s-literal]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_expression.c#L832-L879
[s-component]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/component.c#L177-L230
[s-declared]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/runtime/declared_components.c#L95-L244
[s-value]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/value.h#L10-L63
[s-array-header]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.h#L10-L57
[s-type]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/type.h#L11-L74
[s-snapshot]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.c#L150-L226
[s-copy]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.c#L274-L333
[s-equality]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/array.c#L336-L384
[s-ecs-columns]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ecs/ecs.c#L116-L128
[s-ecs-growth]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ecs/ecs.c#L235-L256
[s-ecs-view]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/ecs/execution.h#L7-L23
[s-functions]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/grammar.js#L274-L301
[s-no-capture]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_expression.c#L555-L575
[s-ir]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic.h#L8-L89
[s-continuation]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L5360-L5436
[s-task-test]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/program.c#L6514-L6556
[s-generics]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/lower.c#L1158-L1188
[s-state-grammar]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/grammar.js#L186-L233
[s-state-semantics]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_declaration.c#L30-L54
[s-state-qualifier]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_calls.c#L209-L229
[s-lookup]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_context.c#L77-L114
[s-format]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_format.c#L158-L310
[s-minimize]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_minimize.c#L1164-L1242
[s-minimize-compare]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_minimize.c#L51-L114
[s-rewrites]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/authoring/source_minimize.c#L1042-L1124
[s-canonical]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/automation/controller.c#L5868-L5916
[s-host]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/host.h#L15-L55
[s-reload]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/execution/program.c#L2553-L2815
[s-plain]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/types/type.c#L386-L409
[s-scanner]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/src/scanner.c#L33-L104
[s-symbol-ids]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_context.c#L143-L160
[s-format-tests]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/format.c#L27-L39
[s-minimize-tests]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/format.c#L407-L444
[s-self]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic.c#L94-L108
[s-self-members]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/semantic_calls.c#L368-L411
[s-library-self]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/program.c#L251-L261
[s-library-state]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/language/program.c#L1290-L1328
[s-version]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/assets/project.h#L11-L17
[s-rules]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/AGENTS.md
[s-fixture-manifest]: https://github.com/waldnercharles/chibi/tree/02f8453211dd4cc57965ba52c645f08a92e53c5e
[s-fixture-source]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/test/fixtures/studio-workspace/content/behaviors/Weapon.chibi#L1-L6
[s-old-parser]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/assets/project_migrate.c#L23-L61
[s-migrate-staging]: https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/assets/project_migrate.c#L4126-L4143

## Appendix A. Complete native responsibility inventory

Static baseline audit, updated with the authorized implementation direction. No repository code was changed, built, tested, installed or published while preparing this document. The classifications are migration candidates, not evidence that the language presently supports every required feature or approval to keep every proposed native portion.

### Baseline and coverage

- Repository: `waldnercharles/chibi-studio`, commit `78cb4d4c47242db9f7af2b9573653d9756050e99`, tree `a91bebf8c9226c4ef974e8fd061db34082ac0755`.
- Complete tree: **867 tracked files**. The companion CSV contains exactly one row for every tree path, including blob SHA, byte count, evidence level and source link.
- Product native sources: **155 = 154 C + 1 C++**, comprising **151** `studio` library sources and **4** host/registry executable sources. Reconciled against [studio_sources.cmake](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/studio_sources.cmake) and [CMakeLists.txt](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/CMakeLists.txt); zero omitted or unaccounted product translation units.
- Product headers: **62 = 61 under src + 1 public application header**. Test headers: **17**.
- Native test sources: **82 = 4 acceptance + 78 held oracle C files**. Product and test native totals are **237**; all headers bring native-source/header inventory to **316**.
- Studio authored project: **264 files**, with **10 .chibi, 81 RML, 102 RCSS, 61 the former document format and 10 metadata files**. The repository-wide 81 `.chibi` files must not be mistaken for 81 Studio application modules.
- `tests/held` has 324 current tree files. Its README reports 321 unique preserved oracle/fixture files; that provenance statement and current directory count measure different things. The held sources have no executable target and are not passing coverage. [Held oracle contract](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/README.md).

#### Disposition meaning

- **MIGRATE**: move Studio-owned data, decisions, algorithms and composition to `.chibi`; delete the old C implementation at cutover. Existing generic SDK calls can remain imports.
- **SPLIT**: author all transferable policy/algorithms and attempt `.chibi` for the proposed mechanism; retain only a concretely justified OS/foreign-ABI/crash or measured-kernel portion behind a typed compiler-visible operation. The old C file is not retained as a callable subsystem.
- **NATIVE_EXCEPTION**: candidate native boundary with a proposed reason, not a permanent exemption. Confirm the exact irreducible functions, authored/language alternatives and evidence. This never authorizes unrelated state, algorithms or workflows in the same file.
- Tests/build tools/browser shell/resources are inventoried separately; none count as migrated product policy.

Evidence is conservative: BODY_REVIEWED means this audit read the body; SELECTED_BODY_OR_INTERFACE means selected implementation or interface only; TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY means the exact function-level split is provisional. Downloading a file is not treated as having reviewed it. Module names below are proposed architecture, not asserted current syntax.

#### Product classification totals

**122 MIGRATE**, **4 candidate NATIVE_EXCEPTION**, **29 candidate SPLIT**. These counts preserve the original file classification, not the final count or size of approved native adapters. All candidate native portions are subject to the `.chibi`-first rule and section 5 evidence. Generated stubs are needed only for actual boundaries; known engine operations should prefer direct/specialized lowering.

### Candidate native exception register

| Current source | Proposed concrete native reason to verify | Constraint |
|---|---|---|
| [src/host/state_file.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/host/state_file.c) | Generic file byte read and temp-write/flush/fsync/rename durability mechanism; contains no editor workflow. Consolidate with SDK file service, expose status/bytes not FILE pointers | Capability-level only; typed handles/results; no Studio policy |
| [src/host/stacktrace.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/host/stacktrace.c) | Crash signal handling, symbolization and debugger trap must operate when language runtime is failing; no Studio policy belongs here | Capability-level only; typed handles/results; no Studio policy |
| [src/diagnostics/web_trace.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/diagnostics/web_trace.c) | Browser performance clock/User Timing ABI bridge; keep generic, with a shared time origin | Capability-level only; typed handles/results; no Studio policy |
| [src/editor/text/zep_bridge.cpp](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/text/zep_bridge.cpp) | Existing C++-API pass-through for Zep. Retain conversion/forwarding only; editor shortcuts, indentation, completion, diagnostics, focus and undo policy migrate from kit_code.c | Capability-level only; typed handles/results; no Studio policy |

The C++ bridge is justified by the repository and its [header contract](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/text/zep_bridge.h), but its complete implementation was not reviewed in this audit. Existing generic SDK rendering, ECS execution, parser/compiler, physics, codecs and RmlUi implementations are outside the 155 Studio source count. Existing external SDK libraries need not be rewritten wholesale to begin, but their location is not a blanket exemption for transferable app/engine algorithms. Prefer compiler-visible operations and authored implementations where feasible; moving the old Studio application wholesale into an SDK package is not migration.

### Product source inventory (all 155)

Each row has an immutable source link. A module destination expresses ownership, not a required one-file-for-one-file translation.

#### src (1)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [studio_registry.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/studio_registry.c) | SPLIT | `studio/bootstrap + sdk/catalog`: Studio module/capability composition becomes authored/generated; generic native catalog construction/type identity and registration remain SDK mechanisms | BODY_REVIEWED |

#### src/diagnostics (2)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [frame_log.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/diagnostics/frame_log.c) | SPLIT | `studio/diagnostics + host/diagnostics`: Frame sample ring and CSV projection move; timing collection and browser export entry are generic host/generated ABI mechanisms | BODY_REVIEWED |
| [web_trace.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/diagnostics/web_trace.c) | NATIVE_EXCEPTION | `host/diagnostics`: Browser performance clock/User Timing ABI bridge; keep generic, with a shared time origin | BODY_REVIEWED |

#### src/editor/collab (7)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [actions.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/actions.c) | MIGRATE | `studio/collaboration`: Editor action dispatch and mutations through the common command transaction path | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [actions_gate.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/actions_gate.c) | MIGRATE | `studio/collaboration`: Collaboration action authorization and editability gating | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [actions_graph.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/actions_graph.c) | MIGRATE | `studio/collaboration`: Graph collaboration action interpretation and application | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [actions_scene.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/actions_scene.c) | MIGRATE | `studio/collaboration`: Scene collaboration action interpretation and application | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [collab.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/collab.c) | SPLIT | `studio/collaboration + host/transport`: Room state machine, ordering, replay suppression, queue limits, presence and recovery move; browser send/control/close transport stays behind a bounded host port | SELECTED_BODY_OR_INTERFACE |
| [protocol.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/protocol.c) | MIGRATE | `studio/collaboration`: Message schema validation and room protocol encoding/decoding | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [workspace.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/workspace.c) | MIGRATE | `studio/collaboration`: Workspace checkpoint composition and candidate restore orchestration | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |

#### src/editor/documents (17)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [asset_code.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/asset_code.c) | SPLIT | `studio/documents + sdk/authoring`: Source buffer/editor policy moves; generic parse/compile/semantic index operations remain SDK services | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [commands.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/commands.c) | MIGRATE | `studio/documents`: Document command semantics and history integration | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [component_properties.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/component_properties.c) | MIGRATE | `studio/documents`: Reflected property presentation and edits via typed SDK field access | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [document.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/document.c) | MIGRATE | `studio/documents`: Document lifecycle and state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [mutations.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/mutations.c) | MIGRATE | `studio/documents`: Document mutation coordination | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [navigation.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/navigation.c) | MIGRATE | `studio/documents`: Document navigation state and history | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [preview.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/preview.c) | SPLIT | `studio/documents + sdk/authoring`: Preview selection and lifecycle policy moves; engine preview/world handles remain native SDK resources | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_disk.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_disk.c) | SPLIT | `studio/documents + sdk/authoring`: Dirty/reload/conflict/save policy moves; filesystem and document parse/serialize are generic host/SDK mechanisms | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_document.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_document.c) | MIGRATE | `studio/documents`: Scene document editing state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_documents.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_documents.c) | MIGRATE | `studio/documents`: Multiple scene document ownership | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_history.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_history.c) | MIGRATE | `studio/documents`: Undo/redo transaction histories and snapshot policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_prototype_actions.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_prototype_actions.c) | MIGRATE | `studio/documents`: Prototype command semantics in scene documents | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_prototypes.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_prototypes.c) | MIGRATE | `studio/documents`: Scene/prototype document coordination | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_retention.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_retention.c) | MIGRATE | `studio/documents`: Retention and disposal policy for document state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [ui_buffer.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/ui_buffer.c) | MIGRATE | `studio/documents`: UI source buffer state and revisions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [ui_code.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/ui_code.c) | SPLIT | `studio/documents + sdk/authoring`: UI source editing/apply policy moves; generic UI parse/load operations remain SDK mechanisms | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [ui_design.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/ui_design.c) | SPLIT | `studio/documents + sdk/authoring`: Designer editing model moves; RmlUi DOM/layout access is a typed host capability | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |

#### src/editor/graph (8)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [eval_view.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/eval_view.c) | MIGRATE | `studio/graph`: Graph evaluation readout projection from runtime inspection snapshots | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [flow.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/flow.c) | MIGRATE | `studio/graph`: Graph editor flow/document coordination, using existing SDK compilation/evaluation services | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [flow_visual.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/flow_visual.c) | MIGRATE | `studio/graph`: Visual flow state and style projection | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [input_bindings.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/input_bindings.c) | MIGRATE | `studio/graph`: Graph input binding presentation and edit semantics | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [pins.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/pins.c) | MIGRATE | `studio/graph`: Graph pin presentation and interaction semantics | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [presentation.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/presentation.c) | MIGRATE | `studio/graph`: Graph presentation data | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [search.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/search.c) | MIGRATE | `studio/graph`: Graph search/filter state and result projection | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [selection.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/selection.c) | MIGRATE | `studio/graph`: Graph selection operations and invariants | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |

#### src/editor/rmlui (14)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [kit.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit.c) | SPLIT | `ui/reconcile + host/ui`: Keyed reconciliation, widget state, scheduling and component ownership move; RmlUi context/element lifetime, event capture and batched DOM patches remain generic host operations | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [kit_code.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_code.c) | SPLIT | `ui/code_editor + host/text`: Completion, indentation, commands, diagnostics, history arbitration and text-editor presentation move; Zep C++ bridge, font shaping/metrics and retained mesh submission remain generic native services | SELECTED_BODY_OR_INTERFACE |
| [kit_command.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_command.c) | MIGRATE | `ui/components`: Reusable command palette behavior, selection and filtering | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [kit_controls.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_controls.c) | MIGRATE | `ui/components`: Reusable control composition and presentation contracts | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [kit_drag.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_drag.c) | MIGRATE | `ui/components`: Reusable drag lifecycle, thresholds and payload policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [kit_inspect.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_inspect.c) | MIGRATE | `ui/components`: Inspector overlay composition, readable paths and box-model presentation via layout snapshots | SELECTED_BODY_OR_INTERFACE |
| [kit_list.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_list.c) | MIGRATE | `ui/components`: Virtual list range, overscan, reveal and follow behavior | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [kit_motion.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_motion.c) | MIGRATE | `ui/components`: Presence/layout animation scheduling and policy over SDK motion primitives | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [kit_number.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_number.c) | MIGRATE | `ui/components`: Typed number-field configuration and commit/change semantics over the existing SDK number control | SELECTED_BODY_OR_INTERFACE |
| [kit_popup.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_popup.c) | MIGRATE | `ui/components`: Popup/menu focus, dismissal, submenu and placement policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [kit_slot.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_slot.c) | MIGRATE | `ui/components`: Keyed slots and mount ownership policy using generation-safe DOM handles | SELECTED_BODY_OR_INTERFACE |
| [kit_text.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_text.c) | MIGRATE | `ui/components`: NumberFlow and morph-text composition over existing SDK elements | SELECTED_BODY_OR_INTERFACE |
| [kit_toast.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_toast.c) | MIGRATE | `ui/components`: Toast state, expiry, actions and composition | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [kit_wires.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_wires.c) | SPLIT | `ui/graph_surface + host/render`: Wire/grid/flow visual policy and routing state move; batched geometry allocation/submission and renderer ownership remain native; any hot tessellation kernel requires separate measured justification | SELECTED_BODY_OR_INTERFACE |

#### src/editor/studio (67)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [studio_assets.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_assets.c) | MIGRATE | `studio/assets`: Asset browser query, selection, sorting, row/tile projection and actions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_calls.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_calls.c) | MIGRATE | `studio/navigation`: Call hierarchy tree shaping, folds, selection and navigation over shared SDK indexes | SELECTED_BODY_OR_INTERFACE |
| [studio_canvas.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_canvas.c) | MIGRATE | `studio/canvas`: Canvas preview controls, asset usage navigation, sizing/backdrop and action log | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_code.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_code.c) | MIGRATE | `studio/code`: Source editor headers, tabs, diagnostics, find, apply/save mode and actions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_collab.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_collab.c) | MIGRATE | `studio/collaboration`: Room controls and collaboration presentation | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_commands.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_commands.c) | MIGRATE | `studio/commands`: Command catalog, availability and execution routing | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_debug.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_debug.c) | MIGRATE | `studio/debug`: Debugger UI policy, selected target and inspection projection | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_definition.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_definition.c) | MIGRATE | `studio/navigation`: Definition navigation and context | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_designer.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_designer.c) | MIGRATE | `studio/designer`: Designer panel composition and orchestration | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_designer_details.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_designer_details.c) | MIGRATE | `studio/designer`: Designer property editor composition | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_designer_edit.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_designer_edit.c) | MIGRATE | `studio/designer`: Designer edit transactions and gestures | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_designer_hierarchy.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_designer_hierarchy.c) | MIGRATE | `studio/designer`: UI hierarchy selection, rows and actions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_designer_library.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_designer_library.c) | MIGRATE | `studio/designer`: Designer library browsing and insertion | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_designer_values.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_designer_values.c) | MIGRATE | `studio/designer`: Designer type-aware values and choices | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_disk.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_disk.c) | MIGRATE | `studio/documents`: Disk-change notifications, revert/reload confirmation state and dirty-document conflict policy | SELECTED_BODY_OR_INTERFACE |
| [studio_drop.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_drop.c) | MIGRATE | `studio/assets`: Asset drop policy and dispatch | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_export.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_export.c) | SPLIT | `studio/export + host/jobs`: Export availability, save-first rule, names, progress and result toasts move; process launch/wait/log I/O and reveal-folder remain generic host capabilities | BODY_REVIEWED |
| [studio_find.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_find.c) | MIGRATE | `studio/navigation`: Project search presentation and navigation | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_gallery.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_gallery.c) | MIGRATE | `studio/assets`: Gallery presentation and asset choices | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph.c) | MIGRATE | `studio/graph`: Graph editor panel composition and orchestration | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_comment.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_comment.c) | MIGRATE | `studio/graph`: Graph comment presentation and edits | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_details.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_details.c) | MIGRATE | `studio/graph`: Graph node/member inspector and popovers | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_draw.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_draw.c) | MIGRATE | `studio/graph`: Node/control visual composition and style decisions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_edit.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_edit.c) | MIGRATE | `studio/graph`: Graph edit transaction semantics | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_find.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_find.c) | MIGRATE | `studio/graph`: Graph find controls and result navigation | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_header.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_header.c) | MIGRATE | `studio/graph`: Graph header, menus, targets and command projection | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_input.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_input.c) | MIGRATE | `studio/graph`: Graph input gestures and command mapping | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_layout.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_layout.c) | MIGRATE | `studio/graph`: Graph arrange/layout command policy over reusable layout algorithms | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_literals.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_literals.c) | MIGRATE | `studio/graph`: Graph constant/source field controls and choices | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_live.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_live.c) | MIGRATE | `studio/graph`: Live graph debug projection and highlighted execution state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_menu.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_menu.c) | MIGRATE | `studio/graph`: Graph context-menu data and execution | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_nav.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_nav.c) | MIGRATE | `studio/graph`: Graph breadcrumbs, focus and navigation history | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_pins.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_pins.c) | MIGRATE | `studio/graph`: Graph pin UI and connection interactions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_properties.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_properties.c) | MIGRATE | `studio/graph`: Graph reflected property presentation | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_values.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_values.c) | MIGRATE | `studio/graph`: Graph value editing and compatible source completion | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_graph_view.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_view.c) | MIGRATE | `studio/graph`: Graph viewport/pan/zoom presentation state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_host.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_host.c) | SPLIT | `studio/app + host/input + host/ui`: Input ownership, guest routing, shortcuts, idle deadlines, preferences and frame coordination move; SDL events/window/cursor/IME and backend present remain native generic services | SELECTED_BODY_OR_INTERFACE |
| [studio_inspect.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_inspect.c) | MIGRATE | `studio/inspector`: UI inspector selection, property views and actions through typed DOM inspection snapshots | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_keymap.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_keymap.c) | MIGRATE | `studio/commands`: User keymap binding data and lookup | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_keys.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_keys.c) | MIGRATE | `studio/commands`: Shortcut/chord policy and dispatch | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_member_properties.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_member_properties.c) | MIGRATE | `studio/members`: Member declaration/type editor and choices | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_members.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_members.c) | MIGRATE | `studio/members`: Member list composition and actions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_model.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_model.c) | SPLIT | `studio/model_preview + host/render`: Model editor controls, reference-image options and bake workflow move; engine preview, renderer and browser download are generic host services | SELECTED_BODY_OR_INTERFACE |
| [studio_node_calls.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_node_calls.c) | MIGRATE | `studio/sdk_imports`: Remove Studio-specific native node-call facade; migrate the actual command/query implementations, retaining only generated imports of capability-level SDK operations | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_outliner.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_outliner.c) | MIGRATE | `studio/scene`: Outliner state, filtered/virtualized rows, selection/fold/reveal and reparent actions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_output.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_output.c) | MIGRATE | `studio/output`: Output records, diagnostics/calls views, filters and navigation | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_palette.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_palette.c) | MIGRATE | `studio/palette`: Palette search state, matching and actions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_palette_view.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_palette_view.c) | MIGRATE | `studio/palette`: Palette result model and composition | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_panels.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_panels.c) | MIGRATE | `studio/shell`: Panel-name/type mapping and registry | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_performance.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_performance.c) | MIGRATE | `studio/performance`: Performance sample projection, graph windows and labels using host timing samples | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_pick.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_pick.c) | MIGRATE | `studio/scene`: Picker/selection policy over generic spatial queries | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_prefs.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_prefs.c) | SPLIT | `studio/preferences + host/persistence`: Preference schema, defaults, migrations and workspace layouts move; native user-file/localStorage/reduced-motion access remain generic platform ports | SELECTED_BODY_OR_INTERFACE |
| [studio_projects.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_projects.c) | SPLIT | `studio/projects + host/files`: Project chooser/new/open workflows move; filesystem chooser and generic SDK project preparation remain native services | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_properties.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_properties.c) | MIGRATE | `studio/properties`: Property panel selection and edit orchestration | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_properties_view.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_properties_view.c) | MIGRATE | `studio/properties`: Property rows, controls and values projection | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_prototype_actions.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_prototype_actions.c) | MIGRATE | `studio/scene`: Prototype actions and command routing | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_refactor.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_refactor.c) | MIGRATE | `studio/navigation`: Refactor command UI and approval/apply orchestration over shared SDK refactor services | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_scene.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_scene.c) | MIGRATE | `studio/scene`: Scene header, zoom/grid options and view overlay projection | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_scene_camera.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_scene_camera.c) | MIGRATE | `studio/scene`: Scene camera controls and authored view options | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_settings.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_settings.c) | MIGRATE | `studio/preferences`: Settings definitions, control types and preference update semantics | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_shell.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_shell.c) | MIGRATE | `studio/shell`: Shell/area/workspace composition and actions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_sources.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_sources.c) | MIGRATE | `studio/panels/* + studio/store`: Split monolithic JSON panel-source builders/actions into typed module-local selectors and command handlers; no replacement global source switchboard | SELECTED_BODY_OR_INTERFACE |
| [studio_thumbnail_cache.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_thumbnail_cache.c) | SPLIT | `studio/thumbnails + host/files + host/render`: Content-key cache metadata, LRU/eviction and failure policy move; binary file operations, PNG codec, texture upload and browser persistence remain generic mechanisms | BODY_REVIEWED |
| [studio_thumbnail_graph.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_thumbnail_graph.c) | MIGRATE | `studio/thumbnails`: Thumbnail graph state/dependency projection and content selection | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_thumbnail_kinds.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_thumbnail_kinds.c) | MIGRATE | `studio/thumbnails`: Per-asset thumbnail recipes and visual choices | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_thumbnails.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_thumbnails.c) | SPLIT | `studio/thumbnails + host/render`: Thumbnail priority/invalidation/cache scheduling moves; render targets, image codecs, GPU readback and job completion remain native SDK services | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [studio_ui.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_ui.c) | SPLIT | `studio/app + ui/runtime`: Authored UI orchestration, source subscriptions, activation routing and refresh policy move; generic UI loader/world/DOM backend lifetime remains SDK infrastructure | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |

#### src/editor/text (1)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [zep_bridge.cpp](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/text/zep_bridge.cpp) | NATIVE_EXCEPTION | `host/text/zep`: Existing C++-API pass-through for Zep. Retain conversion/forwarding only; editor shortcuts, indentation, completion, diagnostics, focus and undo policy migrate from kit_code.c | AGENTS_AND_HEADER_CONTRACT_ONLY |

#### src/editor/ui (4)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [choices.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/ui/choices.c) | MIGRATE | `ui/presentation`: Reusable choice collection/filtering presentation | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [highlight.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/ui/highlight.c) | MIGRATE | `ui/presentation`: Syntax highlight presentation/markup over shared tokenization semantics | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [kit_wire_shape.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/ui/kit_wire_shape.c) | MIGRATE | `ui/presentation`: Wire shape/routing geometry algorithm; no blanket native exemption for pure math | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [labels.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/ui/labels.c) | MIGRATE | `ui/presentation`: Reflected labels, icons and user-facing naming | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |

#### src/editor/workspace (31)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [application.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/application.c) | SPLIT | `studio/app + sdk/runtime`: Application/workspace candidate construction, recovery, room switching and scheduling policy move; platform loop and engine/world resource operations remain typed host services | SELECTED_BODY_OR_INTERFACE |
| [asset_index.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/asset_index.c) | MIGRATE | `studio/documents`: Unsaved/open document asset-index overlay and invalidation policy over shared SDK index | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [asset_refactor.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/asset_refactor.c) | MIGRATE | `studio/documents`: Refactor planning/apply orchestration and document updates | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [asset_refactor_source.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/asset_refactor_source.c) | MIGRATE | `studio/documents`: Source refactor coordination over shared semantic SDK operations | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [editor.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/editor.c) | MIGRATE | `studio/documents`: Editor document selection/lifecycle and edit coordination | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [editor_camera.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/editor_camera.c) | MIGRATE | `studio/scene`: Editor camera navigation and view state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [game_presentation.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/game_presentation.c) | SPLIT | `studio/runtime + host/input + host/render`: Game-preview presentation/session policy moves; display/input backend integration stays native generic | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [gameplay_input.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/gameplay_input.c) | MIGRATE | `studio/runtime`: Routing normalized game input according to editor focus/play state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [input.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/input.c) | SPLIT | `studio/input + host/input`: Studio input mapping and state move; SDL event/pointer/device sampling remains a normalized native input port | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [model_preview.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/model_preview.c) | SPLIT | `studio/model_preview + sdk/runtime`: Disposable model fixture composition, view/bake settings and rejection state move; native prototype/engine/renderer/bake services remain generic SDK mechanisms | SELECTED_BODY_OR_INTERFACE |
| [scenario_playback.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scenario_playback.c) | MIGRATE | `studio/runtime`: Scenario playback control and editor follow policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_actors.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_actors.c) | MIGRATE | `studio/documents`: Scene actor authoring state and operations through SDK documents | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_asset_drop.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_asset_drop.c) | MIGRATE | `studio/documents`: Scene asset insertion/drop semantics | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_authoring.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_authoring.c) | MIGRATE | `studio/documents`: Scene authoring document/session coordinator; generic SDK mutations are imports, not a copied C subsystem | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_checkpoint.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_checkpoint.c) | MIGRATE | `studio/documents`: Scene workspace checkpoint schema/composition and restore policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_collider3d.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_collider3d.c) | MIGRATE | `studio/scene`: 3D collider edit controls and gesture policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_debug.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_debug.c) | MIGRATE | `studio/runtime`: Scene debug selection, breakpoints and inspection presentation | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_gizmo.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_gizmo.c) | MIGRATE | `studio/scene`: 2D transform gesture, snapping and undo-group policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_gizmo3d.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_gizmo3d.c) | MIGRATE | `studio/scene`: 3D transform gestures, constraints and snapping | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_gizmo3d_targets.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_gizmo3d_targets.c) | MIGRATE | `studio/scene`: 3D gizmo target selection and transform transactions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_gizmo3d_view.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_gizmo3d_view.c) | MIGRATE | `studio/scene`: 3D gizmo visual model and handle geometry | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_hierarchy.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_hierarchy.c) | MIGRATE | `studio/documents`: Scene hierarchy editing and invariants | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_overlays.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_overlays.c) | MIGRATE | `studio/scene`: Scene overlay selection and visual composition | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_runtime.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_runtime.c) | SPLIT | `studio/runtime + sdk/runtime`: Play/pause/restart/target/keep-changes/spawn-break policies move; ECS world/engine step/snapshot handles remain generic SDK mechanisms | SELECTED_BODY_OR_INTERFACE |
| [scene_tabs.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_tabs.c) | MIGRATE | `studio/documents`: Scene tab lifecycle and dirty/active state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_view.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_view.c) | MIGRATE | `studio/scene`: 2D scene camera/view interaction and display policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_view3d.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_view3d.c) | MIGRATE | `studio/scene`: 3D scene camera/view interaction and display policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_viewport.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_viewport.c) | SPLIT | `studio/scene + host/render`: Scene surface sizing/camera/dirty policy moves; render-target and scene renderer calls remain generic native services | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [scene_world_preview.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_world_preview.c) | SPLIT | `studio/runtime + sdk/runtime`: World-preview selection/lifecycle policy moves; generated-world preparation and engine execution remain native SDK services | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [tree_watch.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/tree_watch.c) | SPLIT | `studio/documents + host/files`: Watch scope, polling cadence, sorting/diff and self-write suppression move; directory enumeration/stat/watch primitives remain native, preserving no-symlink-follow behavior | BODY_REVIEWED |
| [viewport.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/viewport.c) | SPLIT | `studio/runtime + host/render + host/input`: Viewport input/presentation policy moves; texture handles, backend draw and native RmlUi/game-UI interop remain generic adapters | SELECTED_BODY_OR_INTERFACE |

#### src/host (3)

| Source | Disposition | Target and responsibility | Evidence |
|---|---|---|---|
| [main.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/host/main.c) | SPLIT | `studio/bootstrap + host/platform`: CLI/startup/checkpoint/recovery intent moves; executable entry, OS window, signal, event-loop and Emscripten bootstrap remain native | BODY_REVIEWED |
| [stacktrace.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/host/stacktrace.c) | NATIVE_EXCEPTION | `host/diagnostics`: Crash signal handling, symbolization and debugger trap must operate when language runtime is failing; no Studio policy belongs here | BODY_REVIEWED |
| [state_file.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/host/state_file.c) | NATIVE_EXCEPTION | `host/files`: Generic file byte read and temp-write/flush/fsync/rename durability mechanism; contains no editor workflow. Consolidate with SDK file service, expose status/bytes not FILE pointers | BODY_REVIEWED |

### Product headers (all 62)

Internal headers are not an excuse to preserve C ownership. Their types and contracts move to module exports; only the reduced host ABI retains C declarations.

| Header | Disposition | Target / boundary | Evidence |
|---|---|---|---|
| [include/chibi/application.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/include/chibi/application.h) | SPLIT | `host/platform + studio/app`; Reduce public application lifecycle ABI to generic host/runtime entry contract; application behavior lives in .chibi | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/diagnostics/frame_log.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/diagnostics/frame_log.h) | SPLIT | `studio/diagnostics + host/diagnostics`; Replace policy types/API with module exports; Frame sample ring and CSV projection move; timing collection and browser export entry are generic host/generated ABI mechanisms | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/diagnostics/tracy.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/diagnostics/tracy.h) | NATIVE_EXCEPTION | `host/diagnostics`; Native ABI/vendor diagnostic declarations retained; not editor state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/diagnostics/web_trace.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/diagnostics/web_trace.h) | NATIVE_EXCEPTION | `host/diagnostics`; Replace policy types/API with module exports; Browser performance clock/User Timing ABI bridge; keep generic, with a shared time origin | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/collab/actions.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/actions.h) | MIGRATE | `studio/collaboration`; Replace policy types/API with module exports; Editor action dispatch and mutations through the common command transaction path | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/collab/actions_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/actions_internal.h) | MIGRATE | `studio/collaboration`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/collab/actions_scene.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/actions_scene.h) | MIGRATE | `studio/collaboration`; Replace policy types/API with module exports; Scene collaboration action interpretation and application | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/collab/collab.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/collab.h) | SPLIT | `studio/collaboration + host/transport`; Replace policy types/API with module exports; Room state machine, ordering, replay suppression, queue limits, presence and recovery move; browser send/control/close transport stays behind a bounded host port | BODY_REVIEWED |
| [src/editor/collab/protocol.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/protocol.h) | MIGRATE | `studio/collaboration`; Replace policy types/API with module exports; Message schema validation and room protocol encoding/decoding | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/collab/workspace.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/collab/workspace.h) | MIGRATE | `studio/collaboration`; Replace policy types/API with module exports; Workspace checkpoint composition and candidate restore orchestration | BODY_REVIEWED |
| [src/editor/documents/asset_code.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/asset_code.h) | SPLIT | `studio/documents + sdk/authoring`; Replace policy types/API with module exports; Source buffer/editor policy moves; generic parse/compile/semantic index operations remain SDK services | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/documents/component_properties.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/component_properties.h) | MIGRATE | `studio/documents`; Replace policy types/API with module exports; Reflected property presentation and edits via typed SDK field access | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/documents/scene_history.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_history.h) | MIGRATE | `studio/documents`; Replace policy types/API with module exports; Undo/redo transaction histories and snapshot policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/documents/scene_retention.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/scene_retention.h) | MIGRATE | `studio/documents`; Replace policy types/API with module exports; Retention and disposal policy for document state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/documents/ui_buffer.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/ui_buffer.h) | MIGRATE | `studio/documents`; Replace policy types/API with module exports; UI source buffer state and revisions | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/documents/ui_code.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/ui_code.h) | SPLIT | `studio/documents + sdk/authoring`; Replace policy types/API with module exports; UI source editing/apply policy moves; generic UI parse/load operations remain SDK mechanisms | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/documents/ui_design.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/documents/ui_design.h) | SPLIT | `studio/documents + sdk/authoring`; Replace policy types/API with module exports; Designer editing model moves; RmlUi DOM/layout access is a typed host capability | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/graph/eval_view.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/eval_view.h) | MIGRATE | `studio/graph`; Replace policy types/API with module exports; Graph evaluation readout projection from runtime inspection snapshots | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/graph/flow_visual.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/flow_visual.h) | MIGRATE | `studio/graph`; Replace policy types/API with module exports; Visual flow state and style projection | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/graph/search.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/graph/search.h) | MIGRATE | `studio/graph`; Replace policy types/API with module exports; Graph search/filter state and result projection | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/rmlui/kit_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/rmlui/kit_internal.h) | SPLIT | `ui/reconcile + host/ui`; Split keyed component state from raw RmlUi ownership; prohibit exposing the old internal struct wholesale | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/studio/studio.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio.h) | MIGRATE | `studio/panels/*`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/studio/studio_designer_edit.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_designer_edit.h) | MIGRATE | `studio/designer`; Replace policy types/API with module exports; Designer edit transactions and gestures | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/studio/studio_designer_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_designer_internal.h) | MIGRATE | `studio/panels/*`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/studio/studio_graph_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_graph_internal.h) | MIGRATE | `studio/panels/*`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/studio/studio_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_internal.h) | MIGRATE | `studio/panels/*`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | SELECTED_BODY_OR_INTERFACE |
| [src/editor/studio/studio_members_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_members_internal.h) | MIGRATE | `studio/panels/*`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/studio/studio_nodes.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_nodes.h) | MIGRATE | `studio/panels/*`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/studio/studio_properties_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_properties_internal.h) | MIGRATE | `studio/panels/*`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/studio/studio_thumbnails.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/studio/studio_thumbnails.h) | SPLIT | `studio/thumbnails + host/render`; Replace policy types/API with module exports; Thumbnail priority/invalidation/cache scheduling moves; render targets, image codecs, GPU readback and job completion remain native SDK services | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/text/zep_bridge.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/text/zep_bridge.h) | NATIVE_EXCEPTION | `host/text/zep`; Replace policy types/API with module exports; Existing C++-API pass-through for Zep. Retain conversion/forwarding only; editor shortcuts, indentation, completion, diagnostics, focus and undo policy migrate from kit_code.c | BODY_REVIEWED |
| [src/editor/ui/choices.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/ui/choices.h) | MIGRATE | `ui/presentation`; Replace policy types/API with module exports; Reusable choice collection/filtering presentation | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/ui/highlight.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/ui/highlight.h) | MIGRATE | `ui/presentation`; Replace policy types/API with module exports; Syntax highlight presentation/markup over shared tokenization semantics | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/ui/kit.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/ui/kit.h) | SPLIT | `ui/components + host/ui`; Component descriptors, state and events become typed .chibi exports; only opaque backend resource ABI remains native | SELECTED_BODY_OR_INTERFACE |
| [src/editor/ui/kit_icons.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/ui/kit_icons.h) | MIGRATE | `ui/presentation`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/ui/labels.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/ui/labels.h) | MIGRATE | `ui/presentation`; Replace policy types/API with module exports; Reflected labels, icons and user-facing naming | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/asset_index.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/asset_index.h) | MIGRATE | `studio/documents`; Replace policy types/API with module exports; Unsaved/open document asset-index overlay and invalidation policy over shared SDK index | BODY_REVIEWED |
| [src/editor/workspace/asset_refactor.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/asset_refactor.h) | MIGRATE | `studio/documents`; Replace policy types/API with module exports; Refactor planning/apply orchestration and document updates | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/asset_refactor_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/asset_refactor_internal.h) | MIGRATE | `studio/documents + studio/runtime`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/checkpoint.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/checkpoint.h) | MIGRATE | `studio/documents + studio/runtime`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/editor.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/editor.h) | MIGRATE | `studio/documents`; Replace policy types/API with module exports; Editor document selection/lifecycle and edit coordination | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/editor_camera.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/editor_camera.h) | MIGRATE | `studio/scene`; Replace policy types/API with module exports; Editor camera navigation and view state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/editor_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/editor_internal.h) | MIGRATE | `studio/documents + studio/runtime`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/game_presentation.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/game_presentation.h) | SPLIT | `studio/runtime + host/input + host/render`; Replace policy types/API with module exports; Game-preview presentation/session policy moves; display/input backend integration stays native generic | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/gameplay_input.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/gameplay_input.h) | MIGRATE | `studio/runtime`; Replace policy types/API with module exports; Routing normalized game input according to editor focus/play state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/input.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/input.h) | SPLIT | `studio/input + host/input`; Replace policy types/API with module exports; Studio input mapping and state move; SDL event/pointer/device sampling remains a normalized native input port | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/model_preview.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/model_preview.h) | SPLIT | `studio/model_preview + sdk/runtime`; Replace policy types/API with module exports; Disposable model fixture composition, view/bake settings and rejection state move; native prototype/engine/renderer/bake services remain generic SDK mechanisms | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/movement_input.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/movement_input.h) | MIGRATE | `studio/documents + studio/runtime`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/scenario_playback.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scenario_playback.h) | MIGRATE | `studio/runtime`; Replace policy types/API with module exports; Scenario playback control and editor follow policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/scene_authoring.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_authoring.h) | MIGRATE | `studio/documents`; Replace policy types/API with module exports; Scene authoring document/session coordinator; generic SDK mutations are imports, not a copied C subsystem | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/scene_collider3d.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_collider3d.h) | MIGRATE | `studio/scene`; Replace policy types/API with module exports; 3D collider edit controls and gesture policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/scene_debug.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_debug.h) | MIGRATE | `studio/runtime`; Replace policy types/API with module exports; Scene debug selection, breakpoints and inspection presentation | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/scene_gizmo.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_gizmo.h) | MIGRATE | `studio/scene`; Replace policy types/API with module exports; 2D transform gesture, snapping and undo-group policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/scene_gizmo3d_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_gizmo3d_internal.h) | MIGRATE | `studio/documents + studio/runtime`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/scene_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_internal.h) | MIGRATE | `studio/documents + studio/runtime`; Migrate module types/enums/constants and public contracts; remove private cross-module struct access and replace with explicit typed exports | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/scene_view3d.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/scene_view3d.h) | MIGRATE | `studio/scene`; Replace policy types/API with module exports; 3D scene camera/view interaction and display policy | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/tree_watch.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/tree_watch.h) | SPLIT | `studio/documents + host/files`; Replace policy types/API with module exports; Watch scope, polling cadence, sorting/diff and self-write suppression move; directory enumeration/stat/watch primitives remain native, preserving no-symlink-follow behavior | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/editor/workspace/viewport.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/editor/workspace/viewport.h) | SPLIT | `studio/runtime + host/render + host/input`; Replace policy types/API with module exports; Viewport input/presentation policy moves; texture handles, backend draw and native RmlUi/game-UI interop remain generic adapters | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/host/stacktrace.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/host/stacktrace.h) | NATIVE_EXCEPTION | `host/diagnostics`; Replace policy types/API with module exports; Crash signal handling, symbolization and debugger trap must operate when language runtime is failing; no Studio policy belongs here | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/host/state_file.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/host/state_file.h) | NATIVE_EXCEPTION | `host/files`; Replace policy types/API with module exports; Generic file byte read and temp-write/flush/fsync/rename durability mechanism; contains no editor workflow. Consolidate with SDK file service, expose status/bytes not FILE pointers | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/host/vendor/debugbreak.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/host/vendor/debugbreak.h) | NATIVE_EXCEPTION | `host/diagnostics`; Native ABI/vendor diagnostic declarations retained; not editor state | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |
| [src/studio_registry.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/src/studio_registry.h) | SPLIT | `studio/bootstrap + sdk/catalog`; Replace policy types/API with module exports; Studio module/capability composition becomes authored/generated; generic native catalog construction/type identity and registration remain SDK mechanisms | TREE_AND_BUILD_OR_MODULE_CONTEXT_ONLY |

### Build, dependency and browser boundaries

| Source | Classification | Required treatment |
|---|---|---|
| [.github/workflows/ci.yml](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/.github/workflows/ci.yml) | RETAIN_BUILD_TOOLING | Retain build/pin/packaging/tooling role; replace Studio C source list with compiled .chibi modules plus explicit native allowlist. Do not add private engine include roots |
| [.github/workflows/publish.yml](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/.github/workflows/publish.yml) | RETAIN_BUILD_TOOLING | Retain build/pin/packaging/tooling role; replace Studio C source list with compiled .chibi modules plus explicit native allowlist. Do not add private engine include roots |
| [CMakeLists.txt](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/CMakeLists.txt) | RETAIN_BUILD_TOOLING | Retain build/pin/packaging/tooling role; replace Studio C source list with compiled .chibi modules plus explicit native allowlist. Do not add private engine include roots |
| [EXTRACTION.json](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/EXTRACTION.json) | RETAIN_UPDATE_METADATA | Preserve provenance and public SDK boundary; update removed/migrated consumers according to repository rules |
| [Makefile](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/Makefile) | RETAIN_BUILD_TOOLING | Retain build/pin/packaging/tooling role; replace Studio C source list with compiled .chibi modules plus explicit native allowlist. Do not add private engine include roots |
| [SDK_HEADERS.json](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/SDK_HEADERS.json) | RETAIN_UPDATE_METADATA | Preserve provenance and public SDK boundary; update removed/migrated consumers according to repository rules |
| [cmake/apply_patch.cmake](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/apply_patch.cmake) | RETAIN_BUILD_TOOLING | Retain build/pin/packaging/tooling role; replace Studio C source list with compiled .chibi modules plus explicit native allowlist. Do not add private engine include roots |
| [cmake/patches/zep-change-inner-word.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-change-inner-word.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/patches/zep-cursor-info.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-cursor-info.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/patches/zep-flat-underline.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-flat-underline.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/patches/zep-host-view.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-host-view.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/patches/zep-indicator-dot.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-indicator-dot.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/patches/zep-integer-types.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-integer-types.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/patches/zep-marker-self-clear.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-marker-self-clear.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/patches/zep-select-range.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-select-range.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/patches/zep-undo-pop.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-undo-pop.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/patches/zep-wheel-scroll.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-wheel-scroll.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/patches/zep-wrapped-line-numbers.patch](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/patches/zep-wrapped-line-numbers.patch) | RETAIN_BUILD_TOOLING | Retain pinned Zep dependency patch while preserving text parity; each removal requires upstream-equivalence evidence |
| [cmake/studio_sources.cmake](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/studio_sources.cmake) | RETAIN_BUILD_TOOLING | Retain build/pin/packaging/tooling role; replace Studio C source list with compiled .chibi modules plus explicit native allowlist. Do not add private engine include roots |
| [cmake/studio_web.cmake](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/studio_web.cmake) | RETAIN_BUILD_TOOLING | Retain build/pin/packaging/tooling role; replace Studio C source list with compiled .chibi modules plus explicit native allowlist. Do not add private engine include roots |
| [cmake/zep_dependencies.cmake](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/cmake/zep_dependencies.cmake) | RETAIN_BUILD_TOOLING | Retain build/pin/packaging/tooling role; replace Studio C source list with compiled .chibi modules plus explicit native allowlist. Do not add private engine include roots |
| [tools/serve_web.py](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tools/serve_web.py) | RETAIN_BUILD_TOOLING | Development HTTP serving tool only; no product-policy migration to .chibi required |
| [tools/sync_sources.py](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tools/sync_sources.py) | RETAIN_BUILD_TOOLING | Extraction/source provenance helper only; do not let it reintroduce removed C product implementations |
| [web/collab/client.js](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/web/collab/client.js) | SPLIT_WEB_HOST | Retain browser-only boot/storage/network/download/isolation mechanics; extract any application lifecycle, recovery or room policy into .chibi. Exact split requires body audit |
| [web/collab/config.js.in](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/web/collab/config.js.in) | SPLIT_WEB_HOST | Retain browser-only boot/storage/network/download/isolation mechanics; extract any application lifecycle, recovery or room policy into .chibi. Exact split requires body audit |
| [web/collab/storage.js](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/web/collab/storage.js) | SPLIT_WEB_HOST | Retain browser-only boot/storage/network/download/isolation mechanics; extract any application lifecycle, recovery or room policy into .chibi. Exact split requires body audit |
| [web/web_cache.js](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/web/web_cache.js) | SPLIT_WEB_HOST | Retain browser-only boot/storage/network/download/isolation mechanics; extract any application lifecycle, recovery or room policy into .chibi. Exact split requires body audit |
| [web/web_default.js](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/web/web_default.js) | SPLIT_WEB_HOST | Retain browser-only boot/storage/network/download/isolation mechanics; extract any application lifecycle, recovery or room policy into .chibi. Exact split requires body audit |
| [web/web_frames.js](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/web/web_frames.js) | SPLIT_WEB_HOST | Retain browser-only boot/storage/network/download/isolation mechanics; extract any application lifecycle, recovery or room policy into .chibi. Exact split requires body audit |
| [web/web_isolation.js](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/web/web_isolation.js) | SPLIT_WEB_HOST | Retain browser-only boot/storage/network/download/isolation mechanics; extract any application lifecycle, recovery or room policy into .chibi. Exact split requires body audit |
| [web/web_launch.js](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/web/web_launch.js) | SPLIT_WEB_HOST | Retain browser-only boot/storage/network/download/isolation mechanics; extract any application lifecycle, recovery or room policy into .chibi. Exact split requires body audit |
| [web/web_shell.html](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/web/web_shell.html) | SPLIT_WEB_HOST | Retain browser-only boot/storage/network/download/isolation mechanics; extract any application lifecycle, recovery or room policy into .chibi. Exact split requires body audit |
| [web/web_storage.js](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/web/web_storage.js) | SPLIT_WEB_HOST | Retain browser-only boot/storage/network/download/isolation mechanics; extract any application lifecycle, recovery or room policy into .chibi. Exact split requires body audit |

### Native test and test-header inventory (all 99)

Keep the C/pico_unit harness as required by the engine rules. Rewrite/admit individual oracle cases only when they target supported public contracts. The four acceptance C units are optional native hidden-renderer integration, not broad interaction/performance proof. The held source list includes historical snapshots; preserve provenance rather than silently counting them as current duplicate coverage. No test ran for this planning audit.

| Source | Boundary |
|---|---|
| [tests/acceptance/application.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/application.c) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/debugger.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/debugger.c) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/debugger.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/debugger.h) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/host.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/host.c) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/main.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/main.c) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/outliner_move.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/outliner_move.h) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/panel_idle.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/panel_idle.h) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/scene_3d.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/scene_3d.h) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/scene_camera.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/scene_camera.h) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/scene_collider3d.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/scene_collider3d.h) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/scene_gizmo3d.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/scene_gizmo3d.h) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/scene_move.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/scene_move.h) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/acceptance/vendor/pico_unit.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/acceptance/vendor/pico_unit.h) | tests/acceptance C dispatcher; Opt-in studio-acceptance target for four C units; headers/fixtures supporting it |
| [tests/held/engine_oracles/prior_snapshots/ebc60a87f99d1cbe035d483c900e5735cb9e38379c23b9a50c0be3356efa2f92/test/movement_trail.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/prior_snapshots/ebc60a87f99d1cbe035d483c900e5735cb9e38379c23b9a50c0be3356efa2f92/test/movement_trail.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/appearance.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/appearance.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/arena_input.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/arena_input.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/arena_play.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/arena_play.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/asset_fixture.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/asset_fixture.h) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/collab_protocol.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/collab_protocol.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/collab_replay.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/collab_replay.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/asset_code.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/asset_code.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/code_intelligence.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/code_intelligence.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/code_intelligence_studio.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/code_intelligence_studio.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/commands.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/commands.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/debug_target.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/debug_target.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/iframe_bindings.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/iframe_bindings.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/kit_code.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/kit_code.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/kit_documents.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/kit_documents.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/kit_empty_documents.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/kit_empty_documents.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/kit_host.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/kit_host.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/kit_slot.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/kit_slot.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/rmlui_kit.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/rmlui_kit.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/rmlui_render.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/rmlui_render.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/scene_gizmo.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/scene_gizmo.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/scene_selection.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/scene_selection.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/scene_view.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/scene_view.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_commands.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_commands.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents.h) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_assets.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_assets.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_chrome.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_chrome.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_designer.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_designer.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_graph.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_graph.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_graph_chrome.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_graph_chrome.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_inspector.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_inspector.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_members.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_members.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_outliner.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_outliner.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_output.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_output.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_palette.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_palette.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_panels.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_panels.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_properties.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_properties.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_scene.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_scene.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_documents_shell.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_documents_shell.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_graph_inputs.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_graph_inputs.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_graph_zoom.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_graph_zoom.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_models.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_models.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_new_assets.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_new_assets.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_nodes_gate.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_nodes_gate.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_pinch.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_pinch.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_settings.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_settings.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/studio_thumbnails.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/studio_thumbnails.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/ui_bench.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/ui_bench.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/ui_bench.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/ui_bench.h) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/ui_bench_studio.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/ui_bench_studio.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/ui_designer.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/ui_designer.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/ui_documents.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/ui_documents.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/ui_inspector.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/ui_inspector.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/ui_popovers.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/ui_popovers.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/ui_sources.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/ui_sources.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor/ui_wiring.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor/ui_wiring.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor_checkpoint.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor_checkpoint.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor_documents.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor_documents.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/editor_flow.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/editor_flow.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/graph/document.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/graph/document.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/graph/editing.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/graph/editing.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/graph/input_bindings.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/graph/input_bindings.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/graph/optimizer.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/graph/optimizer.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/graph/optimizer_sources.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/graph/optimizer_sources.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/graph_navigation.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/graph_navigation.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/labels.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/labels.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/math_expression.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/math_expression.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/movement_input.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/movement_input.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/movement_trail.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/movement_trail.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/physics_executor.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/physics_executor.h) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/runtime/catalog.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/runtime/catalog.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/runtime/game_capabilities.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/runtime/game_capabilities.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/runtime/ui_source.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/runtime/ui_source.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/scene_checkpoint.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/scene_checkpoint.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/scene_debug.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/scene_debug.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/scene_disk.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/scene_disk.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/scene_history.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/scene_history.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/scene_instances.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/scene_instances.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/scene_refactor.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/scene_refactor.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/sdf_material.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/sdf_material.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/suite_internal.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/suite_internal.h) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/support/code_fixture.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/support/code_fixture.h) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/support/event_fixture.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/support/event_fixture.h) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/support/physics_executor.c](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/support/physics_executor.c) | tests/held oracles; No executable target; held/quarantined until individually admitted |
| [tests/held/engine_oracles/test/support/simulation_fixture.h](https://github.com/waldnercharles/chibi-studio/blob/78cb4d4c47242db9f7af2b9573653d9756050e99/tests/held/engine_oracles/test/support/simulation_fixture.h) | tests/held oracles; No executable target; held/quarantined until individually admitted |

### Completion/accounting gate

For each implementation batch, replace every provisional row touched with a reviewed function split, declared owner, native justification (if any), migration issue/PR and parity evidence. Compare the built source manifest against the allowlist, inspect compiled imports for whole-application call-throughs, and account for all old units as deleted/migrated or explicitly retained generic adapters. Count of `.chibi` files or a wrapper calling `studio_frame` is not completion. Preserve the installed-SDK consumer boundary throughout. Build/test policy and exact-head review gates belong to the implementation plan, not to this static inventory.

## Appendix B. Branch-discovery snapshot

Observed through the connected GitHub repository on 15 October 2026. Branch names are not evidence that a branch contains a migration; compare its commits and file changes. The 11 listed Studio heads and 65 available PR metadata records did not identify a newer full C-to-`.chibi` branch. The historical engine lines are already ancestors of the audited main. An unpublished/local branch remains possible.

### Studio branch heads

| Branch | Exact observed head |
|---|---|
| `claude/project-thread-inhgdv-20` | [`e3ece769c7bbf20bfee2efe9af5b88e57afb283c`](https://github.com/waldnercharles/chibi-studio/commit/e3ece769c7bbf20bfee2efe9af5b88e57afb283c) |
| `claude/project-thread-n3squ5-units` | [`2c008cd957458cd6169b132751a3a725b605b58d`](https://github.com/waldnercharles/chibi-studio/commit/2c008cd957458cd6169b132751a3a725b605b58d) |
| `claude/project-thread-n3squ5` | [`807ed64a176c760287e09912445a5ac1e028ddad`](https://github.com/waldnercharles/chibi-studio/commit/807ed64a176c760287e09912445a5ac1e028ddad) |
| `claude/project-thread-sfxom4` | [`176b79347ecbf606056d74ff026cdbf6579d9a41`](https://github.com/waldnercharles/chibi-studio/commit/176b79347ecbf606056d74ff026cdbf6579d9a41) |
| `claude/project-thread-x5ac39-2` | [`e979478570ca8de301e8e843049c4e99a03228a3`](https://github.com/waldnercharles/chibi-studio/commit/e979478570ca8de301e8e843049c4e99a03228a3) |
| `cwaldner/chibi-review-signal-trusted` | [`74e5539a46b7ceca832352c2c06927b43d518b62`](https://github.com/waldnercharles/chibi-studio/commit/74e5539a46b7ceca832352c2c06927b43d518b62) |
| `cwaldner/chibi-review-status-ascii` | [`3861c219f355a80e575ded6d09bbe67b9a95604d`](https://github.com/waldnercharles/chibi-studio/commit/3861c219f355a80e575ded6d09bbe67b9a95604d) |
| `cwaldner/pinned-editor-88c5dac` | [`de027ec8d23d471c42c3cc22210375ffd4aeca2e`](https://github.com/waldnercharles/chibi-studio/commit/de027ec8d23d471c42c3cc22210375ffd4aeca2e) |
| `cwaldner/seven-option-menus` | [`c5fd08aa961e223b892f735c5a1f6eb193ea4ad9`](https://github.com/waldnercharles/chibi-studio/commit/c5fd08aa961e223b892f735c5a1f6eb193ea4ad9) |
| `cwaldner/stillwater-pinned-editor` | [`0ad2e5a1fea7d38e4aba19d9d04074ba5e0558d5`](https://github.com/waldnercharles/chibi-studio/commit/0ad2e5a1fea7d38e4aba19d9d04074ba5e0558d5) |
| `main` | [`78cb4d4c47242db9f7af2b9573653d9756050e99`](https://github.com/waldnercharles/chibi-studio/commit/78cb4d4c47242db9f7af2b9573653d9756050e99) |

### Likely historical migration lines

- Engine `cwaldner/editor-polish-design-4679ad`, [PR #147](https://github.com/waldnercharles/chibi/pull/147), head `e49b5fcc6e32c71d9a58eff0055bb8468166ac81`: merged, 1,244 behind and zero unique commits relative to the engine baseline.
- Engine `codex/cut-58-migration-audit`, [PR #169](https://github.com/waldnercharles/chibi/pull/169), head `a19b1ff4ff3c6b7c9814379633d9025887a7a08c`: merged, 1,187 behind and zero unique commits.
- Engine `cwaldner/imgui-rmlui-migration-feebd2`, head `0ef72c5bd22dfd2d74fd2a074409d92371b772ff`: ancestor, 2,059 behind and zero unique commits.

Read-only ancestry comparisons establish inclusion, not current product test results. Preserve these refs; a new implementation branch should be created only after plan approval and refreshed baseline checks.
