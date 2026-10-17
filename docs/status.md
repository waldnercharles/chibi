# Current status and backlog

Reconciled on 2026-10-11 for the combined review candidate: engine PRs
[#343](https://github.com/waldnercharles/chibi/pull/343) through
[#353](https://github.com/waldnercharles/chibi/pull/353) on engine `main`
including #340, and Studio PRs #55 through #59 on Studio `main` including #60.
This is the starting point for choosing work. Detailed contracts stay in their
owning docs; [PLAN](../PLAN.md), the [Arena plan](arena-roadmap.md) and the
[RML migration record](studio-rml-migration.md) retain dated history.

Implementation means the reviewed source contains the capability. Coverage means
checked-in assertions exist, subject to [test admission](test-quarantine.md).
Execution evidence names the revision and checks actually run. None implies the
other two. The combined candidate's final execution evidence belongs in its
integration PR; the historical results below do not validate that combination.

## Implemented capabilities

| Capability | Owning implementation and checked-in coverage | Limits and integration |
| --- | --- | --- |
| Qualified library and prototype-argument completion | [Source intelligence](../src/authoring/source_intelligence.c), [navigation cases](../test/language/navigation.c) cover library-member replacement ranges, lexical shadowing, incomplete calls and declared prototype parameters including named arguments. | See [navigation](authoring-navigation.md) for remaining context and integration limits. |
| Definitions, references, rename and calls | Shared [navigation](../src/authoring/source_navigation.c), [rename](../src/authoring/source_rename.c) and [navigation cases](../test/language/navigation.c); Studio exposes these through its text/navigation UI. | The controller and selected Studio pointer/debugger cases are described below; broader interaction coverage remains separate. Studio #60 rewrites recorded entity-path fields on object/ancestor rename and through undo/redo/checkpoints. Hierarchy moves retain authored path strings. |
| Scene hierarchy | [Scene authoring contract](runtime-authoring.md), [ECS hierarchy cases](../test/tools/ecs_hierarchy.c), [scene cases](../test/tools/scene.c) and Studio's `--scene-hierarchy` cover nested persistence, parent-relative transforms, picking and undo. | [Scene moves](scene-authoring.md) and Studio's scene-move/outliner-move cases cover reparenting, ordering, grouped history and collaboration. Behavior/component path strings are not rewritten by moves. |
| Box3D integration | [3D runtime contracts](runtime-authoring.md#simulate-3d-bodies), [physics implementation](../src/runtime/scenes/physics3d.c) and [physics cases](../test/tools/physics3d.c) include bodies, contacts, queries, compounds, joints and capsule characters. | Grounded capsule characters ride translating/rotating support bodies. Jumping, separation, collider removal/replacement and filters release support as documented. |
| glTF/GLB import | [Import contract and limits](rendering-3d.md#importing-gltf-models), [importer](../src/assets/model_import.c) and [import cases](../test/authoring/model_import.c). Meshes, materials, images and prototype output already exist. | `KHR_texture_transform` is implemented for all five material texture slots, with import/render cases. Other UV sets, sampler fidelity, skinning and animation remain outside that slice. |
| Directional, point and spot shadows | [Shadow contracts](rendering-3d.md#shadows), [directional](../src/render/scene3d_shadow.c) and [local-light](../src/render/scene3d_light_shadow.c) paths, [scene3d assertions](../test/render/scene3d.c). | Point/spot shadow edges remain hard. Authored rendering fixtures now cover the previously listed rendering group; remaining exclusions stay explicit. |
| Studio undo, dirty prompts and asset selection | [Acceptance inventory](https://github.com/waldnercharles/chibi-studio/blob/main/tests/acceptance/README.md) describes production history, SDL keyboard input, dirty-prompt pointer clicks and list/grid selection cases. | New graph-pointer and asset-pointer cases use SDL hit testing. Debugger-source-history covers invalid drafts against a retained paused generation; valid live replacement and broader debugger gestures remain outside that case. |
| Browser publishing | Engine [release workflow](../.github/workflows/publish.yml), Studio [distribution workflow](https://github.com/waldnercharles/chibi-studio/blob/d10222dc73e01340dd3ccd193ab016c706708ffc/.github/workflows/publish.yml), and the [game web workflow](game-web-workflow.yml) that publishes a game's player plus `/dev/` workspace are implemented. | [Serial browser acceptance](https://github.com/waldnercharles/chibi-studio/blob/main/tests/acceptance/BROWSER.md) records those interactions on named revisions. Pending browser saves now retain unload protection and retry until durable; worker mode and cross-browser acceptance remain separate. `make publish-web` is still unavailable; Actions owns publication. |

## Execution evidence

These are recorded results, not tests re-run by this documentation change.

- The [October 8 extraction evidence](../PLAN.md#final-implementation-evidence--2026-10-08)
  records installed packages, four native Studio edit/undo/redo frames and bounded
  SpaceCadet VM/SLJIT playback. Its local artifacts and package identities belong
  to that delivery. It did not launch the browser.
- [Navigation's recorded validation](testing.md) includes the earlier 16-case,
  195-assertion, 0.614-second ASan/UBSan capture. It is not a fresh run at `f3ff608c`.
- The [rendering record](rendering-3d.md#tests) reports 95 cases and 83,503
  assertions on October 10, in 23.3 seconds on Vulkan llvmpipe and 15.2 seconds
  on GLES 3.2. This is a recorded focused run; GLES does not establish browser
  product acceptance, and this audit did not re-run either backend.
- [Studio PR #53](https://github.com/waldnercharles/chibi-studio/pull/53) reports
  all 16 acceptance selectors at `243fdc6`, each below 60 seconds. At `64c0b3b`
  it re-ran source-edit, code-keys, kdl-keys, code-followups, code-glyphs and smoke.
  Neither report establishes all interactions on `d10222d`.
- Engine [release run 38099495996](https://github.com/waldnercharles/chibi/actions/runs/38099495996)
  successfully built and published packages for `f3ff608c`.
- Studio [publish run 38088359471](https://github.com/waldnercharles/chibi-studio/actions/runs/38088359471)
  built and deployed `d10222d` using engine `fd69f0bf953e1adfa40bcc053162747db95d12ff`.
  Its [native CI run 38088359470](https://github.com/waldnercharles/chibi-studio/actions/runs/38088359470)
  passed bounded starter/Studio-project startup, installed startup and game export
  against that engine. These runs do not validate the `d10222d`/`f3ff608c` pair
  or establish browser interaction or threaded-product acceptance.

Admitted native/language drivers and focused GPU selectors do not release the
[quarantined full suites and historical oracles](test-quarantine.md). A workflow's
configured steps, an installed package, or an old screenshot cannot substitute
for current runtime evidence.

## Reviewed work and remaining scope

The October 10 workstreams below now have reviewed implementations and checked-in
coverage in this combined candidate. Final execution results and eligibility for
landing are recorded by the integration PR. Shared SDK changes belong in the
engine, interactions in Studio, and game rules/composition in game assets.

| Workstream | Reviewed implementation and coverage | Remaining scope or evidence limits |
| --- | --- | --- |
| Rendering feature coverage | [Rendering fixture guide](../test/fixtures/feature-coverage/README.md), authored rendering/surface/visibility scenes and C fixture assertions cover the prior 17-component/3-function rendering group. | Keep engine-internal and other authored-coverage exclusions explicit. Current GPU execution belongs to the integration record. |
| Controller authoring workflows | [Controller cases](../test/tools/controller.c) exercise command-level source ranges, diagnostics, rename non-mutation, generation/backend identities and profiling warmup/accounting. | Broader CLI compatibility and Studio gestures are separate. |
| Published browser workflows | [Serial browser report](https://github.com/waldnercharles/chibi-studio/blob/main/tests/acceptance/BROWSER.md) records deployed identities, player controls, edit/save/reload/export and controlled revision isolation. [Storage cases](../test/tools/web_storage.c) exercise production scripts through save/failure/concurrency paths. | Historical browser evidence names its revisions. The combined candidate adds a flush-completion race fix; fresh deployment, worker mode and cross-browser coverage are separate. |
| glTF texture UV transforms | [Import contract](rendering-3d.md#importing-gltf-models), import cases and rendered controls cover nonidentity transforms across PBR slots and normal-map conventions. | Other UV sets, sampler fidelity, skinning and animation stay outside this slice. |
| Visible Studio panel idle work | Studio caches selection-derived lists and constructs field/debug-target choices only when visible. Its panel-idle acceptance case checks construction counts, invalidation and reopen behavior. | Small-fixture counts do not establish a whole-application speedup or large-list behavior. Held panel conversion stays out of scope. |
| Studio pointer and debugger interactions | Studio's graph-pointer, asset-pointer and debugger-source-history cases cover rendered selection/search/filter input and paused-source history. | Valid edits replacing a live generation, broader stepping/gestures and web pointer parity are not established by those cases. |
| Installed player rendering | [`--terrain-playable-render`](../test/tools/terrain_playable_render.c) runs an installed player on an artifact the installed compiler prepared from `test/fixtures/terrain-lab`: four hidden frames and a PNG check. | Package-pin, resource-closure relocation and lab landmark checks were retired with the example projects on 2026-10-17 ([examples](examples.md)). |
| Moving-platform characters | [Physics contract](runtime-authoring.md#simulate-3d-bodies), physics cases and neutral MovingPlatforms3D fixture cover support translation/rotation, separation, filters and lifetime. | No general character-controller redesign or automatic momentum inheritance. |
| Outliner hierarchy editing | [Shared scene-move contract](scene-authoring.md) and Studio scene-move/outliner-move cases cover moves, ordering, transforms, stable identity, grouped history, persistence and collaboration/checkpoints. Studio #54's parent/path preservation is included through merged #60. | Authored path references remain text. Runtime sessions reject editing; separate hierarchy coverage owns Play/Stop restoration. |
| Gap documentation | This entrypoint and engine/Studio status references describe the combined reviewed source and preserve historical evidence. | Validation claims must remain attached to the actual tested revision. |

Existing `cwaldner/3d-starter` work in the primary engine/Studio checkouts is
outside this integration; preserve those checkouts and do not infer its
acceptance from these changes.

## Unassigned work and holds

The [feature-coverage exclusions](../test/fixtures/feature-coverage/coverage-gaps.txt)
also list material simulation, 2D lighting/sky, the Visibility At query and
optional combat/encounter uses. They are missing authored coverage, not a claim
that the capabilities are absent.
Other glTF limitations stay in the [import contract](rendering-3d.md#importing-gltf-models).
Prototype component-presence ranking remains a [navigation limitation](authoring-navigation.md).
Physical macOS trackpad validation and the remaining historical Studio reports
need current evidence before their status changes.

Behavior-driven conversion of the other Studio panels remains explicitly on hold
in the [migration record](studio-rml-migration.md#current-scope-2026-10-11).
Outliner interaction approval does not release it. Preserve all existing PR/ticket
holds and denied-action approval requirements, including HRC
[#173](https://github.com/waldnercharles/chibi/pull/173) and
[#187](https://github.com/waldnercharles/chibi/pull/187); this reconciliation
supplies no merge authority. Recheck eligibility before coordinated integration
under [reviewed validation](testing.md#batch-validation-cut62).

The combined review reconciles shared dispatcher/CMake registrations and test
policy, including scene-authoring and acceptance-file overlaps. An unpublished
sibling change is an explicit dependency, never an implicit part of another PR's
validation.
