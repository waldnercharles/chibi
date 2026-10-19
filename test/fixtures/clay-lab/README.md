# Clay material laboratory

An authored P0/P3 checkpoint with bounded CPU validation against engine main
`07eb2abaa5a097352c472746b2aadf2f97cd5e4e`. It uses the existing planar SDF
renderer. It does **not** add SDF support to the separate `scene3d` renderer.

## What is authored

`MaterialLab` places a sphere, capsule, rounded block and worked block in each
of two rows: neutral above, terracotta below. `ClaySculpt` combines a rounded
box with a translated, smoothly joined sphere and a spherical thumb cut.
`Form=0` recovers the unworked block; `Form=1` shows the lobe and cut. The three
calibration models do not declare a form parameter. Model bounds, not the
prototype's `Geometry3D` dimensions, define their geometry.

Both presets reuse `ClaySurface`. Neutral linear RGB is `(0.46, 0.43, 0.38)`;
terracotta is `(0.52, 0.16, 0.08)`. Pigment stays uniform. Curved fingerprint
ridges perturb the shading normal through the existing Bump node, while
lower-frequency roughness variation is separate. Fingerprints are an analytic
height pattern, not a scanned fingerprint or imported texture. No time or
random value enters either graph.

Eight `.chibi` assets own the component, input behavior, change-only system and
five prototypes. Eight KDL files own the manifest, input bindings, scene, four
models and material. No executable behavior is authored as a KDL graph.
The existing GPU expressions remain inside the declarative model/material
contracts; this is not a new `.chibi` shader language.

## Controls and saved tuning

The same keys affect all eight specimens while playing:

- F: form off/on, visible only on the two sculpt instances
- G: fingerprint normal strength off/on
- V: roughness variation off/on
- O: roughness 0.68/0.50, an artistic oiliness comparison using one existing lobe
- R: restore form 1, fingerprints 1, roughness 0.68, variation 0.06 and spacing 2.4

Controls use existing named action handlers (`on Form`, `on Fingerprints`,
`on Variation`, `on Oiliness`, `on Reset`), dispatched on pressed edges. Holding
a key does not repeatedly toggle it. The built-in movement `on Input` event is
not a button-action callback.
Reset retains each model, pigment and calibration/sculpt identity. Presets are
saved as `ClayNeutral.chibi` and `ClayTerracotta.chibi`; runtime toggles do not
write source or persist between launches. Edit `ClaySettings` defaults in the
neutral prototype to inspect intermediate values. The reset target is explicit
in `ClayControls`, so change it too when adopting a different default preset.

`ApplyClay` publishes material parameters only when `dirty` is true, then
clears it. It writes shape `Form` only for `has_form` instances. A script or
inspector changing a setting must also mark that instance dirty. The material
is the entity's fallback binding, with no model material slots, so the existing
material setter addresses the intended surface directly.

Spacing is 2.4 authored units at scale 1; ridge height is 0.06 units and the
Bump derivative distance is 0.12 world units. The material guards spacing at
0.5 and clamps final roughness to `[0.2, 0.9]`. These are trial artistic values,
not calibrated clay measurements. No physical unit conversion is established.

## Preview and comparison

Open the project with the normal installed player/compiler workflow, or inspect
the saved models in the existing orbitable Model Preview. The checked-in
`preview.json` requests 640×360, one CPU step, one GPU warmup and one output
frame. The separately admitted checkpoint capture reduces only the dimensions
to 320×180 and targets offscreen Vulkan on SwiftShader. It failed during initial rendering
on a UBSan null-argument violation in a zero-size `memcmp` in the pinned Cute
drawing dependency; no image was produced. This was not a device-availability
diagnosis. The checked-in larger request is outside that one-image admission.

Routine agent captures require a built exact candidate and applicable execution
admission. Use the bounded hidden/offscreen controller; do not launch Studio or
turn repeated images into a quarantined suite. Report the actual backend/device
and inspected image, not just process success. One still cannot establish motion
stability or frame time.

