# Declarative SDF modeling

Models are editable KDL construction graphs stored in `content/models`. Their
asset domain is `model`. The compiler derives a distance program, conservative
local bounds, parameters, dependencies, and named part identities. The authored
graph remains the source; no generated shape KDL is saved beside it.

The validation sections below record earlier implementation revisions and do not
verify the current checkout. Stages 1–6 passed their native build, focused modeling, public authoring,
and inspected scene gates. Stage 5 includes independent mesh controls. Stage 7
passed actual native and Web authoring/orbit workflows; the final viewport
boundary regression passed its native, Web, and actual browser gates. Packaged launch and
bounded cost results are reported separately below. This document
does not establish backend readiness or target performance.

## Authoring and binding

Edit saved model, material, prototype and scene KDL in the project, or use
Studio. Inspect the native capability catalog before choosing nodes. Models
need a 3D scene preview; a flat material preview cannot represent model
surfaces.

Geometry wires carry the compiler-only nominal type `Geometry3D`. They describe
geometry that can be evaluated at another position. They do not carry a sampled
distance, an ECS entity, or a serialized process pointer. Connect the finished
geometry to `Model Output`; required geometry inputs cannot be left empty.
`Profile2D` is a separate construction type used by extrusion and revolution.

Bind a model through the existing `Shader.shape` field. Model and raw shape
names share this reference namespace; ambiguous names are rejected. A model's
computed bounds are authoritative. `Geometry3D` dimensions continue to bound
raw shapes but do not resize a model. Change model dimensions through its
parameters, and change instance scale through the visual transform.

Projects use `CHIBI_PROJECT_VERSION` 24, as created and loaded by
`src/assets/project.c` using `src/assets/project.h`. The graph document's own serialized
`version` is a separate format field; do not replace it with the project version.

## Coordinates and operations

Model coordinates use local project units, with X/Z horizontal and Y vertical.
Primitives are centered at the origin unless their contract says otherwise.

| Node | Dimensions and surface contract |
| --- | --- |
| Model Sphere | Positive `Radius`. |
| Model Box | Positive outer `Half Size` along X, Y, Z. |
| Model Rounded Box | Fixed outer `Half Size`; `Radius` is zero through the smallest half size. Rounding does not enlarge the box. |
| Model Cylinder | Y axis, positive `Radius` and `Half Height`; includes caps. |
| Model Capsule | Centered Y segment, positive `Radius`, nonnegative `Half Segment`. |
| Model Torus | Ring around Y; positive `Minor Radius` smaller than `Major Radius`. |
| Model Translate | Moves the complete child geometry by `Offset`. |
| Model Rotate | Radians, forward rotation around X, then Y, then Z. |
| Model Scale | Positive `Factors`; minimum factor times the inverse-scaled child distance gives a conservative step bound. |
| Model Reflect | Reflection through the origin across the selected X, Y, or Z axis. |
| Model Union | Minimum of child signed distances. |
| Model Intersection | Maximum of child signed distances; disjoint bounds may be empty. |
| Model Difference | Removes B from A; bounds remain within A. |
| Model Smooth Union | Positive `Smoothness`; polynomial smooth minimum with a proven bounds envelope. |

Nonuniform scaling preserves the zero surface and safe stepping; its value is
not an exact Euclidean distance. Zero and negative scale are rejected. Use
reflection for mirroring. Bounds support off-center objects and expand for
smooth joins, including a smooth join that revives an otherwise empty
intersection.

Model rotation and runtime entity rotation are separate contracts.
`VisualTransform.pitch/yaw/roll` uses the existing `visual_rotation`
mapping: yaw is about Y with angle `-heading - yaw`, pitch is about Z, and roll
is about X; the quaternion composition is `yaw * pitch * roll`. Those fields
must not be interpreted as the model node's X/Y/Z rotation sequence.

## Parts, reuse, and parameters

