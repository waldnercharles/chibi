# Rendering a 3D arena game

Research checked September 8, 2026. This document recommends implementation work;
it does not claim the proposed renderer or performance targets already exist.

## Recommendation

Build the game on rasterized 3D geometry with depth, normals, emissive materials,
and bloom. Add bounded 3D SDF objects for shapes that benefit from blending or
deformation. Evaluate 3D radiance cascades after this playable renderer exists.
The visible target is a dense, readable arena with convincing depth, responsive
effects, and designer control. Whole-scene sphere tracing and global illumination
are techniques to test against that target.

| Approach | What it gives us | Main cost | Decision |
| --- | --- | --- | --- |
| Rasterized meshes with bounded SDF effects | Efficient repeated actors, familiar surface materials, depth-correct SDF effects and particles | Two geometry paths must write consistent depth and shading inputs | Default production path |
| Entire scene represented by analytic SDF graphs | Editable blends, cuts, repetition and deformation | Acceleration, distance-bound correctness, silhouette tracing, secondary rays and material lookup all become engine responsibilities | Dedicated experiment after the first playable 3D slice |
| SDFs sampled into sparse 3D grids | More predictable field lookup, support for complex authored shapes | Baking or incremental voxel updates, resolution loss, memory and traversal | Later asset option if analytic evaluation becomes the measured bottleneck |
| 3D radiance cascades | Emissive objects illuminate nearby geometry through indirect light | Probe placement, visibility, ray queries, interpolation and dynamic-scene updates | Optional measured lighting milestone |

These are engineering recommendations. No reference establishes that this game
requires any one geometry representation or GI algorithm.

## Repository baseline before this slice

The original renderer collects 2D entities and draws four RGBA16F material images. Its HRC
passes operate on scene color, emission, transmittance and albedo, with a reduced
2D lighting grid. There is no 3D surface depth, normal or BRDF input in this
contract. Preserve the useful shader compiler, parameter bindings and graph
editor, while introducing an actual 3D material contract. See
[scene renderer](../../src/render/scene_renderer.c),
[radiance inputs](../../src/render/radiance.h) and
[renderer notes](../../src/render/README.md).

The initial implementation now adds a separate mesh path with perspective, depth,
instancing and direct lighting. Its progress and remaining material work are in
the [development notes](../arena-development.md).

