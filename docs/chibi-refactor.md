# chibi editor usability refactor

> Historical implementation record. Commands, MCP recipes, helper scripts and
> validation results below describe the recorded revision. The MCP product and
> its authoring helpers are retired. Current authoring uses saved `.chibi`
> files and [native checks](runtime-authoring.md); see [testing](testing.md) for
> admitted checks. These results do not verify the current checkout.

Tracking: https://github.com/waldnercharles/c-blueprints/issues/44

Implement one ticket at a time through delegated work, primary integration, and rendered/runtime verification. Preserve the extensive preexisting uncommitted Arena3D changes. Issues remain open while implementation is local and uncommitted.

## Status and order

| Issue | Work | Local status |
| --- | --- | --- |
| #37 | Arena3D content closure | Verified |
| #38 | Behavior simplification | Verified |
| #39 | Document tabs, global history, Arrange | Verified |
| #40 | Quick Open, references, useful asset documents | Verified |
| #45 | Searchable inline input bindings | Verified |
| #41 | Material authoring and edit performance | Verified |
| #42 | Appearance/geometry contracts | Verified |
| #43 | Rename engine/editor to chibi | Verified |

Research: [Editor workflow comparison](research/chibi-editor-workflows.md), based on official VSCode, Unreal, Unity, GameMaker and Blender documentation.

Keep Scene and Graph visible together. Reuse document routes, history, and retained authored state. Make asset relationships navigable and input controls visible at the point of use. Material graphs already provide Surface output and procedural operations; their immediate problems are authoring ergonomics and value-edit latency.

## Additional usability proposals

The rendered document workspace and source review suggest three bounded follow-ups. These are proposals beyond the current ticket sequence.

- Give every floating graph command a tooltip with its name, shortcut and reason when disabled. Arrange has a tooltip; the shared icon-button helper does not supply one for the remaining transport commands.
- Keep the active asset and selection context explicit in the Inspector. Use a compact asset or component heading and useful empty state; avoid adding explanatory text to every pane.
- Once multiple documents are routinely open, add an open-document switcher and tab actions such as Close Others and Reopen Closed. Evaluate overflow in the completed Content/Quick Open workflow before adding another navigation control.

## Baseline and content migration

The original checkout had 173 modified tracked files plus extensive untracked Arena3D work. Native build/tests passed. Baseline capture: `/tmp/chibi-before.png`. Preserve preexisting work; do not reset or stage the entire checkout.

The shipped dependency closure is now 49 JSON assets: 19 behaviors, 5 events, 3 libraries, 7 materials, 13 prototypes, Arena3D, and ShaderMath. Legacy samples are fixture-only under `test/fixtures/content`. Startup opens Arena3D and TwinStickInputBehavior. Shared WeaponActions defaults now spawn ArenaProjectile.

Scene entity behavior arrays replace prototype attachments. Apparently duplicated arrays cannot be removed without changing gameplay. Native component dependencies also matter: absence from a prototype's own behavior graph does not establish that a component is unused.

#37 verification: make build/test, --ui-native, --shader-ui and --arena-ui pass. Primary inspected `/tmp/chibi-content-verified.png`. Native pointer tests convert framebuffer bounds to logical mouse coordinates on Retina.

## Behavior authoring results

Counts exclude generated expression implementation graphs.

| Behavior | Before | After | Largest authored graph after |
| --- | ---: | ---: | ---: |
| Enemy Visual | 44 | 16 | 9 |
| Projectile Visual | 40 | 5 | 5 |
| Ship Motion | 93 | 43 | 15 |
| Pickup | 120 | 83 | 31 |
| Wave | 146 | 139 | 26 |
| Shard | 56 | 51 | 28 |
| Drop | 52 | 52 | 21 |

Behavior simplification and input bindings reduce these selected behaviors from 551 to 389 visible nodes. Generic range, spring, angle and easing operations replace repeated math plumbing. Trigger Visual Reaction restarts the existing visual-motion pulse; continuous rotation stays in VisualMotion. Gameplay formation and pickup rules remain authored behavior logic.

