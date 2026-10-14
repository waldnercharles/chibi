# Material and scene native retirement map

## Current candidate status

See the [canonical migration evidence](../studio-rml-migration.md#current-candidate-evidence-2026-10-14)
for current SDK/source identities, formatting, package, startup and interaction
results. Source retirement is complete; complete workflow parity is not certified.

## Candidate source retirement, 2026-10-14

Studio's native implementation and obsolete native build dependencies have been
removed from this candidate. The manifest `chibi-studio/NATIVE_RETIREMENT.json`
records 347 retired files, including 318 C/C++ source/header files, with baseline
`34f13325c3fb4e1888dbd64bf45d809e07c782d3` and original blob identities. Recover
reference code with `git show 34f13325:<path>` in the Studio repository.

The mappings below distinguish authored replacements from parity and acceptance
gaps. Removal of source is complete; validation of every former workflow is not.
Earlier SDK4 whole-application compilation passed; Home failed before UI
because inactive AssetBrowser required `workspace_resources`. SDK5 optional
service contracts subsequently passed CPU/package checks. Current results are recorded in the canonical migration evidence linked above. References to SDK2–SDK6 below
retain their original evidence scope and do not certify rendered acceptance.

This is a source migration map, not an acceptance certificate. Studio behavior,
layout and styling belong in its `.chibi`, RML and RCSS assets. Engine modules
below own reusable asset, document, rendering, physics geometry or host
capabilities. The mapped workflows still require installed-SDK application checks and closure
of the explicit parity gaps, even though the native source has been removed.

Paths under `src/editor/` refer to chibi-studio; paths under `src/authoring/`,
`src/render/` and `src/runtime/` refer to chibi.

## Current source additions

`StudioMaterialCreate.chibi` now owns matte, metal, emissive and unlit preset
selection and typed creation fields. `AssetFileHistory.chibi` owns recoverable
file deletion, and `StudioPrototypeActions.chibi` owns Apply and Save As policy.
The earlier name-only form gap recorded below is superseded by these authored
sources. Actual creation, cancellation, undo and room behavior still need
installed-SDK interaction acceptance.

SDK4 promoted measured pixel-ratio consumers for Graph, Scene, Material, Model
and UI Canvas. SDK5 adds bounded input timeline and simulation services; matching
Scene input, replay and generated-material consumers are promoted. Their first
installed-SDK5 source compilation exposed missing simulation declaration host
bindings and aborted. The subsequent engine fix and promoted Scene/Input
consumers passed isolated compilation and are included in later whole-application compilation; see the canonical
migration evidence linked above. Gameplay and browser replay acceptance remain pending.

The promoted runtime/evidence implementation retains the hash and byte count
of the exact executed prepared artifact and verifies it before
recording/replay/export. Current CPU validation is recorded in the canonical migration evidence. Trusted prepared imports
can record/replay with matching executable and SDK identity; document source
evidence still requires a compiler-owned frozen project.

Keep Changes is promoted, mounted and accepted by isolated source compilation;
its interaction acceptance remains pending. `StudioSceneKeepChanges`
captures changed selected reflected scalar values, queues a validated canonical
candidate, offers token-guarded capture Undo, and applies after successful Stop
through one correlated document edit/proposal. Generic runtime field JSON and
exact resolved scalar JSON in `WorkspaceAssetField` support the comparison.
No editor queue or selection policy moved into C. Typed source-local shader
Show node diagnostic consumers are also promoted and accepted by isolated
source compilation; unproved dependency origins remain unavailable rather than
navigating guessed IDs.

The collaboration source now uses checkpoint version 3 and opaque document
state, including grouped document history. The earlier history-serialization gap
below describes the pre-SDK4 implementation. Live recovery acceptance remains
required. This does not add an execution-state checkpoint.

## Earlier material and shape implementation record

These rows preserve an earlier implementation checkpoint. Missing/staged claims
refer to that checkpoint; later source additions above supersede them.

| Native implementation | Authored owner | Shared capability | State |
| --- | --- | --- | --- |
| `studio/studio_shader.c:studio_shader_controls`, `parameter_controls` | `StudioVisual.chibi`, `StudioMaterialProperties.rml/.rcss` | `visual_authoring`, document semantics, exposed typed defaults | Implemented source; validation is tracked separately. |
| `studio_shader.c:edit_parameter`, `finish_edit` | `StudioVisual.chibi` gesture and proposal policy | revision-guarded document transactions and visual source edits | The controller owns source flush, commit/cancel and room proposals. No renderer-owned canonical buffer. |
| `studio_shader.c:texture_controls`, `images_changed`, `remember_images` | `StudioVisual.chibi`, authored image choice controls | `workspace_resources`, `visual_catalog`, image preparation | Resource inventory and resident image provenance are engine data; picker policy is authored. |
| `studio_shader.c:view_controls`, `studio_shader_frame`, `drop_picture`, `studio_shader_free` | `StudioVisual.chibi` | `visual_authoring_preview`, render resource scopes | Session handles, draw sizes and asynchronous evidence jobs are explicit. Human interaction still needs rendered acceptance. |
| `studio_shader.c:refresh_report`, `diagnostics` | `StudioVisual.chibi` | `shader_report`, CPU sampling and compiler diagnostics | Static cost and CPU probes are labeled separately from rendered output. |
| `studio_shader.c:export_evidence`, `poll_export`, `export_result` | `StudioVisual.chibi` | frozen shader/model evidence plans and bounded preview readback | Source/dependency freeze precedes readback; manifest records SDK/backend/dimensions/coverage. |
| Native New Material form | `AssetBrowser.chibi` | `material_authoring_create`, `standard_material_build` | Current authored form uses name-only `workspace_asset_template` + exclusive document creation. Presets/typed fields and shared material namespace refusal are still missing; the existing native/CLI creator is the reference contract. |

Model operations are owned by `StudioModel.chibi` and the shared `visual_model`
and model preview/evidence services. No part of this map authorizes replacing
model editing with material-only controls.

## Earlier scene, tools and viewport implementation record

These rows preserve an earlier implementation checkpoint. Missing/staged claims
refer to that checkpoint; later source additions above supersede them.

| Native implementation | Authored owner | Shared capability | State or remaining work |
| --- | --- | --- | --- |
| `scene_viewport.c:app_scene_authoring_draw_viewport`, `app_scene_authoring_render` | `StudioScene.chibi`, `StudioSceneViewport.rml/.rcss` | `workspace_runtime` detached prepared player + production scene renderer | Frozen source compiler jobs preserve the last valid world on failure. Native and browser compiler adapters share the resulting prepared-player path. |
| `scene_viewport.c:pick_scene_entity`, 3D pick | `StudioScene.chibi` pointer policy | `render_geometry_2d_pick`, `render_scene3d_pick` | 2D body/custom shape/tilemap/mesh hits and 3D picks use runtime geometry; source identity mapping uses names/parents, not assumed scene indices. Undrawn editor-only markers are still absent. |
| `scene_viewport.c:fit_scene_view`, `frame_scene_view`, `fit_arena_view` | `StudioScene.chibi:Frame`, `ResetView` | runtime bounds; `render_geometry_2d_bounds`, `render_scene3d_entity_bounds` | Visible geometry frames without hidden entities. Marker framing remains tied to the missing undrawn-marker workflow. |
| `editor_camera.c`, view-through-camera branch in `scene_viewport.c` | `StudioScene.chibi` camera mode/navigation policy | runtime camera read, shared `camera_view`/`camera_entity_view` lens contract | Orbit/pan/zoom/reset/frame implemented. SDK3 typed camera binding and view-through-camera controls are staged with the production last-drawn project/ray contract; rendered acceptance remains pending. |
| `scene_gizmo.c`, `scene_gizmo3d.c` transform gestures | `StudioScene.chibi:Nudge`, `Nudge2D`, `Finish`, projected controls | pure object candidates, exact revision edits, runtime floating-field feedback | 3D position/quaternion/scale and 2D position/orientation/dimensions use canonical documents. SDK3 staged authored tools add world/object axes, inverse-parent translation, shared-pivot ring rotation and multiplicative scaling, plane/center handles, bounded marquee scans and modal grab. Source compilation and runtime acceptance of that staged expansion remain pending. |
| `scene_collider3d.c` outlines | authored display toggles | `render/debug_geometry` bounded CPU segments + optional production viewport draw | Boxes, spheres, capsules, 2D colliders and camera frusta implemented in engine source. Hull/mesh collider outlines are explicitly unsupported, as in the old native collider overlay. SDK3 typed presentation binding and authored toggles are staged; rendered acceptance remains pending. |
| `scene_collider3d.c:scene_collider3d_part`, `begin`, `drag`, `draw_handles` | authored collider handle policy | scalar canonical object candidates and runtime field feedback | SDK3 staged authored six-face handles edit box extents, sphere radius and capsule radius/height through exact document revisions. Singular/sheared placements and hull/mesh handles are explicitly refused. Canonical cancel/proposal and rendered acceptance remain pending. |
| `scene_viewport.c` lighting/fog/grid options | `StudioScene.chibi` | runtime presentation + existing renderer view/grid APIs | Grid implemented. SDK3 unlit/fogless/collider/camera-frame bindings and controls are staged, including existing saved preference keys. Quality/lab/shadow settings remain authored settings UI work. |
| Native gameplay input recording/replay (`engine_record_input`, `engine_replay_input`) | Authored scene recording workflow still required | runtime input timeline | Engine-owned PCG32 streams and record/replay seeding are verified by the SDK3 CPU random-isolation selector (4 cases, 431 assertions). Typed bounded timeline recording/replay and actual gameplay/browser replay execution remain required before claiming deterministic gameplay replay. |
| Native runtime play/pause/stop/step | `StudioScene.chibi` | `workspace_runtime_control`, deterministic engine fixed-step loop | Implemented source. Stop reloads the initial scene; it does not claim to restore an execution checkpoint. |
| Scene capture/evidence | `StudioScene.chibi` Capture/Cancel/status | `workspace_runtime_evidence` CPU freeze planner + asynchronous production viewport readback | SDK2 authored capture controls are present; SDK3 adds initialized engine-owned RNG provenance. Captures last successfully drawn unchanged viewport, actual pixel dimensions/camera/steps/time, SDK identity, source/executable hashes and bounded host-call trace. GPU/render acceptance pending. |

Debug geometry visits at most 4096 entities and emits at most 8192 segments.
Invalid geometry, unsupported shapes and truncation are reported by the engine
result. Camera clip-crossing overlay lines are omitted rather than projected
behind the eye. Those limitations belong in diagnostics and evidence; they do
not justify saying all colliders were rendered.

## Checkpoint contract and earlier recovery gaps

The old `scene_checkpoint.c:app_scene_authoring_checkpoint` serializes the
canonical scene, prototype/source buffers, document history, selection, camera,
actor identities and playback preferences. `restore_playback` starts a fresh
scene/prototype session and restores playing/paused preference. It does not
serialize physics solver state, program stacks, simulation accumulator, runtime
spawned entities or an execution step position.

Its replacement therefore belongs to authored workspace/document recovery:
`workspace_documents` buffers and grouped history, authored tab/selection/camera
state, and `StudioCollaboration.chibi` checkpoint/proposal policy. The current collaboration archive freezes file bytes and named open text
overlays, but does not yet serialize document undo/history. That is a real
retirement gap. SDK3 staged scene recovery carries bounded camera, selection,
tool, presentation and fresh-world playback preferences; oversized state refuses
checkpoint capture explicitly. Remaining recovery gaps must be listed by those
owners, especially history/sidecar atomicity and dirty file deletion undo. A new engine execution-state checkpoint is not
required to reproduce an old capability that never existed.

`workspace_snapshot` and browser compiler frozen roots are compiler input
snapshots. Runtime evidence bundles are inspection artifacts. Neither is an
execution-state checkpoint. Evidence explicitly marks gameplay assertions and
replay execution false, records truncated traces/untracked changes, and requires
the same SDK/resources/backend for pixel comparison. SDK3 runtime math/vector/state and execution random operations use an engine-owned
PCG-XSH-RR32 stream (fixed stream 54). Recording/replay seed only that owner's
stream. CPU source/VM/SLJIT/export isolation checks passed through the integration
coordinator; browser replay and gameplay assertions have separate acceptance
requirements. Evidence snapshots the actual initialized stream state after the
last draw and does not infer that replay executed. Hosts that mutate a target
outside runtime calls must call `workspace_runtime_mark_external_change`.

## Lifetime and acceptance gates

The Studio UI world, mutable asset previews and each prepared target own separate
render/type generations. Target UI retirement callbacks run in the old target's
scopes before every world retirement, including a world that never drew a
viewport. Readback jobs own their transferred canvas and frozen evidence plan;
a later draw or target replacement cannot change their bytes or provenance.

Current validation evidence is reported by the integration owner. An isolated
`.chibi` compile does not prove RML event routing, geometry selection, gesture
cancel, collaboration acceptance, browser compilation or GPU capture. Migration acceptance requires real authored application checks, appropriate
bounded CPU/GPU cases, SDK packaging and platform checks required by the task.

## Historical SDK5 validation record, 2026-10-14

The following records the earlier package before later source promotions and
current validation. Pending/staged statements here describe that checkpoint only.

SDK5 packaging and bounded CPU validation now pass. Installed build
`cef077585ca28f1edfb80ccc00d6344026bec1bf347104974b1303d08d210a21`
has manifest SHA256
`4aab3609c818da3813741e160ab972068e9540e0206a3773af8253921c230baa`
and engine source snapshot
`5cb31f58592436059bd0ed49e44013551d2063eb045e6f17bf3128b0f0346ba9`.
All five required groups passed, along with owning workspace-authoring,
workspace-services, host-services, authored-surface, code-control, event-payload
and random-isolation selectors. Strict installed-SDK packaging also passed.
Whole-application SDK5 compilation and Home startup remain pending at this
update. Future SDK6 work is staged and unvalidated. These CPU/package results do
not establish rendered interaction or browser acceptance.

