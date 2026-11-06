# Multi-plane HRC and cached SDF feasibility

Research date: 2026-10-02. Current source inspected at `3b14ccfa68788b74327cfdffb8495a0e70c5a737`. This is an assessment, not an implemented or benchmarked renderer.

This focused assessment extends the existing [3D radiance cascades research](3d-radiance-cascades.md), whose screen-probe/world-interval distinction remains relevant. Primary sources below were checked again for this question.

## Assessment

Two intersecting HRC planes could produce a useful stylized lighting approximation. They would add complementary views of silhouettes and emitters. They would not reconstruct general 3D lighting simply by interpolating the two results. A depth render for each plane adds useful geometry information, but it becomes useful to transport only when the ray traversal and interval representation actually consume it.

Consider planes XZ and XY. They share X, but one collapses Y and the other collapses Z. Projection onto both can make an object visible in one field when it is hidden in the other. However, a diagonal light path whose direction has nonzero X, Y and Z components lies in neither plane. Projecting that path makes different geometry overlap. Two independent planar visibility answers therefore do not determine the true path visibility.

A depth map preserves the nearest surface coordinate along the projection axis. This permits reconstructing that sample's XYZ position and making depth-aware sampling decisions. It does not preserve all surfaces along that axis. Two nearest-depth maps can still miss enclosed geometry, undersides, and surfaces hidden from both projections. Orthographic views are especially convenient for mapping world positions to these fields; perspective depth requires reconstruction through the projection matrix.

## What current HRC would reuse

The current mesh pass projects real XYZ triangles, keeps depth within a mesh group, and emits material height and normals. The depth attachment itself does not reach HRC. The general material canvas has a single visible height/normal per pixel. See [mesh material](../../src/render/mesh_material.c), [mesh fragment](../../src/render/shaders/mesh_material.frag), and [material targets](../../src/render/material_raster.h).

[HRC traversal](../../src/render/shaders/hrc/hrc_trace.shd) marches `vec2` paths through emission/optical depth and previous bounce. [Final composition](../../src/render/shaders/hrc/composite.shd) subsequently uses visible height for ten local relief-shadow steps. Rendering the scene twice into the existing inputs would retain those same transport assumptions twice.

Height-aware traversal can compare an occluder to a ray's elevation. Its visibility then depends on the ray's starting elevation and vertical slope. Current cached intervals do not contain those extra coordinates. A plane with depth is consequently not a drop-in 3D interval representation. A constrained elevation band or fixed light elevation can be an intentional approximation, but should be described as such.

## Reconstruction and blending

Naively adding the two fields counts duplicated emitter energy twice. A normalized weighted blend avoids that straightforward brightness increase, but does not repair visibility: a bright result from one projection can fill a shadow that only the other projection captures. Taking the darker value can instead introduce false shadows from either projection.

A practical blend should account for the surface normal, proximity to the represented depth, valid coverage, and whether the sample belongs to the same surface. These weights reduce leaks and discontinuities; they do not make the missing paths recoverable. Plane normals alone are insufficient when a projected source or blocker comes from the wrong depth.

Keep fields world-aligned if the camera rotates. Camera-aligned fields change overlap, blocker silhouettes and missing geometry as the camera moves. A stable world field lets each visible XYZ hit sample the same lighting. The current history logic reprojects translation and checks spans; it does not provide rotation/perspective reprojection ([radiance.c](../../src/render/radiance.c)).

## Relevant primary evidence