The pinned Cute revision, `a13f4661f5ba27e8d45027b52f121c1704786425`, already has
3D transform and camera stacks, automatic mesh instancing, custom shader stages,
draw lists and depth-enabled canvases. It explicitly leaves lighting and
materials to the application. Start with that facility instead of introducing
another graphics backend. Its immediate submission normal transform is exact for
rigid transforms and uniform scale; arbitrary nonuniform scale needs a correct
normal matrix. [Pinned draw3d API](https://github.com/RandyGaul/cute_framework/blob/a13f4661f5ba27e8d45027b52f121c1704786425/include/cute_draw3d.h),
[pinned graphics API](https://github.com/RandyGaul/cute_framework/blob/a13f4661f5ba27e8d45027b52f121c1704786425/include/cute_graphics.h).

The existing material output exposes implementation buffers, including four
border coefficients. For new 3D materials, use a familiar Surface output with
Base Color, Emission, Roughness, Metallic, Normal and Opacity, plus a simple
unlit mode. Geometry graphs should describe geometry independently. Noise,
ramps, mapping, texture sampling and reusable groups should work across the
surface and effect workflows where their inputs make sense.

## What the references establish

- Inigo Quilez documents compact analytic shapes and composition. His distance
  reference distinguishes exact distances from bounds, and explains that smooth
  combinations and nonuniform scaling require care. Extruding a valid 2D SDF is
  a useful bridge for our existing shapes. His overview explicitly calls out the
  need for acceleration structures when analytic shape counts grow.
  [Distance functions](https://iquilezles.org/articles/distfunctions/),
  [raymarching overview](https://iquilezles.org/articles/raymarchingdf/).
- `three-rc` uses screen-space probes with a triangle BVH for rays that can see
  offscreen emitters and occluders. Its alternative screen-space tracer loses
  those offscreen contributions. The source has depth and normal inputs, a
  separate gather/shading stage, and known material-lookup and scene-scaling
  TODOs. This demonstrates a hybrid 3D RC architecture, not a drop-in C library
  or a measured result for our arena. The author provides no license and states
  that the interpolation implementation cannot be licensed. Do not copy or
  vendor its implementation. [README](https://github.com/CodyJasonBennett/three-rc),
  [source at inspected revision](https://github.com/CodyJasonBennett/three-rc/blob/43fd4f067b98b8b685259c69ff2c3eb67bf225b9/src/index.js).
- The Holographic Radiance Cascades paper is explicitly about **2D** global
  illumination. Its fixed-cost and timing results do not establish a 3D budget.
  [Authors' paper](https://arxiv.org/abs/2505.02041).
- The JCGT link resolves to *Ray Tracing of Signed Distance Function Grids*.
  It studies sampled fields, voxel traversal and intersection solvers. Its best
  reported approach on an RTX 3090 uses hardware ray tracing over occupied voxel
  bounds. It does not demonstrate that flat evaluation of thousands of analytic
  shape nodes will meet our frame budget on Apple hardware.
  [Paper](https://jcgt.org/published/0011/03/06/paper-lowres.pdf).
- Geometry clipmaps cache terrain height fields in nested grids around the
  camera. Their bounded updates and LOD are useful concepts, but this paper does
  not supply an arbitrary 3D SDF representation or a GI solver. A small arena
  does not currently justify implementing terrain clipmaps.
  [Losasso and Hoppe](https://hhoppe.com/geomclipmap.pdf).

Reference access was checked explicitly. The IQ articles were readable through
ordinary HTTP requests after the web extractor failed. Shadertoy `lslXD8`
returned access errors, so its code, authorship, licensing and performance were
not verified. The dynamic-SDF video title was reachable, but its rendering
claims were not verified from a transcript or source. The truncated Steam CDN
URL cannot identify a document. The JCGT volume/issue/article prefix was enough
to recover its real paper. No requirement should depend on guesses about the
unresolved links.

## Incremental integration

1. **Make depth visible.** Add a 3D camera and mesh draw path using Cute. Render
   a floor, arena walls and repeated actor meshes into an HDR color target with
   depth. Add one directional light, readable ambient lighting and emissive
   color. Keep the movement plane explicit and preserve graph-authored game
   rules. Verify perspective, occlusion, selection and aiming before GI work.
2. **Make materials useful.** Compile the Surface output into the same shading
   function for preview and scene drawing. Supply UV, object/world position,
   world normal, view direction and time. Add textures and visual nodes, live
   parameter overrides, HDR bloom and depth-aware particles. Group repeated
   meshes and bind per-instance data without changing uniforms for every actor.
3. **Add one real SDF feature.** Render a bounded blended obstacle or deforming
   enemy through a proxy volume. Intersect its bounds, trace only the contained
   interval, stop at nearer opaque scene depth, and write the actual hit depth
   and normal. Use analytic intersections for simple shapes where practical.
   Give complex subgraphs a maximum step budget and conservative bounds.
   Compare against a mesh version at the game's camera distance.
4. **Run a 3D GI experiment.** Give raster and SDF objects a shared surface
   description and explicit visibility representation. Start with diffuse RC
   over a bounded arena, static arena acceleration and dynamic actor bounds.
   Test offscreen light and occlusion, thin walls, moving emitters and camera
   cuts. A screen-space-only experiment must be labelled approximate and cannot
   pass an offscreen visibility requirement. Implement or adopt code with
   usable terms; `three-rc` remains a reference. Keep direct lighting and bloom
   as the selectable baseline while measuring GI.

Do not derive rigid-body dynamics from visual SDF gradients in this rendering
work. Contact persistence, continuous collision, sleeping and stable stacks are
separate physics problems. Share authored dimensions with colliders where useful,
and keep visual deformations independent of gameplay collision by default.

## Acceptance and decision gates

Use the current Apple M4 Pro with 20 GPU cores as the first named benchmark
machine, at a fixed 1920x1080 viewport. This is a development reference, not a
claim about the eventual minimum specification. Record the build, resolution,
effect settings and scene counts with each capture.

- Run a repeatable two-minute combat sequence after a 30-second warm-up. Track
  frame-time p50/p95/p99, draw calls, actor/particle counts and GPU pass timings
  where available. CPU submission time must not be reported as GPU duration.
- Target p95 at or below 16.7 ms and p99 at or below 20 ms for the first slice.
  Keep 120 Hz as a later quality target. Include a dense fixture with 200 enemies,
  2,000 projectiles and 10,000 particle instances; these are proposed load targets
  to validate, not measured limits or permission to drop gameplay entities.
- Add geometry-overlap, grazing-angle and full-screen-effect cases. Ordinary
  average FPS cannot establish that sphere tracing behaves during combat spikes.
  Record SDF step counts and budget exhaustion; budget exhaustion is a visible
  defect to resolve, not an acceptable silent disappearance.
- Admit 3D GI to the default quality setting only if it adds useful visible
  lighting within 2.5 ms p95 of GPU time on the reference machine and the total
  frame target still passes. If it fails, keep the experiment available for
  development and continue the game on direct lighting and bloom.
- Verify renders with known occlusion, normal, roughness and emissive cases;
  compare SDF and raster depth at intersections. Exercise the actual editor for
  creation, parameter changes, group navigation, Undo, play, stop and reload.
  Capture real ImGui output. Keep recordings opt-in and out of project assets.

The first release gate is a playable, editable 3D arena with readable combat and
stable frame times. SDF and GI adoption each require their own evidence.