Ship uses Advance Rolling Motion and Apply Ship Pose functions. Shard initializer inputs name actual state (Pitch Rate, Roll Rate, Lift). Expressions must be refreshed after connecting typed inputs so their generated signatures remain valid. Preserve data dependencies: continuous velocity differs from quantized face angle, and shard fade uses the current age and Birth Scale.

#38 verification: `/tmp/chibi-38-final-make-test.log`, explicit --arena-encounter and --arena-ui pass. Formation probes check all 32 positions; shard probes check source-scale preservation. Primary inspected final Enemy, Ship and Shard graphs. Final captures are also in the task's visualization directory.

## Document state and navigation

Typed workspace tabs own identity/order; existing editor document history retains graph JSON, Undo/Redo, view and selection. Prototype edits retain their existing state. Scenes retain authored snapshots, history, camera and selection in one ECS world. Systems retain graph and query snapshots and saved baselines.

The current underlying scene is distinct from the active asset document. Returning from an asset to that scene keeps the live session. Switching to another scene restores its authored state and remaps entity handles in history. Global Back/Forward uses stable scene/document keys; Save As changes a path without losing identity. Failed restores roll back. Save paths refresh Discard baselines and reject open-path collisions.

Closing a dirty document offers Save/Discard/Cancel, with actual shared-asset reversion. Keep one scene open; closing an underlying scene first activates another scene. Tabs use ordered removal. Same-tab document selection does not capture/synchronize state: a redundant Scene-pane selection previously ended a newly started viewport drag.

The document bar contains global Back/Forward and named tabs. Node Canvas shows the displayed graph asset name. Arrange has its own floating control. Empty graph selection keeps the Inspector in the active document context. System dirty tracking uses graph/query revisions rather than per-frame serialization. Synchronization observes pending editor edits before the revision advances.

The previously uncalled checkpoint tests now run in an initialized hidden Cute/ImGui process through --scene-documents and make test. They cover scene/system retention, Undo/Redo, camera/selection, Save As history, rollback, close/discard, stable unsaved keys, and repeated navigation between populated scenes.

#39 accepted locally: normal make build and make test pass (`/tmp/chibi-39-final-make-build.log`, `/tmp/chibi-39-final-make-test.log`). Normal --arena-ui, --ui-native and --shader-ui pass (`/tmp/chibi-39-final-normal-ui.log`, `/tmp/chibi-39-final-ui-native.log`, `/tmp/chibi-39-final-shader-ui.log`). Native tests cover Navigator-hidden global history, Arrange/Undo, dirty-close Cancel/Discard, reopening, gameplay and Performance. Primary inspected `/tmp/chibi-39-documents.png`. The temporary focused runner used while unrelated automation work could not link is superseded by normal build/test results.

## #40 implementation direction

Use a focused typed in-memory reference index owned by app_scene_authoring. Identity includes kind plus canonical path and stable session identity where no path exists. Same-name Prototype and Material must not collide. Edges retain graph/node/pin/component-field/scene-actor provenance for reveal.

Read authored working definitions before library linking, which replaces external reference provenance. Include prototype attachments, scene instance attachments and replacement semantics, Shader material/shape, MeshParts materials, spawn targets, Event consumption/emission and external Library calls. Include retained inactive scenes/systems and pending active edits. HostTick/Begin are not external assets.

Invalidate from actual revisions/generation or query on demand. A dirty bool or topology revision alone misses name/default edits. Do not serialize/rebuild the project every UI frame. Reserve integration for #45 binding references.

Global Quick Open uses keyboard search and typed results with normal tab/history routes. Consolidate discovery in Content while keeping scene hierarchy and document structure clear. Event documents show signatures/emitters/consumers; libraries show authored functions/imports/callers and omit generated expression internals. Component material/shape references have adjacent Open actions.

