# Modeling Lab

Editable model graphs and focused scene gates for the declarative SDF modeling layer.
All models are authored through the existing generic Chibi graph tools. Saved KDL
retains construction nodes, geometry connections, and stable IDs. Compiled shader
code and bounds are derived from those assets.

Run `node projects/modeling_lab/author.mjs --preview` after building the matching
controller. The script verifies its project root before writing. It creates
missing fixtures and validates existing assets without overwriting edits.

## Scene gates

| Stage | Scenes | Evidence |
| --- | --- | --- |
| Model source and sphere | ModelPrimitiveStudy, ModelSphereControl, AnalyticalSphereControl | Native Metal render inspected. Matched control images are pixel-identical at 900 × 630. The model ignores deliberately undersized manual proxy dimensions. Numerical tests cover distance, bounds, source IDs, save/reload, invalid input, and preservation of the previous compiled generation. |
| Solids and transforms | ModelBooleanStudy, ModelTransformStudy, ModelTransformNoRelief | Native captures inspected. Independent tests cover primitives, nested CSG, empty intersections, forward XYZ rotation, reflection, safe nonuniform scaling, smooth sublevel bounds, expansion limits, off-center entity transforms, and catalog generation recovery. |
| Parts and reuse | ModelReuseStudy | Native capture inspected. Tests cover named part paths, shared defaults, sparse overrides including explicit zero, nested geometry functions, bounded arrays, dependency cycles, rename/removal, atomic publication, and saved-source reload. Public tools also exercise parameter/asset rename, rejected edits, and removing an override to resume inheritance. |
| Runtime parameters and bounds | ModelBoundsStudy | Four fixed animation states and a live run inspected, including mesh overlap. Numerical tests cover transformed surface samples, dynamic empty bounds, invalid updates, 48 cached variants, entity reuse, cleanup, and nested instance override identities through rename. Public tools reject invalid prototype and scene overrides before publication. |
| Profiles and solid construction | ModelProfileStudy, ModelProfileControl, MeshProfileControl | Native scene and matched controls inspected. Independent tests cover analytical circle/rectangle sections, convex polygons, extrusion, revolution, filled/annular axis transitions, transforms, conservative smooth bounds, invalid profiles, functions, and canonical round trips. |
| Part materials | ModelMaterialStudy | Native and public authoring gates passed. Part/material identity, transformed coordinates, cut/smooth ownership, slot overrides, material edit invalidation, scoped parameters, and resource limits are checked separately from the later release hit-resolution timing comparison. |

Local build output and captures are in `build/model-evidence`. Stage 1 passed
`make build`, `build/test_suite --modeling`, and the existing
`build/test_suite --sdf-material` checks for surface data, perspective, mesh depth,
batching, parameter variation, and draw ordering. This is focused validation;
the final full suite, actual Web authoring, and packaged launch subsequently passed;
see the final validation record.

Stage 2 also passes those checks. Its source snapshot, before/after images, and
logs are retained under `build/model-evidence`. The renderer now extrapolates
resolved heights only across coherent planes; curves and creases retain the
frontmost measured surface. Remaining local height-shadow aliasing at sharp
creases is visible in `ModelTransformStudy`. `ModelTransformNoRelief` renders
the same geometry with local relief disabled to isolate that HRC approximation.
This modeling work does not introduce a new GI transport system.

Stage 3 passes the native build, focused model tests, public authoring/refactoring
checks, and canonical reload. Reproduce its scene with
`node projects/modeling_lab/author-reuse.mjs --preview`. The former scripted
authoring, material and override checks are retired; the recorded passes
describe the original revision.

Stage 4 passes the native build, focused model tests, public override checks, and
canonical reload. Reproduce with `node projects/modeling_lab/author-bounds.mjs
--preview`. The live run recorded 224 measured frames with zero model source
compilations, unchanged program revision, 224 render parameter/bounds updates,
1,792 cache hits, and 448 setter validations. Setter validation is counted
separately because it also validates derived bounds before accepting an update.
These are work counts on native Metal on an M4 Pro; the development build's
timings are not a target hardware or optimized release claim. Runtime selection
framing and editor interaction remain part of the Studio gate.

Stage 5 also passes the native build, focused model tests, public construction,
metadata checks, and canonical reload. Run `author-profiles.mjs --preview` for
its scene and independent triangulated wing control. The 1,020 × 690 control
images have mean absolute RGB byte error 0.0403; 0.064% of pixels differ by more
than 8 bytes in any channel. This is a full rendered-image comparison including
raster edges and HRC lighting, separate from the numerical distance checks.
The wing, bracket, vessel, and ring compile to 77, 76, 166, and 12 distance IR
nodes respectively. Inspection timing measures one compiler call after loading;
it does not measure GPU shader compilation or total asset loading.

The final image reconstruction uses the reference and attribution under
[`reference`](reference/README.md). The model will remain construction geometry;
the reference photograph is comparison material.

## Materials, editor, and cost workflows

