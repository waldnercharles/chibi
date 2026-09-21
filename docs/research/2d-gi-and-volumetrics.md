# 2D GI and volumetrics for 1440p and 4K

Research date: September 21, 2026.

Updated direction: [Realtime optics before further HRC tuning](realtime-optics-direction.md)
supersedes the implementation order below. Reflection, refraction and caustics
are required, and realtime performance takes priority over physical exactness.
Evaluate bounded optical paths with the diffuse/fog renderer before tuning a
particular cascade implementation further.

The target is beautiful, accurate 2D lighting at 60 fps with screens routinely
filled with fog. Diffuse bounce, participating media, reflections,
refraction and caustics are required. The minimum GPU is undecided, so the design
needs explicit quality levels. Shallow depth is an option, not a requirement.

## Recommendation

Keep Holographic Radiance Cascades as the leading candidate for diffuse and
volumetric transport, but replace the surrounding optical model and resource
schedule where the evidence calls for it. Run a small comparison against corrected
conventional RC before committing. Establish bounded specular transport and
caustics alongside diffuse/fog lighting, then compare the complete feature set
at a fixed frame budget. Use controlled approximations where they improve motion
stability or cost without objectionable artifacts.

My proposed system has these responsibilities:

- A persistent material field supplies density, extinction, scattering and
  emission independently of the simulation method.
- A bounded, world-aligned HRC domain computes diffuse incident light. Its
  resolution follows visible feature size and GPU budget rather than display size.
- Fog shading uses that light and actual density. An optional shallow volume
  provides depth toward the camera if the visual comparison justifies it.
- Bounded specular rays handle reflective and refractive interfaces. A light
  tracing pass handles concentrated caustic energy when enabled.
- Full-resolution reconstruction, exposure and tone mapping produce the image.

This is an engineering recommendation. Candidate performance and visual quality
remain to be measured.

## What was inspected

The separate worktree is `.worktrees/gi-research`, on `cwaldner/gi-research`, based
on `1be98e1eb0ceaad6cc6dec6caa22a9994d341dc5`. The main checkout was on
`cwaldner/hrc-gpu-lab` with substantial uncommitted changes. Those changes were
read but were not copied, committed or modified. This worktree contains the
research note and the committed baseline, not a snapshot of the ongoing experiments.

Chibi MCP reported its project root as
`/Users/cwaldner/Personal/c-blueprints/projects/arena2d`. No authored assets were
edited. Source findings below describe the inspected main checkout unless a
committed-baseline link is explicitly given.

| Area | Current behavior and implication |
| --- | --- |
| Transport | Prefilter, four sequential Trace/Extend/Merge quadrants, gather, filter, bounce and composite. The algorithm is already HRC. |
| Storage | Intervals hold RGB16F plus scalar half-float optical depth. Fluence uses RGB9_E5. Quadrants already reuse scratch storage. |
| Resolution | The transport grid scales down, but gather, temporal images, bounce and composition retain canvas resolution. |
| Bounce | Albedo multiplied by a gathered incident field feeds a later frame. This is iterative feedback with artistic gains, not an explicit converged surface/volume scattering solution. |
| Fog | HRC composition adds `fluence * fog`; fluid raster also produces density-dependent coverage and albedo. There is no separately parameterized scattering coefficient or phase function in this path. |
| Reflection | Existing planar reflection mirrors the already lit screen image. It does not trace reflected transport through the scene. |
| Refraction | Liquid normals distort background and lighting samples. This is image distortion, without Snell refraction or refracted shadows. |
| Gas motion | A persistent sparse staggered velocity grid already advects flow, applies vorticity and projects pressure. Visible density still comes from particle splats. |
| Camera | The raster has a 16-pixel border. History filtering samples the previous image at the same UV. Camera translation is not reprojected in that filter. |