Verify typed identity, edge provenance, inactive/pending edits, refresh and unresolved targets; native Quick Open, reference reveal/Back, useful Event/Library views and component-to-material navigation.

Rendered integration corrected clipped material Open actions by reserving their width in the shared shader property row. Content uses the available pane height, with folder browsing behind a collapsed disclosure and prototype drag-and-drop available directly on catalog results. Quick Open is global and opens at a fixed upper-center workspace position. Native reference checks wait for the next canvas frame before measuring revealed node visibility.


#40 accepted locally: make build/test and normal --ui-native, --arena-ui, --shader-ui pass. Final core/state verification: `/tmp/chibi-40-final-make-test.log`; headless tail independently passed exit 0. Native logs: `/tmp/chibi-40-final-ui-native.log`, `/tmp/chibi-40-final-arena-ui.log`, `/tmp/chibi-40-final-shader-ui.log`. Primary inspected upper-center Quick Open and Content/material captures. The shader suite exposed a freed editor working pointer after Undo; the index now reads the current editor asset and checks definition name/domain before overlaying it. A concurrent automation build used a removed preview-idle function; the narrow integration repair uses the supported preview tick at time zero.

## #45 input bindings

Use explicit authored input metadata, not invisible persistent getter nodes. Offer variables by stable ID, Self component fields by reflected identity, and context values such as Delta Time. Show the source on the node, with searchable type/scope-filtered choices, clear-to-literal, source navigation and conversion to a getter.

Resolution order: wire, active binding, existing default-Self behavior, literal. Preserve dormant binding metadata while wired; disconnect restores it. Invalid active bindings report errors. Reads must preserve getter evaluation timing, watches and dependency/effect tracking.

Runtime seams are eval.c and flow.c, using behavior_instance_copy_variable, owner plus ecs_component_read_field, and eval_context.delta_seconds. Compiler validation covers source identity/type/scope and component-read effects. Serialize optional binding metadata and use existing whole-asset Undo snapshots. Systems require explicit lowering/query support before offering bindings; material/shape compilers must exclude unavailable ECS/behavior context.

Test read timing, DeltaTime, Self fields, errors, wire precedence, source/type changes, serialization, Undo, clone/extract, effects, native picker interaction and usage-index integration.

Extraction must preserve when reads occur. Pure extraction can move a binding onto an explicit replacement-call input and wire the new function parameter to its consumer. Executable extraction retains bindings on copied consumer pins inside the new function, using the same behavior instance and stable variable namespace. Do not hoist these reads onto call inputs: that would move them ahead of mutation, suspension or branching. Keep dormant bindings through crossing/internal wire transformations.

Variable renames keep stable identity. Deletion or incompatible retyping leaves a visible invalid binding; fixed consumer pin types must not change to match the source. Clipboard transfer between assets must prevent accidental rebinding to an unrelated variable with the same numeric ID. Component fields use reflected keys and field names, never stored offsets.

#45 integration progress: graph inputs now carry explicit bindings, with an inline source control/chip and searchable choices. Runtime component reads use an ECS adapter through evaluation services, keeping graph-only consumers independent of ECS. Clipboard provenance follows document ownership through Undo/tab restoration; same-name foreign assets cannot silently reuse local variable IDs. Pure extraction moves bindings onto call inputs, executable extraction retains read timing, and ambiguous dormant fanout is rejected. Dormant bindings behind wires do not invalidate compilation or add source effects. Active behavior bindings in shader graphs are rejected.

Core and native binding tests pass locally. Primary accepted the rendered picker, bound chips and migrated Ship graph. Final make build/test, --ui-native, --arena-ui and --shader-ui pass; logs are /tmp/chibi-45-final-*.log. Chips scale with zoom, long source names use ellipsis and tooltips, and Inspector actions fit the available width. The first migration removes 16 single-consumer getters: Ship Motion 54 to 43, Pickup 87 to 83, Enemy Visual 17 to 16. Shared/watched getters remain. The migrated core suite passes (`/tmp/chibi-45-migrated-core-suite.log`). Runtime regression testing caught wildcard defaults affecting shader vectors; preservation is now limited to bound inputs.