`Model Part` labels a geometry subtree. Its identity uses the asset, stable
reference/array instance path, and authored node ID. Renaming changes the label,
not the identity. Geometry functions use the existing pure function machinery
with typed geometry arguments and returns. References and functions must be
acyclic.

`Model Instance` references another model definition. Overrides are sparse and
identified by the target parameter declaration ID. An absent override inherits
the referenced default, including later default edits. Explicit zero is a value,
not absence. Removing an override restores inheritance; parameter rename
refreshes its label without changing its declaration identity.

`Model Parameter` exposes a scalar or ShaderVec2/3/4 value with a readable name,
stable node ID, default, minimum, and maximum. Ranges and defaults must be finite
and ordered. Supported numeric expressions can drive geometry inputs. Runtime
values must also satisfy the operation contracts: a positive declared interval
alone does not make every combination of radii or offsets valid.

The shared `Set Shape Parameter` behavior node updates one entity's shape/model
parameter. It validates the proposed parameter snapshot and derived bounds
before accepting the change. Public uniform changes reuse geometry code;
structural edits recompile. `Shader.parameters` is a KDL object value in
a reflected string field. `shape.<name>` and `material.<name>` disambiguate the
two binding namespaces.

`Model Linear Array` places copies at `i * Step`, beginning at index zero.
`Model Radial Array` rotates copies by `i * Angle Step` around the selected axis
through the origin. Translate its child first to give the ring a radius. Counts
are structural integers from 1 through 64. Nested arrays can still exceed the
compiled program budget; source reuse does not guarantee low evaluation cost.

The existing limits remain 32 compiled parameter slots and 1,024 shader nodes.
Private derived slots also consume that parameter budget. Excessive expansion,
invalid geometry, nonfinite arithmetic, missing references, and cycles produce
source diagnostics rather than silently increasing limits.

## Profiles and solid construction

Stage 5 native construction and independent mesh-control checks have passed:

- `Model Profile Circle` uses positive `Radius` and a numeric ShaderVec2 `Center`.
- `Model Profile Rectangle` uses positive ShaderVec2 `Half Size` and `Center`.
- `Model Profile Polygon` takes a literal structural ShaderVec2 `Vertices`
  array of 3–32 finite, distinct vertices. List them in strictly convex
  counterclockwise order and omit a repeated closing vertex. Collinear corners,
  degenerate edges, self-intersection, and reversed winding are rejected.
- `Model Extrude` maps profile X/Y into model X/Y and gives it centered positive
  `Half Depth` along Z.
- `Model Revolve` maps profile X to radius and profile Y to height, making a full
  revolution around model Y. The complete profile must have nonnegative radial
  coordinates, including the radius/center extent of a circle.

Vertex count and polygon coordinates are structural source data. Open or
concave profiles, arbitrary sweeps, and lofts are outside this contract.

## Publication and invalid edits

Derived generations publish atomically after dependency linking, construction
validation, bounds derivation, and compilation succeed. A failed edit retains
the last successful runtime generation. It does not authorize shipping the
invalid current source. Authored source remains available for correction; the
editor labels stale previews and preserves source; native and Web invalid-edit
workflows passed.

## Part materials and preview controls

`Model Material Slot` declares a stable named binding with a default existing
Material asset. Connect its compiler-only `Slot` output to a `Model Part`'s
optional `Material` input. An unassigned part uses the entity material. Referenced
models expose sparse material-slot overrides by declaration identity; removal
restores the shared default. Numeric parameter names remain scoped to their
material binding. Inspect the returned `model_material_slots` rather than
guessing a nested instance's flattened slot name.

Ray marching evaluates distance. Once a surface is found, a separate derived
hit program resolves the owning part and its local coordinates, then the selected
material is shaded. Union chooses the smaller distance, intersection the larger;
ties choose the left operand. Difference inherits the left operand even on a cut
surface. Smooth union chooses the dominant operand rather than blending material
graphs. A transformed part retains its construction-local procedural coordinates.
There is no UV unwrap operation in this contract.

