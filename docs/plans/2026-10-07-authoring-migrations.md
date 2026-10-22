Summary: Implement the corrected CUT-70/CUT-71 authoring requirements agreed on 2026-10-06. the former document format defines declarative assets; readable .code is the complete executable source for behaviors, callable libraries and systems. Studio text and graph views share that source and the existing native/WASM runtime backends. LuaJIT is out of scope.

> Historical record: this document describes an earlier source format and toolchain.
> Use the current language and authoring guides for executable instructions.
> Suffixes in prose follow current conventions; linked Git revisions retain the exact historical files.

Context: Fresh main is 0e515e5453ea81a762c7885fbb2ec9481ad0b9bf, project version 5. asset_text owns the earlier document syntax over av_value; graph serialization already constructs models from values. The JavaScript behavior bridge is separate from native loading. Studio has source drafts, Zep standard/Vim editing, graph history, diagnostics and live generations, but unsaved graph/text changes are not synchronized.

System impact: Restore project.chibi and the former document format for scenes, prototypes, event definitions, materials, shaders, fields, shapes, models and scenarios. Project version 7 records the changed contract. Executable source owns state, calls, events, functions, control flow and suspension. Graphs are derived compiler/editor models. Optional identity/layout metadata cannot supply executable values, operations or connections. Invalid source preserves the last valid document and runtime.

Approach:
- Complete the shared C behavior compiler and readable graph-to-source printer through existing graph/expression APIs. Cover every supported behavior graph operation without explicit graph records or a serialization fallback. Prefer structured statements; labels are available for control flow that cannot be nested without changing execution.
- Retain drafts/comments, reconcile visual edits through ranges, preserve unambiguous identity, and validate before replacing models.
- Restore declarative values in the former format from current values, preserving authored changes and migrating domain-specific references. Rewrite all executable corpus assets into readable source and verify semantics independently of optional metadata. Retain the former document format dependencies; keep the obsolete JavaScript compiler and required sidecars removed.
- Extend editor/controller language services, simultaneous views, shared undo, navigation, diagnostics, debugging and safe generation/state replacement.

Changes:
- src/support/asset_document.* and retired-parser build integration: the former document format parsing/writing. code_lex remains the behavior lexer.
- src/assets/code_source.*: structured parse, resolution, graph construction, formatting, identity and diagnostics.
- src/assets/asset_source.* and reference/navigation services: completion, hover, range edits and reconciliation.
- src/assets/behavior_loader.c, project.*, graph/document/serialize.c: loading, discovery and format contract.
- src/runtime/behaviors/live.*: validated generation/state replacement.
- src/editor/documents, studio and text: synchronized code/graph editing and standard/Vim authoring.
- src/automation, tools/mcp: native source operations.
- content, projects, test fixtures, build/package/install tools: verified mixed-format migration and obsolete-path removal.

Verification:
- Focused C/pico_unit syntax, typed graph round trips, structured examples, ranges, identity changes, invalid candidate preservation and state lifecycle.
- Full corpus semantic comparison before cutover; canonical format and load checks afterward. Strip optional metadata and prove executable source still reconstructs equivalent behavior. Verify shared state, evaluation timing, external calls, loops and waits.
- Existing native runtime parity and navigation/controller/editor tests.
- Native build, required full native regression gates for this broad contract change, and Native/WASM Studio/controller checks. Report actual render coverage and unavailable hardware accurately.

The current user request authorizes behavior editor integration previously deferred in CUT-45. Unrelated panel conversions and publication holds remain outside scope.

Preservation: build/requirements-restart-baseline records the existing uncommitted tree 48a953d7578ba091afde0df7120ef30ec3537fea on HEAD 0e515e5453ea81a762c7885fbb2ec9481ad0b9bf. The real Git index is unchanged. Earlier validation belongs to that prior tree and is not evidence for this revision.

Progress:
- Requirements settled; source and validation ownership split across frontend, asset format, corpus, shared services and editor work.
- Baseline preserved before edits.
- Declarative the former document format restoration is complete, including embedded payloads and project manifests. Native semantic comparisons cover the restored documents.
- The shared readable frontend, source intelligence, metadata preservation, controller publication and Studio integration are implemented. Current source, precision, aggregate-capture and complete/incomplete-source navigation regressions pass.
- All 105 executable assets are active as readable source. The final1277 comparison verifies original raw document facts, executable semantics without metadata, independently reopened metadata-bearing source, native compilation and query parity for every asset. Every activation checked current bytes, preserved a backup and verified the written hash. Earlier baseline proofs are superseded.
- Studio browser checks cover simultaneous views, source/graph editing, standard/Vim navigation and history, debugging, invalid drafts, saved-file repair, quoted cross-file calls and creation of all three executable asset kinds. The migrated MovementSystem passes its independent movement oracle on generic, typed bytecode and SLJIT backends.
- Canonical checks cover all 11 project and fixture roots. Postcanonical verification proves all 105 current assets retain executable semantics, exact activation models, identical authoring metadata, queries and stable saves; all 315 original/current/metadata-free compilations pass. The source and corpus hashes remain unchanged through verification.
- Integration checks exposed and corrected callable/projection identity matching, expression grouping, shaped constructor labels, MCP expression editing, updated-catalog refactoring, empty-source dispatch and Studio creation routing. Failed and superseded runs are retained alongside focused, full native and web evidence under build/requirements-final-validation and build/requirements-editor-final-web-*.json. A focused pass or a superseded run does not establish a full gate.