### Whole component binding correction

MovementActions.Movement Velocity accepts a complete Movement value on Speed. The original picker enumerated component fields only, leaving that struct input without the corresponding Self source. Whole component bindings use an explicit component kind and stable component key. The picker offers Self.Movement and also matches the familiar Get Movement name. Field bindings remain distinct. Runtime reads copy the component value at evaluation time; conversion creates the normal getter directly. The native regression reproduced the omission with the whole-component option disabled, then passed after restoration, including getter conversion and Undo. Normal make build/test and the native UI suite pass; runtime coverage includes copied snapshots, type errors, missing components, serialization and wire precedence. Logs: /tmp/chibi-movement-test-final.log and /tmp/chibi-movement-ui.log.

### Generic source correction

The user clarified that any compatible source that needs no other input connection should be selectable. The component correction enumerated all registered components, but the catalog still listed only variables, components, fields and Delta Time. Its strict pin compatibility check also omitted wildcard and automatic conversion cases accepted by normal wiring.

The correction uses ordinary source nodes and edges, with a presentation flag to display a source inline. The input names its source and offers expansion to the ordinary node for configuration. Eligibility follows the current graph's node catalog and normal connection rules. Data sources can use their authored defaults; unresolved generic values are excluded. Self-sufficient random sources remain eligible and keep ordinary graph evaluation semantics. Executable nodes remain ordinary flow nodes. Sharing a source or connecting one of its inputs makes it visible. Runtime evaluation, conversions, dependency tracking and errors continue through the ordinary graph paths.

Acceptance covers non-Movement components, generic context sources, pure functions, wildcard and conversion connections, expansion, Undo, persistence, clipboard and extraction. `make test`, `make build`, native workspace/picker tests, shader UI, Arena UI and bounded `make dev` startup pass. Native coverage includes Health, Transform aliases, Self, Delta Time on a wildcard, a no-argument pure function with scalar conversion, and material Time/Constant sources. It checks source visibility, clearing, expansion, Undo/Redo and round trips. Model coverage checks clipboard ownership and extraction. The conversion test also exposed an existing Integer-to-float edge failure: evaluation validated the intermediate double before normal float coercion. Both pure and flow evaluation now coerce before validating the converted value. Logs: `/tmp/chibi-generic-source-test.log`, `/tmp/chibi-generic-source-ui-final.log`, `/tmp/chibi-generic-source-shader-ui.log`, `/tmp/chibi-generic-source-arena-ui.log`.

## #41 material authoring and performance

Measure the real ramp UI gesture before changes. Extend shader_ramp_ui to a continuous 60-frame drag after warmup; time full shader_frame calls and report p50/p95. Add an actual pipeline compile counter at shader_program.c compile entry. Catalog revision alone is not a driver compile count.

Use a compact per-program vec4 uniform array for ramp stop positions/full RGBA (two vec4 per stop), supporting multiple ramps. Lay out offsets per compiled program across every included catalog asset, including image-less assets; regenerate GLSL from stored IR with those offsets. Do not use fixed asset-ID offsets or globally cached GLSL offsets. Use a stable transport capacity of 512 vec4, because Cute retains each uniform array length across shader changes. Bound actual ramp data to that capacity; 512 vec4 uses 8 KiB alongside the existing approximately 3.1 KiB Arena block, under 16 KiB. Keep existing named material parameter slots available. Cute supports array uniforms across 3D, fragment and compute paths. Ramp fields live in the existing bound blocks: the Arena uniform block, fragment shd_uniforms, and compute Params. Separate blocks were unsuitable: fragment binding names matter, and a separate compute set collided with the existing Metal buffer index. Declarations precede generated graph functions.