Material-only edits can update bindings without recompiling unchanged geometry.
The model supports at most 32 material slots and 32 combined material parameter
slots across its entity fallback and slot bindings. Resource validation rejects
more than eight unique image variants or 512 ramp vec4 resources. These are
separate budgets from the model geometry's 32 parameter slots. Inspect and test
the chosen content rather than assuming that repeating a material is free.

Studio's model source family, geometry/profile sockets, parameter editors,
reference pickers, dependency navigation, diagnostics, and orbitable model view
use the existing editor interfaces. Preview parameter overrides are temporary
view controls; saving construction remains a graph edit. Native and Web user
workflow acceptance is tracked separately from compiling these interfaces.

Use the dedicated orbitable Model Preview or the ordinary 2.5D scene renderer
to view the actual SDF surface. The editor's separate free-camera perspective
arena viewport still uses its existing manual-primitive drawing path; it does
not march model SDF geometry. Its model framing uses computed bounds and its
CPU picking can intersect the model surface, but its displayed primitive is
not the constructed model. That display mode remains unsupported for model
surface review.

The Web Export Project guard has a saved-source validator prepared for the
packaging gate. It reads model files and project dependencies, rejects invalid
current source with an asset/node diagnostic, and does not mutate or publish
them. Its presence is not a claim that Web export acceptance has passed.

## Scenes, inspection, and evidence

The Modeling Lab studies of primitives, Boolean operations, transforms, reuse,
bounds, profiles, ships, materials and construction costs were retired with the
example projects on 2026-10-17 ([examples](examples.md)); no checked-in project
saves a model now, and `--model-graphs` and `--model-catalog` cover models in
memory. The former `author*.mjs` recipes and their `--preview` flags are retired.
Retained `build/model-evidence` results are historical evidence. The local HRC relief
approximation still shows aliasing at sharp creases; the relief-disabled control
isolates that from model geometry.

Runtime model counters distinguish published CPU source compilations, cached
render parameter updates, bounds updates, cache hits, and setter validations.
Setter validation can evaluate bounds before a value is accepted and is counted
separately from render snapshot work. Program/parameter revisions show invalidation.
These counters do not measure GPU program count, ray steps, draw submissions,
or whole-frame time; report those separately with matched workloads. The
4080 Super is asset-generation hardware, not a game performance target.

## Isolated package validation

Prepare authored model/material/scene assets with a pinned installed compiler SDK.
Deploy its prepared artifact and declared resources alongside a separate verified
player package:

```text
make package-player PLAYER_OUTPUT=build/model-evidence/player
/installed/sdk/bin/chibi-compile --project-root /absolute/project \
  --output /absolute/output/game.chibip
build/model-evidence/player/bin/chibi-player /absolute/output/game.chibip \
  --resource-root /absolute/output
```

Packaging leaves games untouched. Compiler preparation and package integrity do not establish
successful rendered model behavior; use affected runtime/GPU checks.
See [native packages](native-package.md) for product boundaries and exact pins.

The evidence below predates the separate SDK/compiler/player contract and
records the retired combined package. It is historical, rather than acceptance
of the current packaged runtime.

The isolated native Release distribution build and assembly passed, followed by
11 native package integration tests. Final runtime logs are retained as
`package-build-final-orbit.log`, `package-assemble-final-orbit.log`, and
`package-tests-final-orbit.log` under `build/model-evidence`.
The saved modeling-project check passed against build
`143c6fb8c2107fd93eccbec6b79164fcaa8f0d8b783e14e470807534057a14a7`:
116 saved assets validated, malformed source was rejected and retained, restoration
recovered, and the relocated engine rendered an inspected 120-frame camera capture.
Project and package hashes remained unchanged. Retained evidence is in
`build/model-evidence/package-check-preserved/packaged-model-9if_1_r7`.
The original isolated check used a Python runner:

