# Clay SDF: scene3d bridge design

Source inspection: `07eb2abaa5a097352c472746b2aadf2f97cd5e4e` (19 October
2026). This is a proposed implementation boundary, not renderer implementation
or validation evidence. No build, shader compilation, runtime test, capture, or
performance measurement was run for this document. P2 remains blocked on the
clay plan's G1 field-correctness gate and applicable toolchain/test admission.

## Decision

Add bounded **per-object SDF draws inside the existing scene3d submission**.
Use the existing depth-enabled scene-linear HDR target and post-processing.
Do not introduce a separate renderer, composite planar HRC output into scene3d,
or bake a mesh and describe that as direct SDF integration.

For the first proof, admit one compiled analytic model, one fallback opaque
material, an ordinary mesh/primitive control, and a perspective camera. Expand
that proof to multiple model parts/instances, a directional shadow and picking
before claiming G2. The initial scene disables GI, screen light, ambient
occlusion, volumetric fog, transparent/masked SDF materials, local-light
shadows, and SDF LOD switching. Unsupported enabled combinations must report a
specific diagnostic; they must not silently use the proxy as the surface.

Use a conservative projected-bound rectangle as the raster carrier, with a
full-viewport fallback for an uncertain near-plane projection. Trace only the
ray interval inside the model bound and camera clipping interval. This remains
one bounded draw per object, without a whole-scene field union. It avoids the
extra correctness cases of a cube carrier disappearing when its faces are
clipped while a surface inside it remains visible. A cube carrier is a later
measured optimization, not a prerequisite.

## Existing interfaces and actual gaps

| Existing source | Reuse | Missing bridge or restriction |
| --- | --- | --- |
| `render/scene_renderer.c` | `app_scene_renderer_draw3d`, retained output, pixel size, HDR/post handoff | No new composition target is needed |
| `render/scene3d.c` | Visible entities, lit draws, material rows, shadow caster lists, resource preflight, draw ordering | `collect_entity` currently ignores `SdfSurface3D`; it collects the ordinary `Geometry3D` visual instead |
| `render/scene3d_visual.c` | Entity enumeration, interpolated placement supplied by the engine, visual transform validation, conservative box visibility | Model bounds must replace primitive dimensions for SDFs; preserve model units |
| `render/mesh_surface.c` | `render_mesh_surface_build_with_bounds` already creates a transformed bound for a model, including off-center bounds and `Transform3D` | Its cube is a proxy, not drawable field geometry; the prepared bound can be supplied to avoid another model snapshot |
| `render/scene3d_camera.c` | Perspective/orthographic camera, eye-relative rays, near/far unprojection, clipped screen bounds | An SDF fragment needs the same ray and hit-depth convention, including viewport/aspect changes |
| `render/shader_catalog.h` | `render_shader_model_snapshot`, `render_shader_model_material_snapshot`, hit/frame lookup, shader resource set, revision/counter APIs | Extraction must freeze these results for the frame; snapshot caches mutate shared state |
| `runtime/models/model_program.h` | Prepared geometry parameters, derived bounds, hit part/material, part-position Jacobian | G1 must establish the admitted distance/termination/query contract; an available evaluator is not a safe marcher |
| `render/shader_program.c` | Generated typed graph GLSL, image/ramp binding, shape specialization, retained last-good GPU programs | Add scene3d SDF color and depth program kinds; existing SDF programs produce planar MRT outputs |
| `render/shaders/sdf_material.frag` | Object-space intersection structure, hit/part resolution, analytic/fallback normals, material frames | Its camera/depth formulas and `surface_material.shd` outputs belong to the planar path |
| `render/shaders/scene3d.frag` | GGX, light/sky/shadow response, surface graph outputs, scene-linear clamp | Factor the hit-independent material/lighting portion so meshes and SDF hits use one implementation |
| `render/scene3d_shadow.*` | Cascade fit/reach using bounds, off-screen casters, depth atlas, `render_scene3d_shadow_read` matrices | Actual SDF intersection must write each light-space hit depth, even for an unmasked material |
| `render/scene3d_pick.c` | Already has model-aware bounds and CPU model ray picking | Its heuristic marcher and exact-world pose are not evidence of displayed-surface parity |

