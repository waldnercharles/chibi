# Clay material laboratory

Unapplied, unvalidated source draft. This fixture converts the published
`aa7b4fcabe62fb91ee90d8e80800724a9c30e6e4` content to the typed asset contract
at `31098151bd5c0f3042f74996d678c0b784f3f25f`. It has not been compiled, loaded,
formatted by the engine, simulated or rendered. The final current-main,
compiler and typed-assets integration checkpoint is still required.

## Authored content

`MaterialLab` places a sphere, capsule, rounded block and worked block in each
of two rows: neutral above, terracotta below. `ClaySculpt` combines a rounded
box with a translated, smoothly joined sphere and a spherical thumb cut.
`Form=0` recovers the unworked block; `Form=1` shows the lobe and cut. The three
calibration models do not declare a form parameter. Model bounds, rather than
the prototype's `Geometry3D` dimensions, define their geometry.

Both presets reuse `ClaySurface`. Neutral linear RGB is `(0.46, 0.43, 0.38)`;
terracotta is `(0.52, 0.16, 0.08)`. Pigment stays uniform. Curved fingerprint
ridges perturb the shading normal through the existing Bump node, while
lower-frequency roughness variation is separate. Fingerprints are an analytic
height pattern. No time or random value enters either graph.

Thirteen `.chibi` files are ordinary `package asset` sources with
`import data "game:assets"` and `pub Build :: () -> byte[]`. They describe the
project, input map, scene, four models, material and five prototypes through
`data.Encode`, `data.Object`, `data.Field.Named` and explicit typed values.
`ClaySettings`, `ClayControls` and `ApplyClay` remain executable declaration
sources. The existing GPU expressions remain inside the model and material
graph contracts.

The project manifest uses `Int(33)`, scene version uses `UInt(1)` and graph
versions use `UInt(3)`. IDs and counters use UInt; reflected float values and
graph coordinates use Real. Terracotta's Shader.parameters string contains a
complete compiled asset Build source for `material.BaseColor`. The fixture
contains no KDL source, compatibility loader or alternate setter names.

## Controls and saved tuning

The same keys affect all eight specimens while playing:

- F: form off/on, visible only on the two sculpt instances
- G: fingerprint normal strength off/on
- V: roughness variation off/on
- O: roughness 0.68/0.50, an artistic oiliness comparison
- R: restore form 1, fingerprints 1, roughness 0.68, variation 0.06 and spacing 2.4

Controls retain named input-action handlers, now with explicit signatures such
as `on Form :: ()`. These actions dispatch on pressed edges. Holding a key is
intended to leave the selected value unchanged. The built-in movement `Input`
event is separate from these button actions.

Reset retains each model, pigment and calibration/sculpt identity. Presets are
saved as `ClayNeutral.chibi` and `ClayTerracotta.chibi`; runtime toggles do not
write source or persist between launches. Edit `ClaySettings` values in the
neutral prototype to inspect intermediate values. The reset target is explicit
in `ClayControls`, so change it too when adopting another default preset.

`ApplyClay` runs on Tick and publishes only when `dirty` is true, then clears
it. It calls `set_shape_parameter` for Form only on has_form instances, and
`set_material_parameter` for Fingerprints, Roughness, Variation and Spacing.
The same names appear in the published source. Float constructors adapt the
settings values to the existing native setters. A script or inspector changing
a setting must also mark that instance dirty.

Spacing is 2.4 authored units at scale 1; ridge height is 0.06 units and the
Bump derivative distance is 0.12 world units. The material guards spacing at
0.5 and clamps final roughness to `[0.2, 0.9]`. These are artistic trial values;
no physical unit conversion is established.

## Preview and comparison

The unchanged `preview.json` requests 640 x 360, one CPU step, one GPU warmup
and one output frame through the normal hidden preview controller. It is an
authored request, not an execution receipt. Rendering this draft awaits the
final combined source, matching dependency archives and a scheduled GPU slot.
Record the actual backend/device and inspect the resulting image. A still
does not establish motion stability or frame time.

The broad white emissive card and fixed HRC settings provide the initial
planar-lighting setup. Feedback and adaptation are disabled. Compare a toggle
on the same instance with the same camera and lighting. The two rows sit at
different distances from the card, so comparing their pixels is not a matched
BRDF or pigment test. Raking/grazing inspection, close-up quality and
moving-camera acceptance remain unverified.

## Capability boundaries

- This uses `SdfSurface3D` and `Shader.shape` in the existing planar/2.5D path.
  The scene has no 3d presentation setting. Free-camera scene3d SDF depth,
  mixed opaque composition and field-shaped directional shadows are unverified.
- Planar HRC maps roughness to its gloss response. The material uses the
  existing metallic/roughness contract with Metallic=0. This draft does not
  establish scene3d GGX appearance. Oiliness changes roughness only; it has no
  independent specular/IOR, clearcoat, second lobe or scattering control.
- ObjectPosition anchors the height field to the whole model. Its attachment
  during rigid motion has not been visually verified here. Marks on a changing
  Form stay in model space; they do not follow a deforming rest surface.
- Bump changes shading. Form changes a bounded primitive construction. The
  fixture has no arbitrary distance noise, sampled volume, VDB bridge,
  persistent dent history, collision component, SDF collider or deformation.
- Pose cadence, surface-update events, shadow/antialiasing toggles, replay,
  metrics and camera choreography remain later work. No second backend or
  representative performance target is established.

## Validation still required

The planned C/pico field and authored-player checks are not part of this draft
directory. Their current CMake/Graph/dispatcher wiring will be adapted on the
final integration checkpoint. Preserve the five field cases and the authored
case's eight specimens, 26 steps, held/released input, independent and combined
toggles, inspector-edited spacing, Reset, both pigments, unchanged model
compilation counts and exactly 24 model setter validations. Preserve real CPU
shader/model publication and resource limits after recounting converted source.

No historical result for the KDL fixture is a pass of these converted sources.
Engine canonicality, actual compiler/SDK integration, current-source ordinary
drawing, real Linux/glibc sanitizer evidence, matching dependency archives and
clay image inspection remain pending. This source draft supplies no merge or
publication eligibility.
