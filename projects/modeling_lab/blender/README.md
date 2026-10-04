# Blender and SDF reconstruction comparison

The refined Blender reconstruction is visibly closer to the source photograph.
The control render shows that much of the difference comes from rendering. The
refined model also corrects proportions and adds optical detail. This exercise
does not establish that meshes inherently reconstruct shapes better than SDFs.

[Open the visual comparison](compare.html). It includes all three images, a
Blender variant selector, an opacity overlay, and side/back views. Images retain
their full 4:3 frame. The overlay does not warp or automatically align them.

- [Editable Blender scene](../../../build/model-evidence/blender-comparison/refined-reference.blend)
- [Final Blender render](../../../build/model-evidence/blender-comparison/refined-reference.png)
- [Existing SDF render](../../../build/model-evidence/camera-delivery/ModelPropStudy.png)
- [Original photograph](../reference/polaroid-onestep.jpg)
- [Reproduction script](build_blender.py)

## What the renders test

| Image | Geometry | Materials and lighting | What it tells us |
| --- | --- | --- | --- |
| Existing SDF | Original 26-part declarative construction | Existing engine materials and planar HRC | Delivered engine baseline |
| Blender control | Mesh counterpart of the same 26 parts and dimensions | Original material values interpreted by Principled BSDF, Cycles lights and physical backdrop | Better lighting and surface response help even before reconstruction changes |
| Refined Blender | Corrected face height, control placement, apron, tapered base, wider lens opening, curved glass, optical chambers, grooves, dial ribs and edge fillets | Cycles, transmitting glass, glossy plastic, physical shadows and reflections | Best reconstruction from this exercise |
| Diffuse material study | Same refined geometry and camera | Same lighting; surface shaders replaced with diffuse reflection, including opaque glass | Shows which cues depend on specular reflection and transmission |

The control is an approximation of the analytic construction, not an exported
sample of the SDF itself. Cylinders and revolutions use 192 angular segments,
rounded boxes use eight bevel segments, and Blender Boolean operations reproduce
the cuts. Coincident union surfaces and edge normals can differ from a marched
SDF. These departures are recorded by `sdf_control.control_report()` and in
`build/model-evidence/blender-control-sanity.json`.

All reference views use the same projection. `sdf-camera.json` derives it from
the engine's actual transform and deterministic quaternion path: 1040 by 780,
1300-pixel focal length, centered principal point. In Blender this is a 45 mm
lens with a 36 mm horizontal sensor. Six points matched the two projection
formulas analytically. This is a projection check, not pixel parity between the
renderers. The lighting environment and color processing differ between Blender
and the engine, so the control isolates the construction from later modeling
changes, not one individual rendering algorithm.

## Why Blender is closer

The original construction already contains a recognizable camera. Rendering it
in Cycles makes the lens and controls cast shadows onto the face and apron, gives
rounded edges highlights, separates glossy plastic from matte surfaces, and
anchors the base to a physical floor. These changes are visible in the control
without raising the face or moving its controls.

Our current material pass reduces diffuse albedo for metallic surfaces and stores
roughness as gloss. It has no transmission or IOR contract. The current HRC
composite uses projected directional lighting, a limited height-based relief
shadow, and a dielectric highlight with a fixed camera-axis view. It does not
trace the optical reflections and refractions in the photograph. See
[`surface_material.shd`](../../../src/render/shaders/surface_material.shd) and
[`hrc/composite.shd`](../../../src/render/shaders/hrc/composite.shd).

Blender's Principled material supplies reflection and transmission, and Cycles
traces rays through the modeled glass and around the scene. The saved scene uses
IOR 1.49, curved closed lens geometry, dark chambers, area lights, and a curved
paper backdrop. These are actual 3D surfaces and lighting. Blender documents the
[Principled reflection/transmission model](https://docs.blender.org/manual/en/4.0/render/shader_nodes/shader/principled.html)
and [Cycles renderer](https://www.blender.org/features/rendering/).

The refined construction makes a separate contribution. Hand inspection found
the SDF's face and controls roughly 17 to 39 pixels too low while the bottom
already aligned fairly well. The Blender model raises the face, lens, shutter,
viewfinder and knob, and adjusts the apron/base. It also models details absent
from the SDF asset: curved optical surfaces, a recessed finder assembly, physical
lens grooves and dial grips. The reference landmarks were used to fit these
changes, so their residual errors are fitting diagnostics, not independent
accuracy validation. Their hand-marked uncertainty is recorded in
[`reference-landmarks.json`](reference-landmarks.json).

None of these observations proves an inherent SDF geometry limitation. Many
proportion changes are directly expressible with the current declarative nodes.
Per-edge fillets, richer profiles and cheap normal detail would make authoring
fine details easier. The current camera is close to the compiler's 1024-node
budget, with 903 distance nodes and 982 hit nodes, which constrains extra modeled
grooves in this implementation. SDF intersections could also feed a richer PBR
renderer; that is separate from how the asset is represented.

## Remaining differences

This is still an approximation. The reference's lens reflections, scratched or
textured optical window, subtle molding, rounded knob and warm light with blue
fill are not fully reproduced. The apron and rear housing are too clean and
simple. Rear details are inferred because the single photograph does not show
them. The full photograph is only sampled on the same two small printed badges
used in the SDF asset; those badges contain photographed shading. The housing,
optics, rainbow, controls and silhouette are geometry or procedural materials.

The diffuse study deliberately turns glass into an opaque light-colored surface.
It is a shader removal experiment, not a proposed competing reconstruction.
Likewise, the alternate views inspect volume rather than verify hidden surfaces
against evidence we do not have.

## Reproduce

From this worktree, with Blender 5.2 installed:

```sh
blender -b --factory-startup \
  --python projects/modeling_lab/blender/build_blender.py -- \
  --variant refined --samples 128 --width 1040
```

On this Mac, replace `blender` with
`/Applications/Blender.app/Contents/MacOS/Blender`. The script uses Blender's
bundled Python and requires no additional packages. It selects an available
Metal, OptiX, CUDA, HIP or oneAPI GPU, otherwise uses the CPU. Linux execution has
not been tested. The `.blend` packs its label image for use outside this checkout.

Use `--variant control` for the construction control, `--variant diffuse` for
the material study, and `--view side` or `--view back` for alternate views. An
optional `--output /path/to/directory` changes the destination. Each run creates
a `.blend`, PNG and JSON report with camera, object count, render settings and
elapsed rendering time. The final model has 66 mesh objects and 87,030 base mesh
vertices before evaluating its remaining bevel/normal modifiers. This is an
asset creation study, not an optimized game mesh.

Validation used Blender 5.2.2 LTS, Cycles with Metal on this Mac, 128 samples,
denoising, fixed seed 29, and 1040 by 780 output. Reference, control, diffuse,
side and back images were rendered and visually inspected. The final saved file
was reopened and checked for packed images, camera settings, finite geometry and
expected materials. Render duration is recorded only as local run evidence;
first-use kernel compilation, denoising and host load prevent interpreting it as
a game-performance comparison.

All additions are in `projects/modeling_lab/blender` and ignored build evidence.
The earlier implementation, authored assets, source photograph and SDF render
are preserved. No engine tests were rerun because no engine code or existing
assets changed. `validation.json` under the comparison evidence records the
saved-file check, artifact hashes and preservation check. Changes are uncommitted.

Reference: [Polaroid OneStep by Mike W.](https://commons.wikimedia.org/wiki/File:Polaroid_OneStep.jpg),
CC BY 2.0. The model and renders are adaptations of that reference; see
[the reference attribution](../reference/README.md).