The last point corrects a possible overly broad reading of the plan: picking
does exist. In fact, a model can currently be picked as an SDF while scene3d
draws its ordinary primitive. The bridge must remove this disagreement rather
than introduce a second independent picker.

## Extraction and resource ownership

1. Recognize an enabled `SdfSurface3D` before ordinary visual collection.
   Keep `Shader.shape` as the resource reference and `Geometry3D` as the
   existing entity admission component. Compiled model bounds are authoritative;
   its width/height/depth must not scale a model. Retain legacy 2D placement and
   `Transform3D` precedence through `render_scene3d_visual_frame`.
2. Resolve only G1-admitted models initially. Diagnose missing/non-model
   references, unsupported operations, conflicting mesh/terrain/particle
   visuals, unsupported alpha modes and nonempty LOD settings. Empty validated
   bounds mean no surface. Singular/invalid transforms follow the existing
   explicit flat/invalid distinction.
3. Extract SDF snapshots **serially**, outside `collect_range`. The current
   model snapshot API grows per-asset caches/caller arrays and changes counters;
   calling it from all geometry workers would create a new race. Ordinary
   geometry can retain its existing parallel path. Do not disable parallelism
   globally merely because an SDF is present.
4. Retain one immutable frame record per visible instance or required caster:
   entity generation, catalog identity/revision, object/world transforms,
   prepared geometry parameters, local/world bounds, composed material table,
   combined material parameters, resource set and program IDs. Bounds, distance,
   part resolution, shadow and query must consume that same geometry snapshot.
   Do not retain borrowed catalog metadata across publication.
5. Preflight all programs/resources before enqueueing a partial new frame.
   Parameter edits update payloads/bounds without structural compilation.
   Catalog changes invalidate the program cache through its current revision
   path. A failed new generation leaves the last valid image/resource usable
   with a visible diagnostic; never mix its old shader with incompatible new
   parameter layouts.
6. Reuse arrays/capacity across frames and clear them on reset. Destroy new
   program caches/mesh resources on free. Unbind images/parameter textures before
   resize or retirement, following existing `render_programs_unbind3d` rules.
   Test entity-generation reuse, world clearing, reload and failed recovery.

Avoid a second instance-parameter texture: the current non-GI scene3d budget
can already use all 16 fragment samplers (one parameter texture, eight graph
images, six scene maps, one ramp texture). Put SDF payloads in a suffix of the
existing `scene_parameters` texture. A simple implementation can reserve a
whole-number multiple of the existing 39-texel row per SDF record, keeping
ordinary row indices and worker-merge arithmetic unchanged. Serialize the
layout with named offsets; validate capacity before writing. Share code and
resources by the existing shape/material/image program key, not by parameter
values. Do not promise instanced batching in the first slice.

The scene uniform block is already almost 4 KiB. Do not append camera matrices
there: SDL/Vulkan only binds the first 4 KiB according to the current shader
source. Put the small SDF pass-camera payload in the existing second
`shd_uniforms` block, whose sky data leaves room, and verify the final reflected
layout on the actual backends. Per-instance payloads remain in the texture.

## Rays, depth and transforms

- The existing camera uses a zero-to-one clip-depth convention. Reconstruct
  near and far points using the eye-relative inverse provided by
  `render_scene3d_eye_rays`; preserve `camera.eye` separately. A viewport-local
  interpolated NDC coordinate avoids assuming that `gl_FragCoord` starts at
  zero, particularly inside a shadow atlas tile.
- Transform the clipped world-ray segment into **authored model space**, not
  the dimension-scaled proxy cube. Normalize that local direction and retain
  its conversion to the original ray parameter. Intersect the prepared local
  AABB, then apply the G1-approved field traversal and termination policy.
  A nonuniform entity transform changes the ray's parameterization; it does
  not justify multiplying a world-space march step by a guessed scale.
- Treat camera-inside, near-plane-inside-bound, far-clipped, parallel slab and
  reflected-transform cases explicitly. Return separate miss, exhausted,
  invalid and hit results internally. Exhaustion must be diagnosable rather
  than silently promoted to either a hit or an ordinary miss.
- Transform the actual hit back to world space, project it with the same
  scene view/projection, reject a hit outside the clipping interval, and write
  its normalized depth to `gl_FragDepth`. Enable ordinary opaque depth tests
  and writes, with no alpha blending. Do not write rectangle/proxy depth or
  use an early-fragment-tests qualifier that defeats the hit-depth write.