Sources: [renderer](/Users/cwaldner/Personal/c-blueprints/src/render/radiance.c),
[composition](/Users/cwaldner/Personal/c-blueprints/src/render/shaders/hrc/composite.shd),
[reflection](/Users/cwaldner/Personal/c-blueprints/src/render/shaders/hrc/reflection.shd),
[fluid raster](/Users/cwaldner/Personal/c-blueprints/src/render/shaders/hrc/fluid_raster.shd),
[gas solver](/Users/cwaldner/Personal/c-blueprints/src/runtime/material_flow.c),
[scene renderer](/Users/cwaldner/Personal/c-blueprints/src/render/scene_renderer.c).
The separate `render/fog.c` implements visibility fog and should not be mistaken
for the participating-media lighting system.

Several proposed optimizations from earlier notes already exist: compact fluence,
quadrant scratch reuse, the even-direction extension shortcut, matching-resolution
gather specialization, and exact 2x mip prefiltering. Avoid implementing these again.
The last two and the luminance experiments include uncommitted work.

## Fix the accuracy baseline before judging algorithms

There is a concrete mathematical deviation in
[the committed extension shader](../../src/render/shaders/hrc/extend.shd).
It combines two alternative paths by averaging their radiance and optical depth:

```text
tau_blended = (tau_A + tau_B) / 2
T_blended   = exp(-tau_blended)
```