- [Radiance Cascades paper source, screen-space probes with world-space intervals](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex#L633): probes can sit on depth-buffer surfaces while storing octahedral directional maps and tracing world-space ray segments, including SDF queries. Spatial interpolation uses depth similarity; directional interpolation is separate. This distinguishes a 2D storage layout from a genuinely 2D transport solution. The same source describes world-space voxel probe grids and their higher storage cost. It does not demonstrate two ordinary planar solves as sufficient for 3D reconstruction.
- [Mara et al., Deep G-Buffers for Stable Global Illumination Approximation](https://diglib.eg.org/items/17a12515-3329-48c9-8462-2d77bfe5779a): multiple separated depth layers improve approximate indirect illumination, AO and reflections within bounded memory. This supports retaining more surfaces as a useful intermediate step; it is a different method from blending intersecting HRC fields.
- [NVIDIA RTXGI visibility-aware irradiance reconstruction](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/rtxgi-sdk/shaders/ddgi/Irradiance.hlsl#L89): reconstruction combines eight neighboring world probes and uses direction, surface orientation, and stored directional distance moments to weight visibility. This is concrete evidence that spatial interpolation of lighting benefits from independent visibility information, beyond proximity alone.

## Recommendation for Neon Swarm

First retain the world XZ HRC field for the game's primary plane. Add bounded 3D SDF visibility if its geometry/deformation benefits are wanted. For lights or elevated ships whose shadows expose projection errors, use targeted height-aware or SDF visibility queries. That keeps the existing appearance and avoids making every light pay for another complete cascade solve.

If two planes are tested, treat them as a visual experiment with a normalized, depth-aware blend. Compare raised emitters, stacked ships, a diagonal blocker, and camera rotation against the single-plane result. Record false shadows as well as leaks. A second solve adds raster, cascade, gather/history and storage work; no constant performance multiplier is established here.

For sustained arbitrary camera angles, a small set of world-space elevation bands or surface probes with world-space SDF intervals is a better-defined path than interpolating two projected images. Layers address missing spatial samples; directional intervals address missing light directions. Both can be restricted to the game's shallow volume without committing to a large general 3D GI system.

## Dynamic SDF video clarification

The [Mike Turitzin video](https://www.youtube.com/watch?v=il-TXbn5iMA) describes caching expensive authored distance graphs at grid samples (4:15), interpolation (5:33), and sparse storage near surfaces (8:43). It explicitly describes brick maps (9:16), 8×8×8 bricks (10:06), one-byte narrow-band distances (8:04), clipmaps (11:44), incremental updates (12:58), and BVH-based edit culling (13:18). At 14:18 the author identifies the update method as an approximation. These findings were checked against a freshly downloaded English transcript; they correct the older research note's statement that the video did not establish a brick size or encoding.

That cache accelerates distance queries; it does not by itself define 3D HRC light transport. The related [JCGT paper](https://jcgt.org/published/0011/03/06/paper-lowres.pdf), sections 2–3, describes intersections and normals from cached trilinear cells. Consequently, normals need not repeatedly evaluate the original authored graph. The video does not establish that its renderer uses RTX traversal.

## What the linked SDF video changes

Mike Turitzin's [dynamic SDF engine video](https://www.youtube.com/watch?v=il-TXbn5iMA), published January 6, 2026, directly addresses the repeated graph-evaluation cost. Its description, chapters, and English automatic transcript were retrieved again for this assessment. This pass inspected those sources, not rendered video frames or an executable benchmark.

- [4:15](https://www.youtube.com/watch?v=il-TXbn5iMA&t=255s): evaluate the authored distance function at grid points and reuse those samples for rendering. Interpolation reconstructs the field between samples.
- [8:43](https://www.youtube.com/watch?v=il-TXbn5iMA&t=523s): allocate distance storage sparsely near surfaces, using a brick map and atlas. At 10:06 he specifies 8-by-8-by-8 bricks. The preceding discussion describes compact one-byte distances over a restricted range.
- [10:34](https://www.youtube.com/watch?v=il-TXbn5iMA&t=634s): retain sampled SDF rendering because rebuilding a mesh at the desired fidelity costs more; accept some rendering cost to make dynamic geometry updates cheaper.
- [11:44](https://www.youtube.com/watch?v=il-TXbn5iMA&t=704s): nested clipmap grids reduce distant detail and storage.
- [12:58](https://www.youtube.com/watch?v=il-TXbn5iMA&t=778s): spatially index edits with a BVH and regenerate affected bricks. At 14:18 he qualifies the update method as an approximation.

This fresh transcript supplies details that the older 3D RC research note left unverified, including brick size and distance encoding. It does not establish a frame budget on our 4080 Super or a portable renderer implementation.

There is no universal thousand-primitive cutoff. A naive renderer's cost grows with covered pixels, ray steps, and graph work per step. Spatial culling reduces local graph work. Caching moves much of that work into field generation and dirty-region updates. Total authored shape count then affects regeneration more than each cached rendering query, although cache resolution, traversal, bandwidth, and change rate remain costs. Rigid ships could keep their field in object space and transform queries without rebuilding on translation or rotation. That is an application proposal, not a claim about the video's world-space cache.

The video's linked [Soderlund, Evans, and Akenine-Moller paper](https://jcgt.org/published/0011/03/06/paper-lowres.pdf) adds another option. Sections 2 and 3 derive ray intersections and normals from cached cell values. Normals need not reevaluate the original shape graph. It also treats continuity between neighboring cells. Its fastest tested method uses RTX traversal, but that is the paper's benchmark result, not evidence that Turitzin's renderer requires RTX.

Cached fields trade exact analytic geometry for sampled geometry. The video demonstrates corner distortion at coarse resolution. Thin features, normal continuity, and brick boundaries need appropriate resolution and filtering. Caching geometry also does not convert planar HRC into 3D light transport; it supplies a more affordable scene query that a future height-aware or world-space tracer could use.

For our assets, small analytic SDFs and cached object-space fields are both credible runtime representations. Sparse world bricks become more relevant for terrain and many persistent edits. Mesh extraction remains an option, but is not necessary to avoid repeatedly evaluating a large graph.