- The proof does not need an extra sampled copy of opaque depth. Hardware
  hit-depth testing already gives opaque composition in both draw orders.
  A depth-based march cutoff is an independently tested optimization later.
- Transform geometric normals by inverse transpose and normalize. Keep the
  geometric normal separate from the material's perturbed normal; shadow bias
  uses the former. Test very anisotropic transforms and coordinate Jacobians,
  not just sphere silhouettes.

## Shared scene3d material response

After intersection, run `sg_model_hit_dispatch` and
`sg_model_frame_dispatch` with the same prepared geometry values. Read the
winning part's material slot from the composed binding table, then evaluate
that material with its own slice of the combined parameter payload. Keep the
current dominant-part rule for smooth joins.

Construct an `SGSample` containing the actual world hit, authored object
position, selected part position, world/object/part Jacobians, projected UV and
its frame, geometric normal, view direction and simulation time. This is the
boundary into a factored scene3d surface/material/lighting function. The
function should accept explicit hit data rather than reading triangle varyings
as if they were the intersection. Both ordinary scene3d entry points and the
new SDF entry point call it, preserving the existing emission, opacity,
roughness, metallic, occlusion and unlit behavior. Height remains outside the
first SDF silhouette contract; fine fingerprints use Bump/Normal Map.

Initially use object-space coordinates for coherent rigid clay detail. Part
space is available but must be tested at changing smooth-union ownership.
Do not describe object space as deformation-following rest space. Use authored
parameters for a stable instance seed and stepped material state.

Derivative policy needs explicit care. `SG_IMAGE_GRADIENTS` currently switches
**every generated image helper** to `dFdx/dFdy` and `textureGrad`, including
shape images called inside divergent raymarch loops. Do not enable that global
switch on a combined SDF program. The analytic first proof can use base-level
image sampling; matching mesh controls must use equivalent sampling when
compared. Filtered material images require a shape/material-specific helper
policy and valid hit differentials. Ray differentials must be obtained before
divergent marching; neither the proxy's derivatives nor undefined derivatives
after discarded lanes establish surface-image or soft-shadow correctness.

## Directional shadows and other depth consumers

Use model bounds for cascade fitting and reach tests, including casters outside
the camera view. Reuse `render_scene3d_shadow_read(...).matrices[cascade]` to
prepare the light-space ray/hit projection. Draw the SDF depth variant between
the existing cascade begin/end calls; its rectangle stays local to that tile.
The shadow program traces the model and writes its surface depth. An opaque
SDF still needs this program: `cast` currently chooses a specialized program
only for masked materials, otherwise it draws the primitive/mesh directly.

Receiving the existing sun map follows automatically only after the shared
lighting code reads the SDF hit's world position and geometric normal. Verify
that it casts through a hole and produces a silhouette different from its
bound, receives another object's shadow, and keeps an off-screen/animated
caster. Start with `angular_diameter=0` to isolate geometric correctness.

`cast` is also reused by camera occlusion and volume depth. Merely teaching the
sun path about SDFs does not make those passes correct. Keep their features
explicitly unsupported for the first SDF scene, then reuse the same depth
program with each pass's actual camera when separately admitted. Likewise,
`trace_solid` supplies proxy geometry to GI/screen-light tracing; a manual box
must not masquerade as an SDF blocker there. Point/spot lights may illuminate
SDFs, but their shadowed configurations stay unsupported until their depth
passes are integrated.

## Picking is an existing, incomplete contract

`model_ray_hit` already clips to compiled bounds and CPU-samples the model. It
uses 128/512 steps, a fixed 0.8 factor, and a residual threshold based on the
largest bound extent. Error, empty, exhaustion and miss all become `FLT_MAX`.
It returns only the nearest entity, not a hit point/normal/part/status.

An anisotropic conservative field shows why residual alone is insufficient:
for a unit sphere scaled by `(100,1,1)`, at local `y=0.5`, `x=91` has field
value about `0.0383`, below the existing extent-based `0.04` tolerance, while
the positive-X surface is at about `86.6025`. That can change pick order and
hit depth materially. G1 must settle the admitted traversal/termination rule;
copying this picker into GLSL does not satisfy it.