Numeric edits update uniforms with no GLSL/program recompilation. Stop count/topology/interpolation changes remain structural. Coordinate shader declaration order across scene, fragment and compute generation. Avoid unchanged preview upserts. Batch ramp-strip drawing, but measure compilation as the likely primary stall.

Inline common scalar/vector/ramp/color controls; shared RGBA/HSV editing preserves alpha, HDR values and Undo grouping. Remove redundant Shader prefixes from display/search labels, while retaining necessary globally qualified serialized operation names to avoid collisions.

Verify preview pixels and zero numeric-drag compile deltas; topology recompilation; Arena, 2D fragment, compute/radiance and WebGL; invalid edits retain last valid preview. Keep timing comparisons reported rather than hardware-dependent test thresholds.

#41 integration: numeric ramp payloads have a separate revision/cache from structural program revisions. Failed structural compilation retains the last successful shader and matching payload; an oversized edit preserves an existing valid program and reports an error. The original 60-frame drag measured p50 8.396 ms and p95 9.247 ms. A native candidate now changes preview pixels with zero gesture compilations and measures p50 8.561 ms and p95 9.124 ms. This is not evidence of a large frame-time improvement on this machine. The original compile-counter baseline was not captured. Primary GPU regressions pass in /tmp/chibi-41-primary-render-2.log, including numeric pixel changes with no compiles on Arena/fragment/compute, structural recompiles, multiple material offsets, failed-compile retention and initial/existing capacity limits. Final native shader, Arena and workspace suites pass. Typed HSV V=2.5 and alpha=0.25 survive Undo/Redo and save/reload. Primary inspected the final ramp and color captures. Final drag measurement: p50 8.695 ms, p95 9.007 ms, zero compilations and changed preview pixels. Logs: /tmp/chibi-41-native-shader-ui-final-7.log, /tmp/chibi-41-native-arena-ui.log, /tmp/chibi-41-native-ui-native.log. Normal make test passes in /tmp/chibi-41-primary-make-test.log.

## #42 appearance and renderer scope

The Arena3D renderer selects components generically and has no Arena prototype-name rendering branches. Remove redundant Appearance from Enemy, Floor, Player, Projectile and WallHorizontal and six Arena3D boundary overrides. Remove Enemy's unused SdfMotion.

Rename SpatialAppearance directly to Geometry3D throughout reflection, assets, registered node contracts, code and tests, with no alias. Inspector labels: 3D Geometry and 2D Appearance. Keep Shader shared; do not combine incompatible dimensional geometry. Preserve native Experience, lights/influences, trails, emitters, mesh parts, visual motion and EncounterMember dependencies. Add fixture-only 2D Appearance where old 2D tests intentionally reuse Arena prototypes.

Verify rendering, picking, fit, preview/effects, physics, serialization and 2D fixtures.

#42 accepted locally: 85 old-contract occurrences migrated across 32 owned files, with reflection manifest updated and no alias. Removed five prototype and six boundary 2D appearance attachments plus Enemy SDF motion; three explicit 2D fixture overrides preserve legacy fixture intent. Independent review found no remaining native dependency. Delegated make build/test, render-checks and native suites passed; primary build and scene-document checks passed, and primary inspected Arena captures. Logs include /tmp/chibi-42-primary-build.log, /tmp/chibi-42-primary-documents.log, /tmp/chibi-42-arena-ui.log and /tmp/chibi-42-shader-ui.log.

## #43 naming boundary

Owned engine/editor branding and API prefixes use chibi or precise domain names. The migration covers public headers, CMake targets/options, host reload exports, the virtual shader root, environment variables, scripts, tests, reflection and documentation. There are no legacy aliases.

The actual GitHub repository and local checkout retain their existing addresses. Unreal Blueprint proper names and third-party code retain their original names.