The former Python validation/benchmark helper for this experiment is retired.

This check relocates the package, copies source and dependencies, validates
models/materials/scenes, verifies invalid-source rejection and recovery, and
captures a fixed-frame packaged launch. It preserves the supplied project and
package, does not install registered game runtimes, and labels visual inspection
separately from successful capture.

The current image reconstruction study, `PolaroidOneStep`, has 154 authored
nodes, 903 distance IR nodes, 982 hit IR nodes, 26 named parts, 10 material slots,
and two public parameter slots. This CPU inspection is retained in
`build/model-evidence/PolaroidOneStep-inspect.json`; the reference comparison is an approximation, with inferred hidden surfaces
and two small nameplates sampled from the photo. Silhouette, proportions,
grooves, and reflections differ from the reference. Fresh script reproduction
produced byte-identical model source and semantic matches for all 17 assets.

The earlier Release comparisons used packaged resource mounts.

The former Python validation/benchmark helper for this experiment is retired.

Repeat in reverse scene order into a new output directory, then alternate both
orders. Apply the same protocol to raw/model sphere and simple/complex controls;
compare instances with the default batch limit and `--sdf-batch-limit 1` using
separate alternating calls. Wait for other builds, captures, and GPU work to stop.
The existing benchmark warms 240 frames and excludes another 16 before recording
224 synchronized samples; pacing sleep is outside measured frame work. Keep
instrumented pass timings separate. A distribution binary launched directly from
`build-package` lacks packaged shader mounts; a missing `/chibi-render` resource
in that setup is not evidence of a renderer regression.

The Stage 8 Release study completed 15 native Metal runs, with both AB/BA orders
for raw/model, simple/complex, and entity/part-material controls. Raw/model and
hit-material controls were pixel-identical in both orders. Identical 256-instance
submissions batch to one draw/upload and one retained GPU program; alternating
two parameter variants still split into 256 draws under the existing adjacent
compatibility rule. Measured frozen and animated rigid-motion intervals have
zero source compilations, parameter updates, and bounds updates. Animated orbit
retains one GPU program and one initial GPU compilation; initial cache fill is
outside the measured interval. Complete counters and frame distributions are in
`build/model-evidence/release-matched-final/report.json` and the modeling lab
README. Concurrent tests/apps and a WebGL tab closure changed host load during
runs, so these timings do not establish a speedup, quiet benchmark, Linux/Web
performance, or target readiness. The final Studio-only package refresh is
reported separately from this renderer-identical benchmark build.

The final Studio boundary package refresh passed against build
`25d56c7e4859a87c60994e5bc5a4213b49fdc07fb3b341b298e06086ee66d9b5`:
11 native package integration checks, verified engine manifest payload hashes,
resource/project-root readbacks, and an inspected 120-frame camera launch, with
project and package hashes unchanged. Evidence is
`build/model-evidence/package-delivery/report.json`; delivery build/assembly/test
logs are `package-build-delivery.log`, `package-assemble-delivery.log`, and
`package-tests-delivery.log`. The earlier 116-asset validation and Release
benchmark records retain their precise earlier build identity; the later change
only fixes Studio pointer handling. Final exact-source `make test` reported
ALL CHECKS PASSED (266.8 seconds), followed by the whole-function audit
(1.5 seconds). Native, Web, and actual Chrome authoring/orbit boundary gates
passed. The separate delivery validation record records these scopes.

The final formatting-only source refresh reproduced the same package build ID
`25d56c7e4859a87c60994e5bc5a4213b49fdc07fb3b341b298e06086ee66d9b5`.
All 11 package checks passed again, and the new 120-frame capture was visually
inspected and pixel-identical to the prior delivery capture. Manifest hashes,
resource/project readbacks, and source/package preservation passed. Final logs:
`package-build-formatted.log`, `package-assemble-formatted.log`,
`package-tests-formatted.log`, and `package-launch-formatted.log`; report:
`build/model-evidence/package-formatted/report.json`. The complete standalone
format audit is `final-check-format-complete.log`.