`author-materials.mjs` constructs named parts and reusable material slots with
existing material graphs. The distance march resolves part ownership only at
the hit: union/intersection select the active operand with a left tie-break,
difference keeps the left material, and smooth union chooses the dominant one.
`author-ship.mjs` builds the independent static ship study; `author-polaroid.mjs`
reconstructs the reference prop. The complete native/Web editor workflow and
final photo comparison have their own acceptance evidence; constructing these
assets alone does not mark those gates passed.

Actual model surfaces render in the dedicated orbitable Model Preview and the
ordinary 2.5D scene path. The separate free-camera perspective arena viewport
still displays manual primitive geometry. Computed model bounds and CPU surface
picking do not make that viewport's display an SDF model renderer; use Model
Preview for surface review.

`author-cost.mjs` prepares isolated single/raw/256-instance/parameter-variant
scenes, plus `ModelCostComplex`. The complex scene uses exact angular-sector
integration of the overlapping projected sphere disks to match the continuous
area of `ModelCostSingle`; its formula, assumptions, and scale are recorded in
`model-cost-coverage.json`. It does not match proxy area, silhouette perimeter,
ray steps, or pixel counts. `ModelCostHitEntityMaterial` and
`ModelCostHitPartMaterial` use identical geometry, pose, camera, lighting, and
StudyIvory shading to isolate the additional hit resolver and binding overhead.
Release timing and visual parity remain separate from these workload definitions.

CPU inspection of these saved models passed. Counts from
`stage8-cost-inspection.json` are:

| Model | Authored nodes | Distance IR nodes | Hit IR nodes | Public/private parameters |
| --- | ---: | ---: | ---: | ---: |
| CostSphere | 3 | 4 | 2 | 1 / 0 |
| CostConstruction | 4 | 249 | 2 | 0 / 0 |
| CostHitEntityMaterial | 11 | 61 | 2 | 0 / 0 |
| CostHitPartMaterial | 15 | 61 | 67 | 0 / 0 |

The hit controls have identical compiled bounds and distance node counts. The
named version has three parts and one material slot; its larger hit program is
evaluated at the surface, not at every march step. Node counts do not establish
GPU instruction counts or frame cost.
This snapshot uses the frozen runtime Release package after common-subexpression
sharing and hit resolver lowering. These counts remain separate from the final
benchmark report. The earlier development inspection remains separately retained as
`stage8-cost-inspection-initial.json`.

`inspectGraph.model_compilation` reports authored/expanded shader counts, public
and private parameter slots, parts, dependencies, bounds, and one warmed CPU
compiler-call time. Runtime model counters distinguish source publication,
parameter/bounds updates, cache hits, and setter validation. Report GPU programs,
draw work, and matched complete-frame timing separately. See
[the implemented contract](../../docs/declarative-sdf-modeling.md) for limits and
the distinction between model XYZ rotation and runtime pitch/yaw/roll.

## Native package validation

The isolated Release distribution build and engine assembly passed, followed by
all 11 `test/package_native_test.py --package` checks. These prove relocatable
engine resources and external-project authoring, not the final modeling scene
launch. Final runtime logs: `package-build-final-orbit.log`,
`package-assemble-final-orbit.log`, and `package-tests-final-orbit.log`
under `build/model-evidence`.

Use explicit distribution `make build` followed by `tools/package_native.py`, as
documented in the contract. `make package-native` invokes the registered-game
installer even with `UPDATE_GAME_RUNTIME=0`. The former focused project check used a Python runner:

The former Python validation/benchmark helper for this experiment is retired.
Use the native controller to validate assets or request previews; see
[testing](../../docs/testing.md).

It copies the engine and project into disposable paths, checks source inclusion
and every model/material/scene, rejects a malformed saved edit without rewriting
it, and captures a packaged launch while blocking source rebuild commands. It
does not run the registered runtime installer. The model-scene package check is
passed against engine build `143c6fb8c2107fd93eccbec6b79164fcaa8f0d8b783e14e470807534057a14a7`.
It validated 116 assets (33 models, 20 materials, two shapes, 37 prototypes,
and 24 scenes), retained rejected saved source, recovered after restoration,
and produced an inspected 120-frame camera capture. Original project and engine
hashes remained unchanged. Evidence is in
`build/model-evidence/package-check-preserved/packaged-model-9if_1_r7`.
The final UI boundary package refresh also passed: build
`25d56c7e4859a87c60994e5bc5a4213b49fdc07fb3b341b298e06086ee66d9b5`,
11 package integration checks, verified manifest hashes and resource/project
readbacks, and an inspected 120-frame camera launch. Project and package hashes
remained unchanged. Evidence: `build/model-evidence/package-delivery/report.json`;
logs: `package-build-delivery.log`, `package-assemble-delivery.log`,
`package-tests-delivery.log`, and `package-delivery-final.log`.

## Image reconstruction study

The editable `PolaroidOneStep` model currently compiles from 154 authored nodes
to 903 distance IR nodes and 982 hit IR nodes, with 26 named parts, 10 material
slots, and two public parameter slots. `PolaroidOneStep-inspect.json` records
this CPU inspection. Fresh production script reproduction yielded byte-identical model source and
semantic matches for all 17 assets. The image reconstruction is an approximation:
silhouette, proportions, grooves, and reflections differ, the hidden back was
inferred, and two small nameplates use photo samples. See the
[reference and CC BY attribution](reference/README.md). Counts do not prove
pixel agreement or frame cost.