Automation contracts use src/automation, tools/chibi_ctl and tools/mcp, with consistent controller targets, server resource URIs, environment variables and smoke tests. Protocol operations and versions remain unchanged. Host module exports and symbol lookup use chibi_application, and new working state uses .chibi. GAME/DEMO reflection identities and external CF/SLJIT/libclang contracts remain intact.

#43 accepted locally: delegated make build, make test, make ui-native, make render-checks, make dev-smoke, MCP smoke and make web all passed after the final UI/font/settings migration. Public export inspection found _chibi_application. Primary independently confirmed the owned-source name sweep and git diff --check, captured and inspected the renamed native editor, and loaded the finished WebGL editor and Arena3D play mode in the browser. Both rendered successfully. Play reported the bounded 64-sample surface influence limit and continued rendering; the strongest effects are retained by the renderer. No browser shader compilation or context error was observed.

Rename inventory: /tmp/chibi-43-pre-rename.inventory. Final delegated audit: /tmp/chibi-43-final-owned-audit.txt. Rename command output was retained in the delegated task rather than per-command log files. Final native capture: `<local-evidence>/chibi-editor.png`.

All eight implementation tickets are verified locally. No files were staged, committed or pushed. The original dirty work remains part of the checkout; the original generated cache remains locally ignored and untouched.

## Development tilt and performance follow-up

The initial temporary tilt lived only in standalone game presentation. It now uses a shared camera constructor and explicit viewport camera state, so the editor Scene Play path used by make dev, rendering, picking, ground aim and selection overlays use the same view. Pause freezes the phase; Stop clears it. Prototype previews keep their usual camera.

The standalone demo left open during the experiment was consuming approximately one CPU core and was closed before measurements. Current build and build-dev caches use unoptimized Debug with address/undefined-behavior sanitizers enabled. Open PRs #72, #73 and #74 concern MCP controller operations; none changes the editor or behavior runtime hotspot. No PR was merged.

The native Arena editor benchmark uses 1920x1080, 1800 fixed 1/60 frames and 180 warm-up frames. Times below are host wall frames, including presentation waits, not GPU pass timings.

| Comparison | Median frame ms | p95 frame ms | Mean behavior ms |
| --- | ---: | ---: | ---: |
| Original camera, Debug + sanitizers | 16.287 | 26.158 | 13.369 |
| Original camera, optimized without sanitizers | 8.318 | 9.128 | 1.362 |
| Tilted camera, original runtime allocation | 16.506 | 26.746 | 12.728 |
| Tilted camera, bulk runtime allocation | 11.510 | 19.480 | 8.001 |

Each comparison pair has identical gameplay counts and active timing sample counts. These are individual measured runs, not a historical regression baseline. The first pair isolates the build configuration; the second compares the allocation fix with the same tilted camera. Other stage timings vary between runs.

Sampling identified runtime_store_node's per-ID append loop as the dominant active hotspot. Asset-wide node IDs can be sparse within a function graph. The fix grows the lookup array once and zeroes the newly exposed range, preserving old entries and graph-lifetime checks. A regression covers sparse growth, preserved values, and empty slots alongside existing removal/reassignment tests.

Evidence: /tmp/chibi-perf-debug-editor.log, /tmp/chibi-perf-release-editor.log, /tmp/chibi-perf-debug-matched-before.log, /tmp/chibi-perf-debug-after.log and /tmp/chibi-perf-debug-sample.txt. The stale build-release dependency checkout would not accept its patch; the optimized comparison used a fresh, locally ignored build-perf-local directory. Default diagnostic build flags remain unchanged.

Validation passed: make build, make test, the native --arena-ui suite (including pointer targeting, resize, pause and Stop), and make dev ARGS="--headless --frames 3", which loaded the rebuilt application generation and exited successfully. Primary inspected the rendered tilted editor capture. Logs: /tmp/chibi-dev-tilt-build.log, /tmp/chibi-dev-tilt-test.log, /tmp/chibi-dev-tilt-arena-ui.log and /tmp/chibi-dev-tilt-host.log. Changes remain local and uncommitted.
