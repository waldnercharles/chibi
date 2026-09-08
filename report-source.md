# 3D SDF asset creation for c-blueprints

**Audience:** c-blueprints creator
**Date:** 2026-09-07
**Scope:** whether and how to introduce an AI-assisted 3D signed-distance-field (SDF)
workflow to the current C23 editor and Neon-Swarm-like game.

## Executive answer

Do not make 3D SDFs the project's general asset format or start a full 3D
renderer now. The current game and renderer are intentionally 2D, and already
have a compelling editable SDF/material workflow. Continue it as the production
path for gameplay visuals, with AI helping author constrained 2D shape/material
graphs and concept art.

Use 3D SDFs later for a small, separate experiment if the visual direction needs
one or two hero forms that benefit from live Boolean blending, smooth morphs,
or impossible emissive geometry. Treat the result as a renderer feature with a
strict performance budget, not a new universal content type.

For ordinary 3D game objects, use mesh assets in GLB/glTF. It is an open runtime
delivery format with PBR materials, meshes, skinning, morph targets, and animation
keyframes. It is also the format directly produced by current AI 3D systems.
[Khronos glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)

## What an SDF is useful for

An analytic SDF is a function that returns the signed nearest-surface distance
at a point. Combinations such as min, max, subtraction, and smooth union make
constructive-solid-geometry forms easy to author. A renderer can sphere-trace
the function, estimate normals from the distance gradient, then apply direct
lighting, soft shadows, ambient occlusion, reflections, and emission. Unreal's
distance-field documentation demonstrates the underlying benefits: fast empty
space skipping, soft visibility, ambient occlusion, and shadows.
[Epic: Mesh Distance Fields](https://dev.epicgames.com/documentation/unreal-engine/mesh-distance-fields-in-unreal-engine)

This is excellent for:

- abstract ships, drones, weapons, architecture, terrain chunks, blobs, and
  effects where CSG is part of the visual language;
- live deformation driven by a small parameter set: pulse, wobble, dissolve,
  growth, and smooth morphing;
- a deliberately limited scene with a few bounded objects.

It is a poor primary representation for:

- AI-generated characters, clothing, foliage, props, or realistic environments;
- skinned character animation, clips, authored UV detail, LODs, and broad DCC
  interchange;
- a web and desktop renderer that must support arbitrary scenes at a stable frame rate.

SDF animation is practical for volumetric transitions and effects, not a
replacement for skeletal animation. Houdini's VDB Morph SDF can morph
narrow-band fields but documents a need to re-normalize the result to retain a
proper SDF.
[SideFX: VDB Morph SDF](https://www.sidefx.com/docs/houdini/nodes/sop/vdbmorphsdf.html)

## Lighting and animation

Lighting is fully possible with a ray-marched analytic SDF. The missing work is
not an SDF limitation; it is renderer work: a perspective camera, ray step and
distance limits, normals, materials, direct-light shadow rays, and a strategy
for noise and temporal stability. Distance-field lighting is proven in large
engines, but their representations are normally generated offline from meshes,
not authored at runtime as arbitrary SDF programs. Unreal, for example, creates
mesh distance fields offline and documents resolution, memory, thin-surface,
rigid-mesh, and non-uniform-scale limits.
[Epic: Mesh Distance Fields](https://dev.epicgames.com/documentation/unreal-engine/mesh-distance-fields-in-unreal-engine)

Animation divides cleanly:

| Need | Best representation |
| --- | --- |
| pulse, displacement, CSG merge, dissolve, blob morph | analytic SDF parameters |
| smoke, fire, fluid, destructive or baked volumetric effect | VDB sequence or a baked game effect |
| character and articulated prop animation | glTF mesh, skeleton, and clips |
| deforming mesh with ordinary PBR materials | glTF mesh plus morph targets or skinning |

The glTF specification stores mesh data, PBR materials, skins, articulated
animations, and morph-target animation. It deliberately leaves playback policy
to the engine, which maps well to c-blueprints behaviors.
[Khronos glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)

## Existing formats and software

There is no one universal 3D-SDF scene format comparable to glTF. Choose based
on which representation needs to be editable at runtime.

| Role | Format/tool | Use it for | Do not use it as |
| --- | --- | --- | --- |
| Runtime mesh interchange | **GLB/glTF** | normal game assets, PBR, skins, clips, cameras | editable SDF source |
| Sparse voxel/level-set interchange | **OpenVDB (.vdb)** | offline VFX, simulation cache, SDF/volume processing | a small universal game asset |
| GPU-friendly sparse VDB payload | **NanoVDB (.nvdb)** | experimental static volume/SDF ray marching or collision | a mutable authoring graph |
| Procedural editable source | c-blueprints JSON graph | compact analytic functions and named parameters | general DCC exchange |
| low-cost DCC | Blender | mesh cleanup, retopology, baking, conversion, look development | a complete game runtime |
| procedural/VFX DCC | Houdini | VDB CSG, fracture, fluid, SDF morphs, baking | a reason to require every game asset to be a volume |

OpenVDB is a mature sparse-volume data structure and toolset. NanoVDB is its
read-mostly, GPU-oriented counterpart; it has C99 headers and documentation for
GLSL-compatible usage, but a static topology and a custom GPU data-upload path
still make it a specialized renderer integration.
[OpenVDB: NanoVDB FAQ](https://www.openvdb.org/documentation/doxygen/NanoVDB_FAQ.html)

Blender can convert volume data into an adaptive mesh. Its documented mesh-to-
volume node produces a density/fog grid, so it should not be assumed to preserve
an exact mathematical SDF.
[Blender: Mesh to Volume](https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/mesh/operations/mesh_to_volume.html)
[Blender: Volume to Mesh](https://docs.blender.org/manual/en/5.2/modeling/geometry_nodes/volume/operations/volume_to_mesh.html)

## AI-assisted workflow

AI 3D generation is useful as an input to a mesh pipeline, not as a dependable
way to generate clean analytic SDF programs. Current systems advertise output
as meshes with PBR materials. Microsoft TRELLIS.2, for example, exports a
PBR-ready GLB; its documented local path requires Linux, CUDA, and at least
24 GB of NVIDIA GPU memory. Meshy's text-to-3D endpoint explicitly starts with
an untextured mesh preview.
[Microsoft TRELLIS.2](https://github.com/microsoft/TRELLIS.2)
[Meshy: Text to 3D API](https://docs.meshy.ai/en/api/text-to-3d)

Use this workflow:

1. Generate a style-controlled reference image, preferably with front/side
   views where the asset needs a reliable silhouette.
2. Use an image-to-3D service or local model to create a candidate GLB.
3. Clean it in Blender: remove artifacts, establish scale/pivot, reduce triangles,
   make collision, check UVs and materials, and author or retarget animation.
4. Import GLB only after a future mesh-rendering slice exists. Keep the AI prompt,
   source image, and DCC file as editable provenance rather than treating the AI
   export as source of truth.
5. For procedural 2D visuals, ask AI to produce a constrained graph proposal
   using your existing primitives and parameters; validate it through the existing
   graph compiler and preview before saving.

Image-conditioned generation is generally the more controllable starting point.
TRELLIS' maintainers recommend its image-conditioned route for detail and
creativity over direct text conditioning.
[Microsoft TRELLIS](https://github.com/microsoft/TRELLIS)

## Fit with the current project

c-blueprints is a 2D game/editor today. The active design document calls a 3D
SDF renderer deferred work, while the current gameplay scope is movement,
projectiles, enemies, and particles. The current renderer already has GPU-readback
coverage for 2D SDF geometry and radiance cascades.
[Editor design](docs/editor-design.md)

More specifically, the existing shader graph contains time, scalar and vector
math, 2D position, circle and box distance nodes, CSG operations, smooth union,
parameters, and material outputs. It compiles to GLSL and evaluates shape graphs
per entity. Its separate 2D material buffers are lighting inputs, not a 3D BRDF
or depth pipeline. See [shader_graph.h](src/graph/shader/shader_graph.h),
[shader_nodes.c](src/graph/shader/shader_nodes.c), and
[entity_raster.shd](src/render/shaders/hrc/entity_raster.shd).

This means an immediate 3D expansion requires more than `vec3` primitives:

- 3D transforms, perspective camera, depth and scene bounds;
- 3D picking, gizmos, serialization, and editor previews;
- a different material/lighting model, normal calculation, and quality settings;
- desktop and web renderer tiers, because the current web path does not share the
  desktop compute/radiance implementation;
- collision choices independent of visual representation.

The project already provides a strong test pattern: deterministic scene setup,
fixed camera, and GPU readback. Use it if an experiment proves warranted.

## Recommendation and next decision

### Production route now

Keep the 2D SDF/material graph system as the primary authored-visual workflow.
Add only bounded AI support around it: graph suggestions restricted to the known
node set, validation, preview, human acceptance, then versioned JSON save.
This gives editable, fast, glowing, animated assets that match the game's existing
visual language and renderer.

### One contained 3D SDF spike later

Only if a design prototype proves that a 3D procedural hero form changes the
game's appeal, create a `render3d` experiment with a distinct `GameRender3D`
component and its own viewport. First milestone:

- one bounded analytic object;
- one perspective camera;
- normal-from-gradient shading;
- one directional or point light plus hard shadows;
- explicit max distance, steps, and frame-time budget;
- deterministic fixed-camera image/readback tests on desktop and web.

Do not widen the current 2D graph schema, change `GameTransform`, or replace
radiance cascades during this spike. Promote it only if it meets the budget and
is clearly easier to author than the current 2D effect.

### Mesh route when the project becomes 3D

If the game needs characters, rich props, environments, or general animation,
first build a conventional mesh renderer and GLB/glTF importer. Add per-mesh
generated distance fields later for a specific lighting, collision, or material
feature. This keeps user-facing assets portable and makes AI-generated results
usable without forcing them through an expensive, lossy voxel/SDF conversion.

## Evidence and limitations

This recommendation is based on current public documentation and the checked-out
project source. It does not benchmark a 3D ray marcher on target hardware, compare
commercial AI service pricing, or evaluate generated-asset copyright/licensing.
Those should be separate decision inputs before a shipping AI service or a 3D
renderer commitment.

Research stopped after the evidence covered the material decision points: current
project fit, runtime/interchange formats, lighting, animation, DCC workflow, and
AI output constraints. Further broad searches would not change the recommended
near-term direction.