Inspected views: [main](../../build/model-evidence/camera-delivery/ModelPropStudy.png),
[side](../../build/model-evidence/camera-delivery-side/PolaroidOrbitStudy.png),
[back](../../build/model-evidence/camera-delivery-back/PolaroidOrbitStudy.png), and
[front](../../build/model-evidence/camera-final/PolaroidFrontStudy.png).
Actual native and Web authoring/orbit workflows passed; the final viewport
boundary regression passed native, Web, and actual browser gates.

## Open the study

From this worktree:

```sh
build/model-evidence/package/bin/chibi \
  --project-root "$PWD/projects/modeling_lab" --open PolaroidOneStep
```

In Properties, open Model Preview and choose Preview Model. Drag to rotate,
use the wheel to zoom, and middle-drag to pan. Preview Scale and Lens Radius
changes in the preview are temporary; edit the graph defaults to save changes.
For the composed photo scene:

```sh
build/model-evidence/package/bin/chibi \
  --project-root "$PWD/projects/modeling_lab" --play \
  --scene scenes/ModelPropStudy.kdl
```

## Release cost results

All 15 native Metal runs passed against engine build
`143c6fb8c2107fd93eccbec6b79164fcaa8f0d8b783e14e470807534057a14a7`,
before the later Studio-only viewport boundary correction. The three isolated
pairs ran AB then BA at 1080 × 740. Identical and alternating-parameter instance
scenes ran default/limit1 in both orders. Each run warmed 240 frames, excluded
another 16, and collected 224 synchronized frame samples. Raw/model sphere and
entity/part-material controls were pixel-identical in both orders.

Firm work results: 256 identical instances use one draw and upload (53,248 bytes),
one retained GPU program and one initial GPU compilation. Limiting batches to
one uses 256 draws and uploads. Alternating two parameter variants still use
256 draws with the default limit: compatible adjacent submissions batch, but
alternating parameter payloads split those batches. Both variants reuse one GPU
program. All these frozen scenes have zero measured source compilations,
parameter updates, and bounds updates.

The animated camera orbit at 600 × 450 also has zero measured source
compilations, parameter updates, and bounds updates, with one retained GPU
program and one initial compilation. The whole render loop includes one initial
parameter/bounds cache fill; it is outside the measured interval. Median frame
work was 19.931 ms, p99 24.423 ms; simulation averaged 0.01957 ms and rendering
19.67347 ms.

Timing is host evidence, not a speedup or target claim. Builds/full tests and
other applications ran concurrently; a retained WebGL tab was closed during
the instance comparisons, changing host load between orders. Process snapshots
and the close event are recorded with every run in
`build/model-evidence/release-matched-final/report.json`. The identical-instance
default median shifted from 48.095 to 26.111 ms across orders, illustrating this
confound. Do not infer a batching speedup from those numbers.

| Control | Median range (ms) | p99 range (ms) |
| --- | ---: | ---: |
| Raw sphere | 45.950–50.384 | 62.638–64.927 |
| Model sphere | 47.395–50.129 | 58.764–66.884 |
| Matched-area construction | 49.877–50.674 | 62.554–86.178 |
| Entity-wide material | 43.365–43.783 | 56.267–58.306 |
| Named parts, same material | 44.637–45.061 | 60.981–62.460 |
| Identical instances, default | 26.111–48.095 | 32.415–64.175 |
| Identical instances, limit1 | 46.033–47.093 | 57.343–66.358 |
| Alternating variants, default | 29.854–30.036 | 35.332–35.469 |
| Alternating variants, limit1 | 29.893–30.316 | 35.624–36.937 |

Simple/complex matching is continuous projected geometry area, as documented
above; ray steps, proxy coverage and raster perimeter differ. The hit comparison
isolates additional resolver/binding work with equal images, not GPU instruction
count. Separate GPU pass measurements were not collected in these uninstrumented
runs. Linux, Web cost, and actual deployment hardware timing remain unmeasured.

The final exact-source `make test` completed with ALL CHECKS PASSED
(266.8 seconds), plus the whole-function audit (1.5 seconds). The final native
and Web viewport boundary regression and actual Chrome authoring/orbit workflows
also passed. See [the delivery validation record](../../docs/declarative-sdf-modeling-validation.md)
for the scoped evidence and remaining platform/performance limits.

The final formatting-only source refresh reproduced the same package build ID
`25d56c7e4859a87c60994e5bc5a4213b49fdc07fb3b341b298e06086ee66d9b5`.
All 11 package checks passed again, and the new 120-frame capture was visually
inspected and pixel-identical to the prior delivery capture. Manifest hashes,
resource/project readbacks, and source/package preservation passed. Final logs:
`package-build-formatted.log`, `package-assemble-formatted.log`,
`package-tests-formatted.log`, and `package-launch-formatted.log`; report:
`build/model-evidence/package-formatted/report.json`. The complete standalone
format audit is `final-check-format-complete.log`.
