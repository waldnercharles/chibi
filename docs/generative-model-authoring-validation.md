# Generative model authoring evidence

> Historical implementation record. Commands, MCP recipes, helper scripts and
> validation results below describe the recorded revision. The MCP product and
> its authoring helpers are retired. Current authoring uses saved `.chibi`/KDL
> files and [native checks](runtime-authoring.md); see [testing](testing.md) for
> admitted checks. These results do not verify the current checkout.

This implementation extends the declarative model workflow used for the Polaroid image reconstruction. The original reconstruction and Blender comparison remain available. New capabilities have separate authored examples so their effect can be inspected independently.

## Delivered capabilities

| Capability | Examples |
| --- | --- |
| Bump and tangent normal maps, planar/cylindrical mapping, triplanar image sampling, bounded decals | `ModelSurfaceDetailStudy`, `ModelMappingDetailStudy` |
| Profile Boolean operations, rounded/beveled extrusion, field offsets and shells | `ModelConstructiveRims` |
| Concave and sampled Bezier profiles, bounded circular tube sweeps, pivot transforms | `ModelConstructivePaths` |
| Named attachment frames and reusable instance exports | `ModelAnchorStudy` |
| Reference image alignment, orthographic views, Part isolation, section cuts and separate geometry/material costs | Studio's Model Preview and Model Resources panels |
| Bounded mesh extraction, live material and coordinate preservation, OBJ/MTL and provenance | The recorded `bakeModel` operation and Studio's Bake Mesh Bundle; current native operation: `bake_model` |

The source examples were generated through the now-retired `projects/modeling_lab/author-detail.mjs` and `author-constructive.mjs` recipes. Their saved assets remain available; no replacement recipe recreates them from an empty project. See [the authoring guide](generative-model-authoring.md), [surface detail contracts](surface-detail-authoring.md), and [declarative model contracts](declarative-sdf-modeling.md).

## Geometry reduction

| Camera | Source nodes | Distance IR | Hit IR | Parts |
| --- | ---: | ---: | ---: | ---: |
| Preserved `PolaroidOneStep` | 154 | 903 | 982 | 26 |
| New `PolaroidSurfaceDetail` | 136 | 833 | 896 | 22 |

The new camera moves four small marks into materials and adds surface detail. The smooth, bump and normal-map tile examples share a three-node model with 17 distance IR nodes; the modeled-ridge control has 29 source nodes and 155 distance IR nodes. Material sampling increases, so these reductions do not establish an overall frame-time improvement.

## Captures and editor workflows

Seven native scenes were rendered and visually inspected. PNGs and structured reports are in `build/authoring-evidence/scenes/`; observations are in `build/authoring-evidence/scene-visual-review.md`. Fine detail aliases at this preview resolution. These are correctness and appearance captures, not target-hardware benchmarks.

The browser workflow uses a disposable copy of Modeling Lab under `build/authoring-evidence/ui-workspace`. Reference selection, alignment, save/reload, orthographic views, named Part selection, section controls and mesh export were exercised through the visible editor. A real coarse `DetailTile` download contains 318 triangles and all four bundle files; the copied ZIP and inspection report are `web-tile-mesh.zip` and `web-bake-report.json` in the evidence directory.

An oversized reference exposed missing viewport clipping. The corrected overlay uses RmlUi's explicit clip contract; the final browser screenshot `web-reference-clipping-passed.jpg` shows it clipped at all viewport edges. Native controls also passed with an oversized reference. The native Bake Limits workflow verifies that 150,000 triangles, 3,000,000 sample queries and 3,000,000,000 node work reach exported provenance. Camera export can use the larger explicit budgets below. The worker Web build launched and rendered the camera in an isolated browser page (`web-worker-camera.jpg`).

The final browser build also rendered the shortened Limits labels and accepted a 3,000,000,000 work budget; its export action reported a created 318-triangle bundle (`web-final-bake.jpg`). The in-app browser did not expose a download event or a new file during this last check, and Chrome is unavailable through the browser connector. Final-build ZIP delivery to disk is therefore unconfirmed. The earlier downloaded bundle, native service/provenance checks and ZIP serialization tests are separate passing evidence; the earlier ZIP is not presented as a final-build download.

