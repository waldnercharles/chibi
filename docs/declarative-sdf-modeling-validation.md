# Declarative SDF modeling validation

> Historical implementation record. Commands, MCP recipes, helper scripts and
> validation results below describe the recorded revision. The MCP product and
> its authoring helpers are retired. Current authoring uses saved `.chibi`/KDL
> files and [native checks](runtime-authoring.md); see [testing](testing.md) for
> admitted checks. These results do not verify the current checkout.

Implementation lives in the isolated `cwaldner/declarative-sdf-modeling` worktree.
The primary checkout and the active SDF optimization worktree were not edited.
The imported SDF prerequisite snapshot and staged compiler snapshots are retained
under `build/model-evidence` and `build/sdf-optimization-evidence`.

## Image-to-model proof

The reference is [Mike W.'s Polaroid OneStep photograph](https://commons.wikimedia.org/wiki/File:Polaroid_OneStep.jpg),
licensed CC BY 2.0. The full attribution and original image are in
[the reference folder](../projects/modeling_lab/reference/README.md).

The saved [PolaroidOneStep model](../projects/modeling_lab/content/models/PolaroidOneStep.kdl)
is a construction graph: 154 authored nodes, 26 named parts, 10 material slots,
and two public parameters. It compiles to 903 distance nodes and 982 hit nodes
within the existing 1,024-node limits. Scale and Lens Radius remain editable.
The body uses extruded profiles and rounded boxes, the lens and knob use revolved
profiles, and the film slot is subtracted geometry. Procedural materials supply
the rainbow and lens grooves. Only the two small printed nameplates sample the
photograph. Hidden surfaces are inferred.

The former `projects/modeling_lab/author-polaroid.mjs` recipe reproduced the asset from an empty project using 419 graph operations split at
complete-part boundaries under the existing 128-operation request limit.
The final independent reproduction produced byte-identical model KDL. All 17
camera-family assets matched semantically; four differed only in scene component
order or generated expression function IDs. Both generated and saved projects
passed validation and canonical formatting checks. Evidence:
`build/model-evidence/polaroid-fresh-reproduction-final`.

Inspected native Metal views:

- [Reference view](../build/model-evidence/camera-delivery/ModelPropStudy.png).
- [Front](../build/model-evidence/camera-final/PolaroidFrontStudy.png).
- [Side](../build/model-evidence/camera-delivery-side/PolaroidOrbitStudy.png).
- [Back](../build/model-evidence/camera-delivery-back/PolaroidOrbitStudy.png).
- [Independent ship study](../build/model-evidence/stage7-native-scenes/ModelShipStudy.png).

The model is recognizable and volumetric, but it is not a pixel-identical or
photorealistic reconstruction. Surface reflections, bevel highlights, proportions,
and inferred rear details differ from the photograph. The current planar HRC
lighting does not reproduce the photographed studio environment. The side-view
review corrected an overlong eyecup stalk; all delivered views show the final
geometry except the front view, where that occluded change has no visible effect.

The reference view uses 1,040 × 780 output, camera 520 × 390, focal distance 650,
instance scale 22, and the saved ModelPropStudy pose. The side/back use the saved
orbit behavior at simulation frames 269/538 and 900 × 675 output. Capture JSON
records the complete settings, work counters, and local timings. These captures
ran alongside other work and are visual evidence, not target-performance tests.

## Stage gates

| Stage | Evidence and result |
| --- | --- |
| Primitive vertical path | Sphere render is pixel-identical to the independent numerical sphere control at 900 × 630. Distance, off-center bounds, depth, save/reload, IDs, and invalid publication tests pass. |
| Solids, CSG, transforms | Independent signs/surface/step bounds tests pass for primitives, nested cuts, empty intersections, smoothing, XYZ rotation, reflection and positive nonuniform scale. Boolean and transform scenes inspected. |
| Parts and reuse | Functions, references, finite arrays, sparse overrides, rename/removal, dependency cycles and stable part paths pass. Reused engine modules and an unrelated shelf scene inspected. |
| Dynamic bounds | Four animation states and mesh overlap inspected. A 224-frame measured interval had zero source compilations and 224 parameter/bounds updates; cache hits and setter validations are separate counters. |
| Profiles | Independent sections and mesh control pass. Wing render mean absolute RGB byte error 0.0403; 0.064% of pixels differ by more than eight bytes in a channel. Polygon validity, axis/annular revolution, normals and bounds checked. |
| Part materials | Ownership, cut surfaces, ties, scoped parameters, local coordinates, source invalidation and resource limits pass. Native GPU image limits and program sharing checks pass. Material study inspected. |
| Studio workflow | Native controls exercise create, named part, dimension, reuse, material selection, overrides, Undo, save/reopen and preview. Chrome controls exercise creation, typed edits, Undo, material overrides, save/reload and orbit. Browser Redo and preview uniform changes were not separately tested. |
| Package and bounded cost | See the final package and timing results in [Modeling Lab](../projects/modeling_lab/README.md). Work counts, image parity, compiler inspection and frame timing are reported separately. |

The compiler now shares identical pure arithmetic, retains only reachable hit
resolution work, and recognizes validated rectangular polygon profiles. It does
not reassociate floating-point operations or merge distinct uniform slots.
Independent rectangle section tests and distinct part-coordinate ownership tests
cover the reductions that made the final camera fit the existing limits.

## Editor and backend limits

Model creation, typed geometry/profile sockets, Properties, material/model
pickers, reference navigation, diagnostics and orbitable Model Preview use the
existing Studio graph and kit infrastructure. The native and browser workflows
share those contracts. See `build/model-evidence/stage7-web-workflow.md`.

The dedicated Model Preview and ordinary 2.5D scene renderer show actual SDF
surfaces. The separate free-camera arena viewport still draws manual primitives;
its computed bounds and CPU picking support do not turn it into an SDF renderer.
Use Model Preview for model inspection.

An early Studio test run used the existing preferences path before isolation was
added. The earlier preference state was not snapshotted, so its preservation
cannot be verified. Subsequent native and browser checks use disposable projects
and isolated preferences.

Local validation used an Apple M4 Pro MacBook Pro, 48 GB memory, macOS 26.7,
Metal and Chrome WebGL. Linux execution was not tested. The RTX 4080 Super is
asset-generation hardware, not an assumed game performance target.

## Final integration

The final native build and full `make test` passed, including all native checks
and the whole-function audit (`full-test-delivery.log`). The final serial/worker
`make web`, native preview boundary regressions, actual Chrome authoring/orbit
workflow, canonical asset checks, clang-format 22 check, and `git diff --check`
also passed (`final-check-format-complete.log` uses the required formatter
version). The last change after the full suite was whitespace-only macro
formatting; final packaged checks use that source. Focused modeling and SDF
material/depth/batching checks passed.

The relocated Release package validated all 116 saved assets, rejected a malformed
current model without rewriting it, recovered after restoration, and launched
the camera scene for 120 frames with build tools blocked. All supplied project
and package hashes were preserved. Fifteen Release measurement runs recorded
program sharing, parameter/bounds work, image parity and frame timing; the
report explicitly records host contention and does not claim target speedups.
Final package assembly and launch after the last Studio-only input fix are
recorded in the Modeling Lab package section.

No commit, merge, push, deployment or registered-game runtime installation was
performed for this work. The preservation record is
`build/model-evidence/final-preservation.json`; all five baseline files in the
primary checkout retained their original SHA-256 hashes.
