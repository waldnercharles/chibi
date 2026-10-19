# Clay laboratory field contract

Source base: `07eb2abaa5a097352c472746b2aadf2f97cd5e4e` (main). Bounded CPU
results below were measured on candidate tree
`be110662e6b3928636e0d23821e9aeedc018f0ac`, 19 October 2026. This is the P1
inventory and initial laboratory subset. It adds a numerical regression corpus,
not a new field API or distance classification in serialized assets. No
project-version change is needed.

**G1 remains open.** The admitted numerical and authored CPU cases now pass;
they do not establish production ray-query correctness, CPU/GPU parity or
backend readiness. Test registration alone is not execution admission. Use the
[current execution policy](testing.md#temporary-60-second-policy); no direct
driver invocation, larger aggregate, or repeated preview substitutes for an
independently admitted check.

## Meaning of a field value

Coordinates are authored object-space project units, X/Z horizontal and Y up.
Negative values indicate the inside, zero the represented surface, and positive
values the outside. A compiler-known construction is distinct from an arbitrary
scalar shader graph.

- **Exact distance:** in ideal arithmetic, the value is the signed Euclidean
  distance to the represented surface. Float evaluation still has rounding and
  finite-range limits.
- **Conservative bound:** the magnitude does not exceed the distance to a zero
  of the represented field, subject to its stated Lipschitz and arithmetic
  assumptions. It is suitable for a bounded step; its residual alone does not
  establish a geometric or ray-depth error tolerance.
- **Estimate:** no such step guarantee has been established. Arbitrary spatial
  noise, displacement, bend, twist, or a float-returning shader function is not
  automatically admitted to either class above.

A continuous 1-Lipschitz field satisfies `abs(f(p)) <= distance(p, zero-set)`.
This lower bound is the reason sphere tracing may take a step. It is not an
upper bound on how far the point is from the surface. Even an exact distance
residual does not bound depth error along a nearly tangent ray. Numerical
floating-point implementation is not an interval-arithmetic proof.

## Existing construction inventory

This table records implemented formulas and conservative classifications, not a
claim that every row has passed the new prototype gate. Main sources are
[model_compile.c](../src/graph/modeling/model_compile.c),
[model_constructive.c](../src/graph/modeling/model_constructive.c),
[model_profiles.c](../src/graph/modeling/model_profiles.c), and
[model_bounds.c](../src/runtime/models/model_bounds.c).

| Operation | Existing semantics | Distance classification |
| --- | --- | --- |
| Sphere | `length(p)-r`; finite positive radius | Exact primitive |
| Box | Euclidean outside distance plus negative nearest face distance; positive outer half sizes | Exact primitive |
| Rounded Box | Evaluate a box with half sizes reduced by radius, then subtract radius; radius in `[0,min(half)]` | Exact rounded primitive; outer extents do not grow |
| Cylinder | Capped Y cylinder; positive radius and half height | Exact primitive |
| Capsule | Distance to the centered Y segment minus positive radius; half segment may be zero | Exact primitive |
| Torus | `length(vec2(length(p.xz)-R,p.y))-r`; `R > r > 0` | Exact ring torus |
| Translate / Reflect | Inverse translation; reflect one axis through the origin | Preserve child classification in ideal arithmetic |
| Rotate | Forward X then Y then Z in radians; evaluate with rounded inverse coefficients | Deliberately contracted bound, including a small contraction at zero rotation |
| Uniform Scale | `s*f(p/s)` for `s>0` | Preserves child classification; does not turn a composed bound into exact distance |
| Nonuniform Scale | `min(s)*f(p/s)`; each factor positive | Conservative if the child is; not Euclidean distance |
| Union | `min(a,b)` | Conservative for conservative children; generally not exact inside an overlap |
| Intersection | `max(a,b)` | Conservative for conservative children; generally not exact outside the overlap |
| Difference | `max(a,-b)` | Conservative for conservative children; keeps A's material, including the cut |
| Smooth Union | Polynomial smooth minimum with positive, spatially constant `k`; maximum subtraction is `k/4` | Conservative for 1-Lipschitz children, generally not exact |
| Offset / Shell | `f-d`; centered shell `abs(f)-t/2`, outward `abs(f-t/2)-t/2`, inward `abs(f+t/2)-t/2` | Preserve Lipschitz bound; amounts are field units, not guaranteed physical thickness after contraction |
| Circle / Rectangle profiles | Exact 2D centered shapes with numeric center | Exact profiles |
| Polygon profile | Minimum edge distance with convex-side or winding sign; simple CCW polygon, 3–32 finite distinct vertices | Exact to the admitted polygon in ideal arithmetic; concave cases exist |
| Profile CSG / Offset | Min/max/negation and scalar subtraction | Conservative after composition; same exactness limitations |
| Extrude / Rounded Extrude | Combine profile distance and capped Z distance; optional inset/radius correction | Conservative given a valid 1-Lipschitz profile; no universal exactness claim for composed profiles |
| Beveled Extrude | Maximum of profile, cap, and `(profile+cap+width)/sqrt(2)` | Conservative given the same profile assumptions |
| Revolve | Map X/Z radius and Y into the profile; reject negative radial bounds, special-case axis boundaries | Profile-dependent construction; keep outside the clay subset until its own corpus is selected |
| Tube Sweep | Minimum of capsule distances for at most 32 nondegenerate polyline segments | Conservative union; spherical joints and caps, not an arbitrary transported profile |
| Bézier Tube / Profile | Expand cubic controls into a bounded sampled polyline/polygon, at most 32 segments | Bound/exactness applies to the sampled construction, not the original continuous curve |
| Rotate Around / Scale Around / Attach | Expand pivot operations or rigid anchor mapping into existing transforms | Inherit the resulting transform/composition classification |
| Parts / functions / instances / arrays | Compile-time organization, finite expansion and stable scope identity; arrays have 1–64 copies | Inherit expanded geometry; organization does not improve a distance guarantee |
| Sampled SDF | RG16 atlas decoded and trilinearly interpolated; clamped border sample plus distance outside its box | Lipschitz-controlled scalar field, not automatically true distance; see below |

For the smooth minimum, within the blend region the two child derivative weights
are nonnegative and sum to one. Thus a spatially constant positive blend amount
preserves the maximum child Lipschitz constant. A parameter can change `k`
between snapshots; making `k` an arbitrary spatial function is a different
contract. Numeric parameter expressions are prepared before spatial evaluation.

### Bounds and numerical limits

- Bounds are local AABBs, separate from distance values. Model dimensions own
  these bounds; `Geometry3D` dimensions do not resize a model.
- Union combines child boxes. Intersection intersects them and may mark the
  result empty when an axis has no positive extent. Difference keeps A's box.
  A zero-thickness intersection can therefore evaluate to zero while its
  rendering bounds are empty. It is not a solid laboratory object.
- Smooth union propagates a positive threshold of `k/4` into each child before
  combining bounds. This is important under nonuniform scale or offset: merely
  padding the final AABB by `k/4` would be wrong. The implementation already
  propagates thresholds through scale, rotation, CSG, shell and offset.
- Rotation bounds invert the actual float coefficient matrix, transform all
  eight child corners, add a rounding envelope, and outward-round the final
  float AABB. The distance multiplier uses a matrix norm bound and
  `nextafterf(...,0)` to avoid claiming an ideal orthogonal float matrix.
- Prepared bounds and the finite-arithmetic proof apply over the compiled
  bounds. They do not promise finite scalar results at every arbitrarily large
  finite query point outside that domain.
- Construction validates finite positive dimensions, rounded-box radius,
  torus radii, legal axes/sides, nonzero scales, cycles and work limits.
  Limits include 1,024 shader IR nodes, depth 128, and 32 public plus hidden
  geometry parameter slots. Dynamic rotations consume three derived slots.
  These are rejection limits, not measured useful scene capacities.

### Sampled fields

[volume.c](../src/runtime/shaders/volume.c) currently accepts cubic resolution
2–144, each half size greater than `1e-5` and at most `1e6`, positive range at
most `1e6`, and declared Lipschitz bound in `[1,16]`. Atlas dimensions must match;
RG stores unsigned 16-bit distance, B must be zero and A 255. Samples include
the local box endpoints, use nearest texel reads and manual trilinear sampling,
with no color conversion or mip interpretation. Every outer-border value must
be strictly positive. Validation bounds the vector of maximum axis differences
and rejects data exceeding the declared gradient bound.

Outside the box, evaluation is the clamped boundary value plus distance to the
box. `shader_graph_lipschitz` returns at least one, or the maximum sampled
declaration plus one. The marcher divides by this bound. The unnormalized
sampled return value itself must not be consumed as a unit-Lipschitz distance.
For supported compiler compositions, positive min-scale correction and CSG
preserve this global bound; the helper is not a general proof for arbitrary raw
shader graphs.

Quantization and trilinear interpolation define the runtime zero surface. The
validator does not prove that the source was a level set, that thin features
survived, or that near-surface values are true geometric distances. No OpenVDB
import/export or re-distancing claim follows from this atlas support. Sampled
assets are outside the initial laboratory checkpoint.

## Coordinates, attribution and parameter transactions

The separate hit program resolves attribution after a surface point is known:
union chooses the lower child value, intersection the higher, and exact ties
choose A. Smooth union selects the dominant child, with the same tie rule; it
does not blend material graphs. Difference always inherits A. Nested parts
retain their scoped identity and construction-local coordinates.

`model_program_hit` is an attribution/coordinate query, not an intersection or
inside test. It can return a part for a point away from the surface. Its
`prepared` argument must be a prepared payload. Selected part bounds are the
subtree bounds before outer CSG clipping, and isolated part distances remain in
part units. These are distinct from whole-model bounds and distance.

The laboratory uses whole-model `ObjectPosition` for fingerprints and roughness
variation so dominant-part changes cannot switch its procedural coordinate
frame. Moving a primitive within a model does not supply an inverse deformation
map for persistent surface marks. The initial Form control is authored shape
variation; it is not retained sculpting or physical deformation.

[model_program_prepare](../src/runtime/models/model_parameters.c) validates
public ranges, computes numeric expressions/hidden uniforms, derives bounds,
and checks finite arithmetic before replacing either output. Invalid inputs
preserve the caller's prepared payload and bounds. `model_compile` similarly
preserves the previous initialized program on failure. Existing
`test/graph/model_parameters.c` and `test/graph/model_constructive.c` exercise
parameter transactions; renderer-cache counters are covered separately in
`--model-catalog`. The measured candidate passed `model-graphs` and
`model-catalog`. The authored `clay-lab` case also verifies unchanged model
compilation counts/revision across its control sequence and exactly 24 model
setter validations. This does not measure material host-call counts or GPU
cache behavior.

## CPU query surface and the current gap

Existing paths already evaluate models on the CPU:

1. `shader_graph_eval_outputs` and the validated scratch evaluators consume the
   same scalar IR used to generate GLSL. Use a prepared parameter payload and,
   for sampled fields, a validated resident-image resolver. Generic successful
   graph evaluation is not a distance-class certificate; callers must check
   finite values and the applicable domain.
2. `model_program_hit`, `model_program_part_point` and `model_program_frame`
   resolve part/material coordinates and the affine Jacobian.
3. `render_scene3d_pick` in [scene3d_pick.c](../src/render/scene3d_pick.c) has a
   private `model_ray_hit`: it selects enabled `SdfSurface3D` model bindings,
   snapshots parameters/bounds, transforms the ray into authored model units,
   clips it to the AABB, and marches the CPU field. Raw shape graphs do not
   enter this model-specific picking branch.

The current picker and [SDF fragment marcher](../src/render/shaders/sdf_material.frag)
both use a residual epsilon of `max(1e-5, maximum_extent*2e-4)`, an 80% field
step, and caps of 128 analytic or 512 sampled/Lipschitz steps. The analytic
step's epsilon floor is below the normal step whenever the residual has not
already been accepted. These constants do not prove a ray-root error bound or
convergence within the cap. The GPU additionally has analytic-sphere and
optional refinement paths; sharing constants is not CPU/GPU parity evidence.

Concrete source-constructed limitation: a unit sphere scaled by `(100,1,1)`
returns `sqrt((x/100)^2+y^2+z^2)-1`. Its maximum extent is 200, so the picker's
residual epsilon is `.04`. At `(91,.5,0)` the value is approximately `.0383`,
inside that tolerance. Along a negative-X ray at `y=.5,z=0`, the true entry is
`x=100*sqrt(.75)`, approximately `86.6025`. Accepting the former point would be
more than four model units early along the ray. The specimen in
`clay_fields.c` checks these field/formula values; it does not execute the
production picker or claim that it returns that exact point.

Other concrete gaps:

- The private picker maps evaluation failure, invalid snapshot, empty bounds,
  step exhaustion and geometric miss to `FLT_MAX`. The public result is only
  an entity. There is no explicit unsupported/exhausted/error query result.
- No distance-class field exists in `model_program`. Current construction
  validation supplies a restricted safe path, but a generic raw shader is not
  tagged as an estimate or rejected by a shared classified-query API.
- The separate scene3d draw path still displays primitive/mesh geometry for
  these bindings, while the picker can intersect their model. Existing source
  explicitly documents this display/picking mismatch. Preview/2.5D rendering
  is the laboratory path until the scene3d bridge is implemented and tested.
- The fragment path discards unresolved rays; current debug output does not
  distinguish every exhaustion/nonfinite/no-hit cause. G1/G2 diagnostic and
  parity requirements remain unsatisfied.
- Profile node descriptions and `declarative-sdf-modeling.md` still describe
  strictly convex polygons. Current code implements simple concave CCW profiles
  and has `concave_sections` coverage. This is a stale documentation claim,
  outside the initial laboratory subset, not a reason to remove that behavior.

No collision uses this document as permission to consume visual field estimates.
The initial playground continues to need explicit existing collider proxies.

## Initial laboratory subset

The authored checkpoint uses existing model syntax and these four constructions:

| Asset | Declared construction |
| --- | --- |
| ClaySphere | Sphere radius 18 |
| ClayCapsule | Capsule radius 12, half segment 12 |
| ClayBlock | Rounded box half sizes `(18,16,16)`, radius 4 |
| ClaySculpt | The rounded block joined to a radius-12 sphere translated by `(14F,3F,2F)`, smoothness 3; subtract a radius-10 sphere at `(-7,38-15F,6)`, with `F` in `[0,1]` |

Allowed geometry is therefore positive sphere/capsule/rounded-box dimensions,
translation, smooth union and difference, with finite bounded scalar/vector
parameter expressions. No anisotropic model scale, arbitrary spatial warp,
sampled field or continuous physics claim is needed. Primitive distances are
exact in ideal arithmetic; ClaySculpt is a conservative composed field. Its
Form-zero endpoint is intended as a pristine-block comparison and needs
numerical/visual fixture verification, not assumption from its name.

Use whole-model coordinates and one material binding initially. Existing rigid
placement and uniform instance scale remain rendering contracts, to be checked
on the actual preview fixture. New numeric field tests at scales `.25`, `1` and
`4` do not validate those rendering transforms. Extending the construction subset
requires selecting its numerical and ray/parity evidence before advertising it
as accepted by G1.

## New bounded C corpus and admission

[test/tools/clay_fields.c](../test/tools/clay_fields.c) supplies
`test_tool_clay_fields()` for the `--clay-fields` dispatcher route. It uses C and
pico_unit with setup/teardown even on failed assertions. The admitted workload
is five cases, eight small in-memory model compilations, each asserted to have
at most 128 scalar IR nodes, 18 bracket attempts (15 crossings and three
expected misses), and at most 413 scalar evaluations. There are no
files, child processes, sleeps, world frames, GPU operations, random inputs,
benchmark loops or environment-sized corpora.

Cases cover:

1. Hard-union/intersection values compared with independent Euclidean geometry,
   demonstrating the conservative/exact distinction.
2. A symmetric smooth-join surface computed algebraically, independently
   bracketed, and checked inside the compiled bounds with both signs.
3. Uniformly scaled sphere roots for outside, inside and near-grazing rays,
   with analytic double-precision root references and explicit miss brackets.
4. Two distinct roots of a thin concentric difference shell.
5. The anisotropic residual/ray-depth limitation above.

The slow reference bisects a known sign-changing interval for at most 24 steps;
it does not use distance-sized steps. It cannot discover tangencies, find the
first root without a supplied bracket, or replace a general intersection
routine. Scalar parity with generated/compiled GPU code and comparison against
the actual picker are deliberately still pending. Existing primitive, bounds,
invalid-input, sampled-field, parameter and material tests remain necessary.

After independent exact-source/workload admission,
`python3 tools/test_policy.py tool clay-fields` passed five cases and 197
assertions in 0.164 seconds on the candidate above. The native Debug build uses
Clang 19.1.7 with assertions, ASan and UBSan. Leak detection was owner-disabled
because of ptrace; leak coverage is not passed. One unchanged 60-second
production deadline includes fixture setup and excludes configuration/build time.
The selector remains standalone; an aggregate addition needs separate
combined-workload admission. CPU-only classification does not admit an expanded
aggregate.

The actual laboratory assets also passed canonicality and the separate
`clay-lab` case: one case, 9,037 assertions and 26 simulation steps in 0.640
seconds. It exercises prepared model/material snapshots and existing named input
actions, including held/released inputs and reset, with no GPU work. These
results do not numerically certify every laboratory surface or the production
picker. See the [baseline](clay-sdf-baseline.md) for supporting checks and limits.

## Next focused implementation, before claiming G1

1. Extend the now-canonical, CPU-prepared laboratory assets with independent
   numerical surface/bounds evidence for their exact construction and finite
   Form snapshots, including the intended pristine-block endpoint.
2. Give the existing model-ray helper a narrow result that distinguishes hit,
   miss, exhausted and invalid evaluation; preserve the existing public picker
   API while making diagnostics testable. Select a shared traversal policy
   with the scene3d bridge, avoiding a second independent marcher.
3. Compare that production CPU path with independent bracketed/analytic roots
   for exterior, interior, thin-shell, tangent/near-tangent, scaled and competing
   object cases. Record residual and ray-depth tolerances separately. Extend the
   result policy only for demonstrated needs; do not introduce a general field
   framework for this checkpoint.
4. Execute matching bounded GPU samples/captures on an available supported
   backend and test actual displayed-surface picking/depth. Unsupported backends
   and quarantined checks stay explicit blockers, not inferred passes.

These steps are separate from an OpenVDB bridge, persistent sculpt storage,
deformable contact or a new shader language. None is implemented by this patch.