Actual browser use caught a GLSL ES reserved identifier and a stale texture binding after render-target resizing. Both were fixed; the exact camera preview reproduction subsequently rendered successfully. `web-preview-crash-diagnosis.md` records the fault mapping and verification. The native GLES harness skips on this Mac because SDL cannot create its GLES window; the skip is not counted as a passing GLES test.

## Scope of the result

Bump and normal maps affect shading, not silhouettes or occlusion geometry. Existing HRC Height relief remains a separate output. HRC transport remains planar.

Sampled profiles and tube paths have explicit bounds. Shell/offset thickness is field-based and can vary physically after nonuniform scaling. Mesh extraction can lose features below its cell resolution; residual measurements are sampled diagnostics, not a global error guarantee. Engine meshes retain live material slots, qualified parameter scopes and Part frames. OBJ/MTL has neutral placeholders and does not translate engine procedural materials into Blender shading.

## Mesh comparison

The original camera was exported through the public `bakeModel` operation, then imported into Blender 5.2.2 LTS. The source hashes remained unchanged. SDF and imported engine mesh were captured with matching scene/camera/light settings at 3840 by 2160 after 120 fixed frames.

| Bake | Triangles | Result |
| --- | ---: | --- |
| Cell size 0.4 | 23,088 | Imports, but fails the visual quality check: the thin sloped panel has gaps and the roller disappears. Retained as a failed control. |
| Cell size 0.2 | 108,413 | Restores the panel and all 26 source parts. All ten material scopes remain. Faceting and jagged material boundaries are still visible at this close view. |

The finer bake used limits of 200,000 triangles, 8,000,000 sample queries and 10,000,000,000 node work. Actual counts were 5,880,177 queries and 6,114,067,124 node work; maximum sampled field residual was 0.001961. Its compact engine JSON is 70,073,037 bytes. Sequential parsing replaces repeated linked-array lookups, and bake/import share the same minimum triangle-area rule. Regression checks cover large aligned attribute streams and tiny generated triangles.

The engine mesh retains the authored shader references, images and Part frames. Blender imported all 108,413 triangles into 26 objects, with neutral MTL placeholders. This verifies interchange geometry; engine shader appearance is not transferred to Blender. The exported mesh remains an approximation and does not establish pixel or material-boundary equivalence.

Evidence: `build/authoring-evidence/bake-public-release-fine/summary.json`, `bake-public-release-fine/PolaroidOneStep/imported.blend`, and the two PNGs in `bake-camera-parity-fine/`. The 755-second public operation includes process startup, validation, compilation and export under substantial concurrent CPU load; it is not a quiet bake benchmark.

## Validation scope

- All `make test` Python/Node preflight checks and CMake generation, reflection, reload, development, MCP, canonical-asset and behavior-backend gates passed. Initial combined invocations exposed a missing catalog entry, a tool-count expectation and three noncanonical new assets; these were fixed and the affected gates rerun. This is completion of the suite's components, not a claim that the initial combined commands exited successfully.
- Full default native suite: passed (`full-native-suite.log`, ending `ALL CHECKS PASSED`). Whole-function audit: passed (`whole-function-audit-final.log`).
- Focused surface-detail GPU readbacks, SDF material/scope checks, public authoring/controller operations, and model export passed. Native Studio controls, save/undo, reference alignment, Part/section controls, resource inspection and real export passed. The final budget controls preserve values wider than 32 bits in provenance.
- Native and both serial/worker Web builds passed. Actual browser checks include large-model rendering, reference save/reload and clipping, Part/view/section inspection and a real ZIP download. Native GLES-window creation is unavailable on this Mac; that harness skipped, while the actual browser paths rendered successfully.
- Seven authored scenes and the two matched camera captures were visually inspected. Graph/IR counts, sampled residuals and observed image quality are reported separately from performance.

The implementation remains uncommitted in the existing declarative-modeling worktree. The original Modeling Lab assets, baseline Git index and HEAD remain unchanged; new examples are separate assets. There is no Linux runtime test or target-GPU performance result. The 4080 Super is asset-generation hardware, not an assumed game target.