The broad white emissive card and fixed HRC settings provide a repeatable
initial planar-lighting setup. Feedback, adaptation, bloom and exposure changes
are absent. Compare each toggle on the **same instance** with the same camera
and lighting. The two rows sit at different distances from the card; comparing
their pixels is not a matched BRDF or pigment test. Raking/grazing inspection,
close-up quality and moving-camera acceptance remain to be verified.

## Exact capability boundaries

- This is the existing `SdfSurface3D` + `Shader.shape` planar/2.5D path. The scene
  intentionally has no `presentation "3d"`. No free-camera scene3d SDF depth,
  mixed opaque composition or field-shaped directional shadow is established.
- Planar HRC maps roughness to its gloss response. The material outputs use the
  existing metallic/roughness contract with Metallic=0, but this preview does
  not establish scene3d GGX appearance. Oiliness changes roughness only; there
  is no independent specular/IOR, clearcoat, second lobe or scattering control.
- ObjectPosition anchors the height field to the whole model, avoiding a
  winning-part coordinate jump at the smooth join. Rigid-motion attachment
  follows that existing contract but is not yet visually verified here. Marks
  on a changing Form stay in model space; they do not follow a deforming rest
  surface or preserve sculpted material coordinates.
- Bump changes shading only. Form changes a bounded primitive construction;
  there is no arbitrary distance noise, sampled volume, VDB bridge, persistent
  dent history, collision component, SDF collider or physical deformation.
- The fixture does not implement pose cadence, surface-update events, shadow
  or antialiasing toggles, replay recording, metrics or camera choreography.
  Those are later checkpoints, not hidden controls. No second backend or
  representative performance target has been established.

## Verification status and next gate

On 19 October 2026, candidate tree
`be110662e6b3928636e0d23821e9aeedc018f0ac` passed the admitted
`clay-format-check` and `clay-lab` routes through `tools/test_policy.py`.
Canonicality took 0.113 seconds; the authored case passed one case and 9,037
assertions in 0.640 seconds across 26 simulation steps and eight specimens.
Native Debug controller/tools/language targets, the Release controller and
compiler-free tools built successfully. Debug tests retained assertions, ASan
and UBSan; the owner disabled leak detection because of ptrace, so leak coverage
is not passed. Configuration/build time is separate from each check's unchanged
60-second production deadline.

The authored case prepares these files, exercises independent and combined
toggles, held/released input, inspector-edited spacing and reset, and checks real
model/material parameter snapshots and behavior failures. Both pigments survive
reset. It verifies unchanged model compilation counts/revision and exactly 24
model setter validations, with none on no-op steps. Material snapshot equality
and the shared dirty guard are not a material-host-call counter. An earlier
candidate failed the first toggle because it listened for the movement event;
the named-action fix passed without changing the test's input path or assertions.

Supporting `model-graphs`, `model-catalog` and `shader-graphs` checks also passed.
`clay-fields` passed five cases and 197 assertions; compiler-free `make test-tools`
passed 134 cases and 27,390 assertions in 24.547 seconds. Those supporting checks
do not themselves load this fixture; the fixed `fixture-format` project list
still excludes it. These are focused results, not passes of unrun aggregates.
See the [baseline](../../../docs/clay-sdf-baseline.md) for the other checks and
publication requirements. The fixed source preflight includes this README in
its 24-file, 32-entry, depth-three and 128-KiB total limits.

Pixels remain unverified after the failed preview. Visible geometry, framing
and broad shading/lighting need image inspection; close-up detail,
moving-camera behavior, scene3d depth/picking and material parity remain separate
gates. Final exact-head validation is still pending. Do not label P0/G0, G1 or
P3/G3 complete from these CPU results. Full native/render/Studio suites remain
quarantined, disabled and not passed.
