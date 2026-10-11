# Current status and backlog

Reconciled on 2026-10-10 against engine `f3ff608c1697f0491ac4d2ae052a864ede1200f7`
and Studio `d10222dc73e01340dd3ccd193ab016c706708ffc`, fetched from `origin/main`.
This is the starting point for choosing work. Detailed contracts stay in their
owning docs; [PLAN](../PLAN.md), the [Arena plan](arena-roadmap.md) and the
[RML migration record](studio-rml-migration.md) retain dated history.

Implementation means the reviewed source contains the capability. Coverage means
checked-in assertions exist, subject to [test admission](test-quarantine.md).
Execution evidence names the revision and checks actually run. None implies the
other two. This documentation reconciliation ran no build, runtime or UI suite.

## Implemented capabilities

| Capability | Owning implementation and checked-in coverage | Remaining boundary |
| --- | --- | --- |
| Qualified library and prototype-argument completion | [Source intelligence](../src/authoring/source_intelligence.c), [navigation cases](../test/language/navigation.c) cover library-member replacement ranges, lexical shadowing, incomplete calls and declared prototype parameters including named arguments. | See [navigation](authoring-navigation.md) for remaining context and integration limits. |
| Definitions, references, rename and calls | Shared [navigation](../src/authoring/source_navigation.c), [rename](../src/authoring/source_rename.c) and [navigation cases](../test/language/navigation.c); Studio exposes these through its text/navigation UI. | Command response and debugger/gesture acceptance are assigned below. Entity-path rewriting on scene-object rename is a separate contract. |
| Scene hierarchy | [Scene authoring contract](runtime-authoring.md), [ECS hierarchy cases](../test/tools/ecs_hierarchy.c), [scene cases](../test/tools/scene.c) and Studio's `--scene-hierarchy` cover nested persistence, parent-relative transforms, picking and undo. | Interactive Outliner move/reparent and sibling order remain assigned. Studio's pending path/parent preservation fix is a dependency below. |
| Box3D integration | [3D runtime contracts](runtime-authoring.md#simulate-3d-bodies), [physics implementation](../src/runtime/scenes/physics3d.c) and [physics cases](../test/tools/physics3d.c) include bodies, contacts, queries, compounds, joints and capsule characters. | Characters collide with moving bodies but do not yet ride them at this baseline. |
| glTF/GLB import | [Import contract and limits](rendering-3d.md#importing-gltf-models), [importer](../src/assets/model_import.c) and [import cases](../test/authoring/model_import.c). Meshes, materials, images and prototype output already exist. | Nonidentity texture transforms are assigned. Other UV sets, sampler fidelity, skinning and animation remain outside that slice. |
| Directional, point and spot shadows | [Shadow contracts](rendering-3d.md#shadows), [directional](../src/render/scene3d_shadow.c) and [local-light](../src/render/scene3d_light_shadow.c) paths, [scene3d assertions](../test/render/scene3d.c). | Point/spot shadow edges remain hard; feature-coverage fixture omissions are separate from renderer implementation. |
| Studio undo, dirty prompts and asset selection | [Acceptance inventory](https://github.com/waldnercharles/chibi-studio/blob/d10222dc73e01340dd3ccd193ab016c706708ffc/tests/acceptance/README.md) describes production history, SDL keyboard input, dirty-prompt pointer clicks and list/grid selection cases. | Asset and graph cases explicitly omit some pointer hit testing. Full debugger generation acceptance is still incomplete. |
| Browser publishing | Engine [release workflow](../.github/workflows/publish.yml), Studio [distribution workflow](https://github.com/waldnercharles/chibi-studio/blob/d10222dc73e01340dd3ccd193ab016c706708ffc/.github/workflows/publish.yml), and Celeste's player plus `/dev/` publication are implemented. | Current browser edit/save/reload/export/input and revision isolation require product acceptance. `make publish-web` is still unavailable; Actions owns publication. |

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

## Assigned work

The owner column names the coordinating chat. The new workstreams were assigned
on October 10; existing 3D starter work is listed separately. All remain
unaccepted in this snapshot. Review each actual diff and execution evidence before
changing that status. Shared SDK changes belong in the engine, interactions in
Studio, and game rules/composition in game assets.

| Owner | Repository and remaining work | Dependencies and overlap |
| --- | --- | --- |
| Build a complete Chibi Tetris game | Private [chibi-tetris](https://github.com/waldnercharles/chibi-tetris): complete single-player play, audiovisual polish and requested local co-op. | Separate from examples. Identify any reusable SDK gap explicitly; no online multiplayer dependency. No playable result accepted yet. |
| Complete rendering feature-coverage scenes | Engine: meaningful authored uses of the 17 rendering components and 3 functions in [coverage-gaps.txt](../test/fixtures/feature-coverage/coverage-gaps.txt). | Initially fixture assets; coordinate the coverage checker with 3D starter work. Do not remove engine-internal exclusions. |
| Verify controller authoring workflows end to end | Engine: command-level ranges, diagnostics, rename non-mutation, generation/backend identities and profiling accounting. | Existing controller and registry implementations; [navigation integration](authoring-navigation.md#controller-integration-status). Shared dispatcher registrations may conflict. |
| Verify published Chibi browser workflows | Engine/Studio/Celeste: deployed identities, player controls, game workspace, disposable edit/save/reload/export and revision isolation. | Existing serial publication. Preserve browser drafts; threaded mode is separate. |
| Support glTF texture UV transforms | Engine: nonidentity transforms, PBR slots and normal-map conventions, with import and rendered controls. | Owns importer/material changes. Other UV sets, sampler fidelity and animation stay outside this slice. |
| Reduce idle work in visible Studio panels | Studio: measure and reduce repeated shown-panel/target-choice construction. | Preserve revision invalidation, selection and reopen behavior. Coordinate CUT-50; stay out of camera/viewport and held panel conversion. |
| Verify Studio pointer and debugger interactions | Studio: graph/search and filtered asset pointer input, then pause/edit/step/history generations. | Owns additions to `tests/acceptance/main.c`; coordinate selectors with performance and Outliner owners. |
| Verify installed players with Chibi examples | Examples/engine: pinned compiler/player, resource closure, relocation and bounded visual acceptance for the five labs. | Existing SpaceCadet evidence does not validate these examples. Preserve model attribution. |
| Support 3D characters riding moving platforms | Engine: support-body motion/lifetime, translation/rotation, separation, filters and neutral authored coverage. | Existing physics dispatcher; no general controller redesign. |
| Add Outliner hierarchy editing | Engine shared authoring + Studio: move/reparent/order, transforms, identity, grouped history, persistence and collaboration/checkpoints. | Review Studio [#54](https://github.com/waldnercharles/chibi-studio/pull/54), pending at this snapshot, for authored path/parent preservation. It depends on merged engine #338. Coordinate with 3D starter and acceptance owners. |
| Reconcile Chibi engine and Studio gap documentation | Engine/Studio docs: this entrypoint and stale-status corrections. | Domain owners retain detailed contracts and validation evidence. No implementation depends on this documentation PR. |
| Existing `cwaldner/3d-starter` work | Engine + Studio primary checkouts: active, uncommitted starter/camera work. | Preserve those checkouts. Camera, viewport, starter assets, coverage registration, SDK exports and test policy may overlap; do not treat the working tree as merged or accepted. |

## Unassigned work and holds

The [feature-coverage exclusions](../test/fixtures/feature-coverage/coverage-gaps.txt)
also list material simulation, 2D lighting/sky/camera, visibility and optional
combat/encounter uses. They are missing authored coverage, not a claim that the
capabilities are absent. The rendering task owns only its stated group.
Other glTF limitations stay in the [import contract](rendering-3d.md#importing-gltf-models).
Prototype component-presence ranking remains a [navigation limitation](authoring-navigation.md).
Physical macOS trackpad validation and the remaining historical Studio reports
need current evidence before their status changes.

Behavior-driven conversion of the other Studio panels remains explicitly on hold
in the [migration record](studio-rml-migration.md#current-scope-2026-10-10).
Outliner interaction approval does not release it. Preserve all existing PR/ticket
holds and denied-action approval requirements, including HRC
[#173](https://github.com/waldnercharles/chibi/pull/173) and
[#187](https://github.com/waldnercharles/chibi/pull/187); this reconciliation
supplies no merge authority. Recheck eligibility before coordinated integration
under [reviewed validation](testing.md#batch-validation-cut62).

Integration must reconcile additions to shared dispatcher/CMake registrations and
test policy, and check starter/camera, scene-authoring and acceptance-file
overlaps. An unpublished sibling change is an explicit dependency, never an
implicit part of another PR's validation.