For consecutive segments of one path, adding optical depth is correct. For the
alternative paths in HRC equation 20, the reference blends transmittance instead.
The [reference implementation's Fluence::blend](https://github.com/entropylost/amitabha/blob/38b7ee2364807a4523af6de93cbb2871968c3b85/src/fluence.rs)
also blends transmittance. The representation conversion should preserve that operation:

```text
T_blended   = (exp(-tau_A) + exp(-tau_B)) / 2
tau_blended = -log(T_blended)
```

An independently evaluated example with `tau_A = 0`, `tau_B = 10` gives:

| Operation | Transmittance |
| --- | ---: |
| Current optical-depth average | 0.006737947 |
| Reference transmittance average | 0.500022700 |

This proves a mismatch in the operation, not its whole-image severity. Expect
the discrepancy where the two paths encounter different occlusion. The first
implementation experiment should compare the corrected operation and direct
transmittance storage against the current result. Direct storage could also remove
repeated exponentials in merging, but its half-float precision needs checking.

The prefilter comment that optical depth is additive also does not establish
that spatially averaging it across side-by-side clear and opaque pixels preserves
visibility. A coarse cell containing both cannot generally be replaced by one
homogeneous coefficient without changing transport. Fixing the extension blend
does not solve this separate reconstruction problem. The relevant physical
identity is multiplicative transmittance along a path.
[PBRT, transmittance](https://pbr-book.org/4ed/Volume_Scattering/Transmittance).

Other correctness checks should establish:

- Consistent world-distance units through zoom, output resize and work-grid changes.
  The current trace scales distances by canvas/work width; that alone does not
  establish world-unit invariance through material rasterization.
- Accurate segment lengths through partially crossed cells. The current trace
  uses fixed longest-axis steps, whereas an exact grid traversal integrates the
  distance spent in each cell.
- One emission convention. The current trace switches between opacity times
  emission and emission times distance near zero extinction. Decide whether the
  input means emission per length or a source function before changing it.
- Reference runs with artificial distance falloff, ambient, shadow floor and
  composite gains removed or explicitly modeled. Their appearance can be restored
  as authored controls after transport validation.
- Stable camera motion and source removal. A same-UV history blend can preserve
  old lighting where the scene has moved.

These are bounded investigations, not a claim that every item is a demonstrated bug.

## Which algorithms fit

| Approach | Strength for this project | Limitation | Decision |
| --- | --- | --- | --- |
| Corrected and rescheduled HRC | Dense varying media, many emitters and detailed visibility on a regular 2D domain | Large interval storage; limited outgoing directional detail | Leading candidate |
| Conventional RC with continuity fixes | Smaller or cheaper configurable hierarchy; useful diffuse comparison | Distant small lights and thin occlusion can lose detail; medium integration still costs work | Build one bounded challenger |
| Sparse RC | Can avoid unused probe work in appropriate geometry | Surface-only probes omit fog lighting; dense fog removes much of the sparsity benefit | Study tile classification, defer wholesale conversion |
| Split RC | Sparse world-space 3D diffuse GI | A different problem and renderer contract; not a ready dense-fog solution | Revisit if real 3D becomes necessary |
| Path tracing with temporal reuse | Broad material model and a useful reference path | Noise, temporal history and difficult specular paths compete with the 60 fps budget | Reference first; optional premium experiment |
| Light tracing / photon methods | Focused reflected and refracted energy, including caustic beams | Sampling noise, accumulation and reconstruction bias | Supplement the base lighting |
| Local light propagation / diffusion | Cheap smooth fill in some thick scattering media | Loses sharp transport, thin barriers and directional structure | Possible later multiple-scattering approximation |
| Full 3D HRC | A route to volumetric transport in all directions | Storage scaling is unsuitable as the first high-resolution target | Do not make this the baseline |

The [HRC paper](https://arxiv.org/abs/2505.02041), sections 4-6, supports its fit
for dense media and identifies small-source aliasing, storage and sharp reflections
as limitations. Its reported CUDA timings on an RTX 3080 Laptop are 1.85 ms at
512x512, 7.67 ms at 1024x1024 and 33.9 ms at 2048x2048. These are transport
results, not game-frame budgets; most comparisons use one iteration. Its diffuse
bounce example accumulates six bounces. None establishes native 4K/60 performance
for our renderer.

The [coauthor's project page](https://adrianmargel.ca/projects/holographicRadianceCascades/)
explains the benefit of preserving probe spacing perpendicular to transport.
[Yaazarai's implementation](https://github.com/Yaazarai/Volumetric-HRC)
is useful for comparing early extension and textures versus buffers, but its
timings and stability claims need reproduction on our backend.

[Sparse RC 2D](https://github.com/IceKontroI/sparse_rc_2d) distinguishes air-filled
and surface-edge probes. Its author records remaining dense structures, utilization
losses and a color-quality tradeoff. Sparse storage, empty-space ray traversal and
sparse lighting queries are separate choices. For a fog-filled viewport, compact
dense tiles may be preferable to per-probe hashing. That last judgment is an
inference to benchmark.

[Split RC](https://arxiv.org/abs/2607.20384), published July 2026, uses sparse
world-space probes and rays from visible surfaces for 3D diffuse GI. Its reported
timings exclude G-buffer creation and shadow rays. The paper also retains
thin-geometry and sharp-reflection limitations. It is relevant if our scope grows
into 3D, but does not demonstrate a replacement for our dense 2D medium transport.

[ReSTIR GI](https://research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing)
reuses sampled indirect paths across space and time. That is a serious alternative
for a ray-traced renderer, but adopting it also entails geometry traversal, history
validation and reconstruction. There is no measured reason yet to exchange our
regular 2D transport for that integration burden.

## The optical contract to build toward

Use a renderer-owned material grid with world coordinates and cell size. Simulation
publishes density and material identity; the renderer derives optical coefficients.
Neither HRC nor the compositor should query particles, Box2D or MPM directly.

For the first volume model, define absorption `sigma_a`, scattering `sigma_s`,
extinction `sigma_t = sigma_a + sigma_s`, emission per unit distance `q_e`, and a
normalized phase function `p`. Optical depth over length `d` is `sigma_t * d`.
The scattering source is proportional to incident directional radiance:

```text
q_scatter(x, direction_out) = sigma_s * integral(p * L_in, directions_in)
q_total = q_e + q_scatter
T_segment = exp(-sigma_t * d)
L_segment = q_total * (1 - T_segment) / sigma_t
```

Use the continuous limit `L_segment = q_total * d` when extinction is zero.
This is the constant-coefficient segment model proposed for implementation;
heterogeneous cells require suitable subdivision or a stated approximation.
The source-function convention instead stores `q_total / sigma_t`. Pick one and
migrate assets directly. Do not retain both meanings under the same material field.

Scattering probability and angular redistribution must conserve energy.
[PBRT's phase-function discussion](https://pbr-book.org/4ed/Volume_Scattering/Phase_Functions)
provides the 3D foundation. For intrinsic 2D transport, normalize over angle on a
circle, not solid angle on a sphere. Isotropic 2D scattering uses `1/(2*pi)`;
copying a 3D phase function unchanged gives the wrong normalization.

Use [Jarosz et al.'s 2D transport formulation](https://cs.dartmouth.edu/~wjarosz/publications/jarosz12theory.html)
for surface scattering and reference images. A physically consistent 2D model has
different measures from 3D. A fluence image, a surface-shaded image and a camera
looking through a 3D volume measure different things; compare like with like.

Start with isotropic diffuse scattering, for which integrated incident light is
enough. Directional fog, normal-dependent surface response and glossy materials
need directional information that the final RGB average has discarded. Preserve
or query directional data only when the corresponding feature is implemented.
Four quadrant means can support a coarse approximation, not a narrow glossy lobe.

Represent surface coverage and boundaries separately from medium density.
An opaque wall needs a visible boundary receiving light without transmitting into
the wall interior. A participating volume can transmit, absorb and scatter inside.
The existing albedo/absorption arrangement conflates those uses. Add the minimum
boundary/material information required by the tested model.

Scalar extinction is economical for neutral fog. Colored glass needs wavelength-
dependent attenuation, at least RGB. Initially that can live in the optional
specular tracer; an RGB-extinction HRC variant would widen every interval and must
earn its memory cost. Dispersion would require wavelength-dependent refraction,
which is a later visual feature.

For multiple scattering, treat feedback as a numerical iteration with a measured
residual and response time. More scattering in dense, bright fog can require many
iterations. Try one explicit extra iteration and warm-started history; measure
light-on, light-off and moving-occluder behavior. Do not label a one-frame source
solve as converged multibounce GI. Reuse extinction work where practical, while
recognizing that changed scattering sources require recomputing radiance transport.

## Fog that fills the screen without visual clutter

Make persistent density authoritative for broad gas coverage. Start by extending
the existing velocity solver with advected density and optional temperature.
Particles can inject momentum, density, heat and local detail. Their count and
spacing should not determine whether the ambient fog exists.

The first art target should be broad density structures with restrained small-scale
detail. Advect detail with the flow and filter it to the lighting grid's resolution.
Independent, rapidly changing noise in each frame would undermine both transport
sampling and visual calm. Vorticity should preserve useful structures rather than
continuously inject small swirls everywhere. Tune sources, sinks and boundary
conditions explicitly for a screen that stays populated.

[Bridson and Mueller-Fischer's fluid notes](https://www.cs.ubc.ca/~rbridson/fluidsimulation/2006/fluids_notes.pdf)
describe staggered flow, pressure projection, advected density, numerical
dissipation and vorticity confinement. The existing implementation already uses
several of these ideas. Evaluate bounded higher-order advection with an appropriate
limiter and record density drift; increased sharpness alone is not a conservation
guarantee.

Future MPM can feed the same material field. [MLS-MPM/CPIC](https://yuanming.taichi.graphics/publication/2018-mlsmpm/)
is relevant to deformable matter, cutting and rigid-body coupling. Its particle/grid
transfer does not supply a fog lighting model or guarantee a cheaper atmospheric
simulation. Rasterize particle mass/density with normalized kernels and explicit
cell units, not arbitrary alpha splats. Keep the MPM grid, gas grid, lighting grid
and display independently sized. Budget GPU contention, transfers and body coupling
across the complete frame.

### Should fog have depth?

My recommendation is to prove the 2D optical model first, then compare a shallow
volume in one controlled scene. For the shallow version, author a thickness or a
small stack of density layers and integrate scattering/transmittance toward the
camera. This can add foreground haze and depth separation while keeping the
simulation planar.

[Wronski's volumetric fog work](https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf)
separates medium lighting from camera integration using a coarse 3D grid.
[Hillaire's Frostbite presentation](https://advances.realtimerendering.com/s2015/index.html)
unifies local particles and larger media through extinction data. Those are useful
production patterns, not evidence that we need a full 3D GI volume.

An experimental 320x180x8 camera volume has 460,800 cells; one RGBA16F texture is
about 3.52 MiB. That is only a storage example. Lighting, temporal buffers,
integration and shadowing add costs. Feeding it a 2D lighting field extruded through
depth is a deliberate approximation. Accurate out-of-plane shadows, phase response
and inter-layer transport would require more lighting information. A layer stack
does not create full 3D correctness on its own.

## Reflections, refraction and caustics

Add a narrow segment-query capability over the material field and real interfaces.
It should return the next boundary, normal, material, optical path attenuation and
the information needed to continue a ray. A conservative distance field or tiled
grid traversal can accelerate empty regions. Within varying fog, an empty-space
distance field alone cannot integrate the medium.

For reflection and refraction, trace a bounded number of interface events, apply
Fresnel weights, handle entering/exiting media and total internal reflection, and
terminate a rough/diffuse continuation using the incident-light cache where valid.
Keep artistic sprite normals separate from geometric interface normals. A 2D
ray cannot reproduce arbitrary camera-view reflections without a chosen display model.

For caustics, sample paths from lights through reflective/refractive interfaces and
deposit their contribution on receivers or along medium segments. Apply a finite
pixel/beam footprint and measure temporal stability. Depositing this energy needs
an explicit path split so HRC and the photon pass do not count the same light twice.
Secondary diffuse scattering may then use the deposited energy as a source.

[Bitterli's Tantalum](https://benedikt-bitterli.me/tantalum/) demonstrates 2D
light tracing, refractive paths and fluence reconstruction. It also explains why
naive camera-origin sampling fails for some ideal caustic paths. Its displayed
light paths are not a complete participating-media solver.
[NVIDIA's caustics implementation](https://developer.nvidia.com/blog/generating-ray-traced-caustic-effects-in-unreal-engine-4-part-1/)
provides a separate example of bounded photon tracing, footprint reconstruction
and adaptive allocation. Neither supplies a portable performance result for us.

Reflection, glass and caustics are now required in the integrated comparison.
Keep their cost predictable through explicit ray/event budgets and quality tiers.
Rough-reflection cache lookups, broader caustic footprints and short histories
are useful approximations when they preserve stable motion.

## Scaling to 1440p and 4K

HRC's predictable scene cost does not mean constant cost with resolution. Our
interval allocation grows approximately as `W * H * log2(max(W,H))`, plus
full-canvas resources and passes. Object rasterization and simulation can still
grow with content. Extra scattering iterations multiply transport work.

I evaluated the allocation formulas in the inspected `radiance.c` with 8-byte
intervals, 4-byte fluence, shared quadrant scratch, three filtered work textures,
four RGBA16F canvas images and a mipmapped bounce canvas:

| Output canvas | Transport grid | Hierarchy scratch MiB | Approximate HRC-owned MiB |
| --- | --- | ---: | ---: |
| 2560x1440 | 2560x1440 | 530.04 | 743.35 |
| 3840x2160 | 3840x2160 | 1233.37 | 1713.28 |
| 2560x1440 | 1280x720 | 125.49 | 291.33 |
| 3840x2160 | 1280x720 | 125.49 | 478.82 |
| 3840x2160 | 960x540 | 69.19 | 415.60 |

These are calculated resource payloads, not observed GPU residency or timings.
They exclude material textures, final output, driver padding and other allocations,
and assume no luminance child and a fresh allocation at that size. The scene
renderer adds a 16-pixel border on every side, so real allocations are larger.
The HRC-owned totals describe the inspected working tree, not a promise that the
committed baseline allocates exactly the same images.

For reproduction, with work dimensions `w,h`, let `N = ceil(log2(max(w,h)))`.
For each level `l` below `N`, set `d = 2^l` and allocate the larger of the two
rotated interval counts, `ceil(w/d)*(d+1)*h` and `ceil(h/d)*(d+1)*w`, times eight
bytes. The largest rotated `ceil(w/d)*d*h` count across levels determines each
of the two four-byte fluence buffers. Add four `w*h*4` frusta. This gives the
scratch column. The total adds `18*w*h` filtered textures, `32*W*H` canvas
textures, an eight-byte full mip chain for bounce, and `8*sum(2^l)` merge weights.

Even quarter-scale 4K retains hundreds of MiB because canvas-sized images remain.
Move transport, bounce sources and most history/filter work to a separately sized
lighting grid; keep high-resolution visibility information and final reconstruction
where they provide visible detail. Measure every full-screen read/write pass.

Start the scaling experiment at 1280x720 transport for both 1440p and 4K output,
with 960x540 as a lower tier. These are test points, not accepted quality settings.
A thin distant shadow lost by a coarse grid cannot be recovered by sharpening.
Preserve blocker coverage and small-emitter energy, use visibility-aware local
reconstruction, and raise resolution when those tests fail. Fix minimum emitter
size in world units rather than letting resolution silently alter source power.

Priorities after correctness:

1. Profile individual GPU phases, memory traffic and dispatch overhead. Sweep
   trace cutoff and coherent workgroup layouts on Metal and a PC GPU.
2. Reduce canvas-sized intermediate images and duplicated writes. Remove an image
   only after checking its bounce, history and debug consumers.
3. Specialize scale-1 and integer reductions, reuse immutable weights and optical
   classification, and preserve already implemented compact storage.
4. Try tile classification and static material caching before sparse radiance
   storage. A dirty material tile can change distant lighting; material caching
   does not authorize caching final light without dependency handling.
5. Add a world-aligned domain with an explicit offscreen-light policy. A 16-pixel
   guard band is inadequate for long-range transport. Larger margins cost area;
   a coarser outer domain needs a separate acceptance test for boundary continuity.

The uncommitted [lab report](/Users/cwaldner/Personal/c-blueprints/docs/hrc-lab.md)
records the following earlier matched 1080p opaque-interior experiment:

| Configuration | Median ms | p95 ms | Mean 8-bit RGB error versus full HRC |
| --- | ---: | ---: | ---: |
| Full RGB | 24.74 | 25.40 | Reference |
| Quarter RGB | 6.16 | 8.84 | 0.921 |
| Quarter RGB, independent half luminance | 10.89 | 11.31 | 0.706 |
| Quarter RGB, luminance derived from RGB | 7.11 | 11.02 | 0.920 |

These historical completed-frame measurements were re-read, not rerun. They are
one scene on the recorded Metal backend. Full HRC is a comparison baseline, not
physical ground truth. Derived luminance does not restore visibility or color
that coarse RGB transport missed, and the extra pass was slower than color-only
in this case. Keep independent luminance only if wider testing earns its cost.

Use an initial engineering target of at most 6 ms GPU time for diffuse transport,
fog lighting and reconstruction on each selected baseline device, within a
complete-frame p95 target of 16.67 ms. Allocate optional specular work separately.
These are acceptance targets, not predicted results. CPU and GPU overlap, so do
not add their independent timings to claim a frame time. The actual minimum GPU
must be selected before making a product-level 4K/60 commitment.

## Experiments and adoption gates

| Order | Work | Evidence needed before continuing |
| --- | --- | --- |
| 1 | Correct interval blending and establish physical units | Analytic slab and two-path checks; matching 2D reference; pinhole, shadow and dense-volume image comparisons |
| 2 | Bounded reflection/refraction and caustics in continuous fog | Mirror/glass/lens scene, correct ray directions and TIR, finite footprints, no duplicated direct light; fixed ray/event budgets |
| 3 | Integrate the complete feature slice and choose its view model | Dense fog, diffuse receiver, moving optics and lights; compare planar and shallow-depth presentation at the same frame budget |
| 4 | Reschedule lighting resources and compare a corrected RC challenger | Same feature set, material field, world extent and output; phase timings, memory, regional error and motion stability |
| 5 | Improve fog motion and scattering where the images justify it | Body wake, source removal and dense high-albedo stress case; stable density and complete-frame timing with simulation active |
| 6 | Tune quality tiers | Equal-time comparisons with documented bias, filtering, temporal lag and optical path limits |

Extend `projects/hrc_lab` through Chibi MCP after explicitly pointing the authoring
session at this worktree. Keep pinhole, tiny emitter, colored bounce and volume
fixtures. Add homogeneous slabs, half-clear cells, rotating thin blockers,
near-white scattering fog, source extinction, camera pan/zoom and an offscreen
emitter. Validate the shared renderer in another scene without combat.

Accuracy measurement should use linear HDR buffers at a fixed exposure, regional
and whole-image error, transmittance error and energy checks. Dark-background
averages can hide a missing beam. Measure shadow-edge location, color bleeding,
light-off response and moving-image flicker. For caustics, use a converged light
tracer or an appropriate bidirectional reference rather than assuming ordinary
path tracing samples every path. Match the reference's dimensional model and
pixel reconstruction filter, and quantify remaining reference noise.

Record GPU/backend, build and source hashes, output and work dimensions, world
span, density coverage, emitter size/count, scattering iteration count, history
settings, memory and p50/p95/p99. Use repeated warmed runs with adaptation disabled.
Include dense media over the entire view. GPU captures diagnose phases; their
instrumented timings should not be mixed with ordinary frame measurements.

Judge beauty in motion as well as stills: quiet broad fog structure, legible bright
silhouettes, color separation, shadow contact and restrained small-scale detail.
Keep HDR transport through composition and use exposure/tone mapping instead of
hard-clipping lighting to display white. Bloom should be a presentation choice
after energy and material behavior are calibrated.

Adopt the simplest candidate that passes these gates. Preserve a pinned baseline
for experiments, then migrate authored contracts and remove superseded runtime
paths when adopting the winner. The project has one consumer; a permanent generic
renderer plugin framework or legacy aliases are unnecessary.

## Evidence and reproducibility

This research performed live source inspection, primary-source reading, a
numerical transmittance counterexample and a resource-allocation calculation.
It did not build or execute a candidate renderer, rerun historical benchmarks,
or establish a performance gain.

Key inspected main-checkout SHA-256 values, useful because that checkout is dirty:

```text
src/render/radiance.c
9aa8f04cc18b10141adb1ef8f643542f737230a22da06baaf2ddf02eb7fdad82
src/render/shaders/hrc/trace.shd
9d40d4e2064de27b9236967e3efce28ac6981a319b0a3b0e493700fe858754cf
src/render/shaders/hrc/gather.shd
002d7e5b53fedea39a127f015d663da4563f30f821f0c396fb272a98f2bcc483
src/render/shaders/hrc/prefilter.shd
edcdddc3c7c3d7ffddc555d4e3143d85416f70f1631d23c3f23ca70d5120f520
src/render/shaders/hrc/composite.shd
4a2b1cee2be688a2ff52f0b4f76f3812fd820d23ff20832ce7e7e16500e5e991
src/runtime/material_flow.c
b5a22719f4de37496c7a16b64cd6cadbe93ab930ccd101e41f9721325fa99712
docs/hrc-lab.md
16a655ea3219673f3a1c5cbbf411019a34da853e5fd9ac89aa4d3f50a5234e06
```

The extension shader and compact fluence helper matched the committed baseline
at inspection. The upstream HRC code was inspected at
`entropylost/amitabha@38b7ee2364807a4523af6de93cbb2871968c3b85`.
Downloaded paper text and page renders used for reading are under
`/private/tmp/cblueprints-gi-research`; the primary-source links in this note are
the durable references.