Use the G1-approved CPU ray query for scene3d picking and compare it with
independent roots and GPU hit depths. Keep unsupported CPU operations explicit.
For the first static proof, the exact-world placement used by the existing
picker and displayed placement agree. The existing picker does not receive the
engine's interpolated render pose. Before claiming moving/held-pose selection,
add a narrow query over the frozen presented snapshot, or another explicit
shared-pose interface. Do not silently change editor exact-placement picking
or substitute `raycast_3d`, which queries physics colliders.

## .chibi-first authoring and the native boundary

Game-specific logic belongs in `.chibi`: controls, camera choreography, pose
index/cadence, reset, seeded state, material presets and contact responses.
Models, shader graphs and scene composition remain KDL in the current supported
syntax. Prototypes can use existing `.chibi` declarations. No new field grammar, shader frontend,
self-hosting dependency or native clay controller is needed for this bridge.

Available now:

- `set_shape_parameter(entity, "Radius", Value: float = value)` updates a
  public model parameter. The native implementation validates the proposed
  combined values and bounds before accepting it. `shape.` is an optional
  prefix. Discover nested/public names through inspection rather than inventing
  flattened names. Scalar float/double and the supported nominal vector types
  are accepted; arbitrary C vectors are not interchangeable.
- `set_material_parameter(entity, "Roughness", Value: float = value)` updates
  the material named by `Shader.material`; `material.` is an optional prefix.
  The first clay controls should use this entity fallback material. Invalid
  names/types/nonfinite values report failure.
- Authored `Shader.parameters` supports qualified shape values and model-slot
  material values such as `material.ImageSlot.Roughness`. This text-binding
  facility does **not** mean the runtime material action can target any model
  slot: its implementation selects only `Shader.material`.
- Ordinary reflected component writes can update `Transform3D`,
  `VisualTransform` and authored state. Existing `Tick(dt)`, 3D contact events,
  and physics actions support smooth simulation plus separately sampled visual
  poses. Rendering takes snapshots after these changes, without graph rebuilds
  in entity iteration.

Native C owns the reusable resource, validation, extraction, intersection,
depth/shader/backend and inspection mechanisms. Only add a new reflected native
binding when an authored scene demonstrates a missing operation, for example
a typed visible-surface ray result or an explicitly slot-targeted material
parameter update. Keep such APIs entity/resource scoped, with engine-managed
ownership and explicit failure. No mandatory user allocator and no demo-specific
native functions. New bindings require their C regression and feature-coverage
use or a justified gap entry.

## Proposed future file ownership

Only this document is changed by this design task. The following is an
implementation allocation, to be reconciled after G1; it does not authorize
parallel edits to somebody else's files.

| Owner | Exact files | Purpose |
| --- | --- | --- |
| Scene3d bridge | `src/render/scene3d.c`, new `src/render/scene3d_sdf.c`, new `src/render/scene3d_sdf.h` | SDF routing, serial extraction, frame records/payload layout, color/depth submission, caster integration, capability diagnostics, lifetimes |
| Scene3d bridge | `src/render/shader_program.c`, `src/render/shader_program.h` | Two program kinds, source composition, sampler/layout accounting, cache/bind lifecycle |
| Scene3d bridge | `sdk/desktop.cmake` | Add the new C module to the explicit renderer source list; decide deliberately whether its header is public or excluded from the header glob |
| Scene3d bridge | new `src/render/shaders/scene3d_sdf.vert`, new `src/render/shaders/scene3d_sdf.frag` | Projected carrier and SDF hit/context/depth entry points; one depth define can reuse the fragment source |
| Scene3d bridge | `src/render/shaders/scene3d.frag`, new `src/render/shaders/scene3d_surface.shd` | Extract shared scene3d material/lighting without changing mesh response; place SDF pass uniforms in the second block |
| G1 field owner, coordinated with bridge | `src/render/shaders/sdf_material.frag`, any shared trace helper introduced by G1, `src/render/scene3d_pick.c` | One reviewed traversal/termination contract, legacy-path regressions, query status/pose boundary |
| Validation owner | new `test/render/scene3d_sdf.c`, `test/tools/main.c`, `test/tools/CMakeLists.txt`, `tools/test_policy.py`, `docs/testing.md` | Fixed C-owned GPU workload, dispatcher integration and independently reviewed admission |
| Validation owner | `test/graph/model_surface.c`, `test/graph/model_parameters.c`, `test/tools/scene3d_camera.c` | Bounds/query/parameter and camera regressions in their existing owners; additions still require workload review |
| Authored demo owner | `test/fixtures/clay-lab/content/scenes/ClayDepth.kdl`, its referenced prototypes/models/materials, `content/behaviors/*.chibi` under that fixture | Reuse the laboratory's actual agreed fixture root, authored controls and mixed-scene proof; do not create a competing demo project |
| Documentation owner | `src/runtime/components.h` (SDF comment), `docs/rendering-3d.md`, `docs/declarative-sdf-modeling.md`, this document | Explain additive scene3d support and the supported subset accurately |