## Saved-source mesh baking

Studio's Bake bundle action bakes a saved model. Save the model first. The
service reads a fresh deterministic catalog, checks model/material bindings,
prepares one parameter snapshot and extracts a bounded triangle approximation.
It never replaces a model graph or changes a scene's rendering choice. Studio
includes checked applied public parameter values. Its budgets are a cell size
and maximum triangles, samples and work.

The destination must be new and its parent must exist. The backend stages all
four outputs beside it, validates the engine mesh, then publishes the directory
with an exclusive rename. An existing bundle is rejected and retained. Invalid
source, parameters, numeric geometry or exhausted budgets publish no bundle.
Native Studio reports the output directory; Web Studio downloads the four files
as `<Model>-mesh.zip` through the existing browser file export hook, including
when using the local server storage bridge.

The bundle contains `model.mesh`, `model.obj`, `model.mtl` and
`provenance.json`. `model.mesh` is the engine's binary indexed mesh (see
[3D rendering](rendering-3d.md#meshes)), with tangents generated from the box
UVs. It retains named part identities, stable source
node IDs, invocation paths, live material asset references and slot scopes, owner-local vertex
coordinates and the nine row-major object-to-part coordinate gradients. Its
optional source `uv_bounds` restores automatic box projection per fragment.
The ordinary imported mesh renderer uses those groups and frames for live
material graphs, including coordinate-based bump detail. The optional group
`material_scope` preserves qualified overrides such as
`material.ImageSlot.Roughness` when the same material is reused across parts;
geometry and other-slot bindings are not consumed as material parameters. Use `MeshAsset`
with dimensions `(1,1,1)`, retain the entity's material binding for unassigned
parts, and enable `MeshSurface2D` instead of `SdfSurface3D` to select the
fallback. Copy `model.mesh` into project content and bind a source such as
`content/meshes/Camera.mesh`; absolute mesh paths, parent traversal and other
extensions are rejected by the importer. The editable model remains available for
subsequent changes/rebakes.

OBJ carries engine XYZ geometry, vertex normals, approximate box UVs and named
material slots. In Blender import with Y-up and negative-Z forward; this maps
engine `(x,y,z)` to Blender `(x,-z,y)`. The inverse engine export conversion is
`(x,z,-y)`. MTL slots are neutral placeholders. Arbitrary image/procedural,
roughness, bump or normal graph appearance is **not** baked into Blender
materials or textures. Those shaders remain live in the engine's mesh groups;
keep their referenced assets/images with the engine project. OBJ has no UV
unwrap, and its interpolated vertex UVs can differ from per-fragment automatic
box mapping near dominant-normal transitions.

Geometry uses deterministic marching tetrahedra over the prepared conservative
bounds, padded by one cell. `cell_size` is measured in authored model units;
zero selects the longest extent divided by 48. Default limits are 128 cells per
axis, 100,000 triangles, 2,000,000 evaluated queries, 1,000,000,000 estimated node
work and 10 edge refinement iterations. Explicit limits are bounded by the
public tool schema and backend. A bake makes at most 166,496 triangles
(`MODEL_BAKE_TRIANGLE_MAX`), the most a mesh file holds when every triangle
owns its three vertices. Work includes conservative hit/frame charges;
it is not a GPU instruction count. The baker drops microscopic triangles at
the same absolute area floor used by the mesh importer. Thin features smaller
than the grid can be
lost. Faces choose one owner at their centroid; material boundaries are an
approximation and `boundary_triangles` records corner-owner disagreement.
Large nested vertex/coordinate/frame streams are parsed with sequential iterators,
so import validation scales with the stream length. Provenance records
saved-source hashes, prepared parameters, bounds, resolution,
budgets and residuals. `max_sampled_residual` measures emitted vertices against
the source distance field; it is not a global surface error guarantee.

Current focused checks passed CPU sphere/cavity geometry, winding, deterministic
output, named owner frames, budget failure preservation and saved-source export.
The Metal regression compares
live SDF and baked mesh scene/albedo/normal targets at four interior samples
using two named image materials, asymmetric automatic box UV, a reflected part
frame and bump detail. That focused fixture passes; the subsequent qualified
material-override regression has its own gate in the authoring validation record.
These checks do not assert identical silhouettes from a finite mesh. The browser ZIP regression checks all four byte streams, source
retention and incomplete-bundle rejection. Actual Web bake/download and camera
fallback visual gates are reported in the separate authoring validation record.

Blender 5.2.2 LTS imported the public Release `ModelSphere` bake with 10,884
triangles, one mesh/material slot and exact ±30 axis bounds. It also imported
the fine `PolaroidOneStep` bundle with 108,413 triangles and 26 mesh objects and
material slots, preserving the documented axis conversion and saving
`imported.blend`. OBJ appearance remains neutral; live engine shaders are not
converted to Blender materials.

The camera exposed two practical failures before the final export. Repeated
indexing of nested JSON vertex streams made import validation quadratic; the
parser now uses sequential iterators, tested with 12,288 varied vertices and an
invalid final frame. Microscopic corner triangles passed the baker's relative
angle test but failed the importer's absolute area floor. Both now use the same
floor, with a near-grid translated-box bake/actual-import regression.

At cell size 0.2 the default 100,000-triangle budget rejects the camera without
publishing output. With explicit budgets (cell size 0.2, 150,000 triangles,
8,000,000 samples and 10,000,000,000 work), final public export
passes and retains SHA256 hashes for all 194 saved input files. It produces
108,413 triangles, all 26 nonempty named parts and all ten material references
and scopes. The grid is 56×45×52; it charges 5,880,177 queries and 6,114,067,124
node work, records 5,627 boundary triangles, and reports maximum sampled residual
0.0019612 authored units. Compact engine JSON is 70,073,037 bytes, below the
128 MiB importer limit. The complete Release public operation took 755.38 seconds
under concurrent CPU load. This includes startup, saved-source validation,
compilation, extraction and export; it is not a quiet extraction benchmark or
runtime frame-time claim.

The historical matched native engine views used disposable scenes authored
through the former MCP transport, with the
same ModelPropStudy camera, lighting, HRC, pose and 120 fixed frames at 3840×2160.
The failed cell-size-0.4 control has large apron gaps and misses the thin roller.
Cell size 0.2 fills those gaps and retains every nonempty source part, labels and
rainbow detail. Visible faceting and centroid material-border zigzags remain at
the lens, viewfinder, nameplates and thin slot. The mesh is a documented
approximation; these images do not prove pixel or material-boundary equivalence
or production quality for close views.

Evidence is in `build/authoring-evidence/bake-public-release-fine/summary.json`,
its `PolaroidOneStep/` bundle and Blender report,
`blender-release-camera-fine.log`, and `bake-camera-parity-fine/`. Compare
[the SDF view](../build/authoring-evidence/bake-camera-parity-fine/ModelPropSdfParity.png)
with [the fine mesh view](../build/authoring-evidence/bake-camera-parity-fine/ModelPropMeshParity.png).
The coarse failed-quality images remain in `bake-camera-parity/`. Public sphere
Release evidence is in `bake-public-release/ModelSphere/`. The focused GPU
material test is `sdf-material-integration.log`. Full delivery and current native/Web UI gates
are recorded separately in the authoring validation document.

Synchronous offline baking can be expensive, especially in a debug/ASan build
or with concurrent CPU tasks. Keep bake resource options explicit and report the
selected workload and actual result. A failed bake does not lower
resolution or replace editable source.