Authoring ergonomics continuation (2026-10-07):
- User authorized implementation of settled improvements. Preserve the existing dirty work; source copy and hashes are in build/ergonomics-baseline.
- Keep executable .code authoritative; move layout/debug IDs to optional .code.meta. Exact-source metadata is hash-gated; code remains valid when identity matching is ambiguous.
- Support behavior-root inferred := defaults and number, typed unquoted Prototype/component symbols, static vec2 norm methods, and lexical await-result declarations. Ordinary eager block locals require activation storage and are not represented by lazy aliases.
- Fix generated expression labels and folded presentation in shared services, constrain graph field widths, and give static/editing expression regions consistent socket geometry.
- Improve code completion spelling/ranking/layout and regular/Vim indentation in the shared kit.
- Migrate corpus directly and bump the project version for the authoring contract. Preserve existing native numeric types during migration.
- Validation uses native/web compilation, selected checks and the user's explicit approval for focused language, indentation and expression-layout checks. Full native/render suites were not run, and historical full-suite results do not validate these edits.
- Main was fast-forwarded through 744adbdc. Overlapping edits were stashed and restored; preservation manifests and backups are in build/ergonomics-main-merge, build/ergonomics-main-merge-final and build/ergonomics-main-merge-latest. The document-tab discard conflict preserves both background-tab focus restoration and source-only drafts.
- All 105 migrated executable assets have optional sidecars. PlayerEffects and ProjectileHit now use concise statements, awaited results, typed asset symbols and ECS component parameters. The canonical formatter completed all 11 project/fixture roots.
- Browser verification caught and fixed missing Studio prototype context and unwanted source rewrites during equivalent catalog refreshes. Focused regressions cover clean PlayerEffects reopening, implicit ProjectileHit producer identity and metadata reloads.
- Real editor typing/undo/redo preserves independently restorable source metadata, node identity, layout and breakpoints. Completion displays the prototype name and type; Go to Definition opens the prototype editor. The web workflow was checked through the browser.
- Final 744adbdc integration builds pass for native Studio/controller/tests and serial WebAssembly Studio/source tests. Approved language/editor/indentation/expression-geometry checks plus WASM source checks pass in 4.270 seconds; the selected document-tabs workflow passes in 3.004 seconds. Earlier focused refactor checks passed in 34.720 seconds. Full native/render suites were not run. Work remains uncommitted.

ECS language continuation (2026-10-07):
- User approved implementation of the discussed language direction. Keep declarative values in the former format and the existing JIT; preserve dirty work and prior recovery snapshots. Current HEAD is 744adbdc. Additional working-source recovery is under build/ecs-language/baseline.
- Make entity component and field paths first-class typed expressions and assignment targets. Infer the entity owner and native event payload types. Canonical source uses bare names, readable statements, and actual events with `on`; irreducible action completions use `after`.
- Systems select entities through `name: Component` parameters, optional `Component?`, and `without Component, Component`. Their body implies Tick and supplies dt. Optional presence tests, including `if component`, narrow safely without introducing numeric truthiness.
- Ordinary block locals capture once at their execution point. Explicit `state name := value` persists per matching entity; `shared name := value` persists per registered system. Query exit cancels work and drops private state; reentry starts fresh. Suspension keeps one invocation per entity without an update backlog, and reacquires component data on resumption.
- Compile maintained `bind` assignments and component change handlers through native graphs. Resolve dependencies and entity lifetimes at deterministic update points, initialize bindings, and preserve change semantics for native and JIT writers. The runtime chooses execution strategy; no tuning annotations are required.
- Extend shared completion/navigation with component and field symbols plus prototype membership provenance. Known members rank higher without hiding other registered components. Studio refreshes this context without dirtying source or adding undo entries.
- Update canonical printing, source/graph metadata, authored assets, and versioned project manifests together. Source remains complete without optional metadata.
- Implement in parallel by compiler, runtime, reactive, printer, shared intelligence, and editor owners. Root coordinates integration/build files and validation. Focused C source/runtime/roundtrip tests plus native and serial web builds; real Studio Code/Graph/Both inspection. Report the actual checks and distinguish unrun suites from passing results.

ECS continuation delivery (2026-10-07):
- Implemented ECS paths, inferred events, optional system parameters and presence guards, retained entity state, shared system state, suspended locals, maintained bindings, qualified binding-change events, and shared prototype-aware completion. Reactive graph views expose authored reads and the editable Set while hiding compiler maintenance guards.
- Migrated required action callbacks to `after` and updated selected gameplay examples. Additive syntax does not force script rewrites. The experimental full-corpus printer migration was not applied; valid source, comments and names remain authored. Formatting now preserves system source as it already preserves behavior source.
- Current 108 scripts passed source validation in 6.878 seconds. Final focused native language/runtime/editor checks passed in 4.033 seconds; WASM source checks passed in 1.749 seconds. Canonical formatting plus its check passed across 11 roots in 21.615 seconds. Focused hidden-window expression geometry passed in 1.352 seconds. Native editor/controller/tests and serial web builds succeeded. Full native/render suites were not run.
- The isolated web Code/Both workflow confirmed source-to-graph and graph-to-source edits and the compact header. That manual session lasted 78 seconds; the tab and server were stopped.
- System source/query replacement currently cancels waits and resets private/shared state. General coroutine task handles, explicit yield, child tasks and cancellation by handle are not implemented. Binding dependency ordering/cycle checks are per asset; overlapping writers are checked across world managers. No whole-editor performance claim is made.
- Temporary migration executable/object, staged rewrite candidates and browser project were removed. Logs, helper sources and recovery copies remain under build/ecs-language. Prior recovery snapshots and unrelated work remain preserved. Changes remain uncommitted.