Expected untouched integration boundaries: `scene_renderer.c`,
`scene3d_post.*`, `mesh_surface.*`, `scene3d_visual.*`, `scene3d_shadow.*`,
`scene3d_light_shadow.*` and model/compiler IR. Their existing interfaces are
sufficient for the proposed first route. Add a narrow change only when an
actual implementation/test exposes a missing contract. In particular, expose a
shadow-resolution control only if the measured workload needs it; do not
silently change production atlas quality to make a test cheap.

`sdk/desktop.cmake` has an explicit renderer source list and a public-header
glob; update those existing owners rather than inventing a second source list.
Shader resource copying already includes the shader directory. No project
version bump is indicated by this additive route; reassess only if a named
existing project would load incorrectly and needs rewritten data.

## Test and capture admission proposal

All execution remains pending. Use the production supervisor, a shared hard
60-second command deadline, exact candidate/source identity, and the required
hardware opt-in. A short canvas does not make a GPU workload automatically
admitted. Do not run a quarantined aggregate or rebuild one through previews.

1. **CPU field/extraction proposal:** extend the existing bounded model/camera
   dispatchers with a fixed small corpus (at most eight model resources, four
   entities and 64 query rays). Assert transform/ray conversion, snapshot/bounds
   identity, finite rejection, empty bounds, no recompilation on parameter-only
   changes, stable part IDs, unsupported-operation diagnostics and cleanup.
   Independent analytic/bisection roots must catch the anisotropic residual
   counterexample. Source review and first-run admission are required for the
   changed workload, even though the selector already exists.
2. **New `--scene3d-sdf` proposal:** one hidden 128x96 app, at most four SDFs
   plus two ordinary solids, four compiled SDF program variants, no more than
   32 explicitly enumerated frames and bounded readbacks. Cover both opaque
   submission orders/intersections; perspective/orthographic and changed aspect;
   near/far/camera-inside; off-center/rigid/nonuniform/mirrored transforms;
   object/part/normal and matching mesh material controls; two independent
   parameter instances; failed update/recovery; reset and resource reuse.
   Include a small directional-shadow phase for hole/silhouette, off-screen
   caster, changed bounds and receiving. The existing atlas is **2048x2048 per
   cascade** regardless of the 128x96 color target; admit that actual cost and
   keep the fixture to one cascade. If that workload cannot fit, revise it and
   seek admission before running, rather than splitting it into many calls.
3. **Regression selection:** after independent affected-contract review, use
   applicable admitted `model-graphs`, `model-catalog`, `scene3d-camera`,
   `shader-programs`, `sdf-quality`, `material-cameras`, `mesh-materials`,
   `scene3d-shapes` and `scene3d-post` workloads. This list is a selection map,
   not a request to run all of them. New pixel coverage does not replace
   existing unique preview/planar/scene3d contracts. Preserve known baseline
   failures, including the documented Metal sky instance-layout failure.
4. **Matched artifacts after the proof:** save one mixed-occlusion image, one
   light/normal/material control, and one field-shadow image with exact camera,
   field/material parameters, exposure, dimensions, backend and commit. Use
   the bounded native hidden-window controller. Keep the preview laboratory's
   separate image as the old-path control; its planar lighting is not the
   scene3d appearance acceptance. A moving capture and performance study need
   their own fixed workload/admission after G2.

G2 is satisfied only when the admitted field corpus is safe, actual hit depths
compose correctly, matched materials agree, picking matches the supported
displayed-pose contract, and the SDF both casts and receives the selected
directional shadow. Backend two is either tested or explicitly unverified with
its blocker. This design alone satisfies none of those runtime gates.
