# Depth shadow reconstruction investigation

October 4, 2026. Source inspection, primary-source research, and frozen native
Metal diagnostic renders. The initial investigation below preceded the
implementation and measurements recorded in the final section.

## Findings in the current implementation

`src/render/shaders/hrc/composite.shd` adds planar incident HRC light to the
depth surface term: `lit = incident * albedo + surface_shaded`. It does not take
the maximum of those two estimates. The following `max` applies only to the
artistic shadow floor and `visibility.rgb`. Neither operation allows a blocked
depth ray to remove the original-emitter contribution already present in HRC.
Consequently, improving depth shadows alone can still leave an expected shadow
filled with planar light. Applying one scalar shadow factor to the final RGB
would suppress unrelated lights and indirect bounce as well.

`surface_depth.frag` has a separate reconstruction problem. Each receiver builds
eight azimuth directions with twelve radial samples and stores a 32-bit elevation
mask for every radial prefix. Source queries select or interpolate these shared
masks. At radius 384, sample centres are 32 world units apart. Thin blockers can
fall between samples; a nearby emitter can be tested against a neighboring
azimuth instead of its actual segment. Smoothing an already incorrect visibility
result does not locate a missing blocker or correct its depth ordering.

The prefix rule attempts to stop at a source's distance, which is useful, but
radial interpolation can blend in a sample beyond that source or miss a blocker
before it. The elevation result is binary until neighboring bins are interpolated.
These are separate from composition overlap. The controlled scene below confirms
a missed shadow, although it does not isolate the share due to each sampling axis.

## Diagnostic captures

The controlled shader overrides in `build/sdf-shadow-diagnosis` use matching
960x632 renders: SDF frame 0 and Suzanne frame 360. The variants are combined,
HRC-only, depth-only, and depth with visibility disabled. In the inspected frames,
HRC-only object interiors are almost black, with rims/fog remaining. Depth-only
retains the major bands. Disabling depth visibility removes some mottling, but
horizontal stripes remain on the upper-right SDF box with a bore. Therefore the
additive overlap is a real architectural limitation, but does not explain those
observed stripes by itself.

A further diagnostic marks the composite's final no-compatible-receiver fallback
magenta. The exact stripes become magenta. Another override changes only the
composite's raw height rejection/attenuation to `dz = 0`; those stripes disappear.
This confirms a separate reconstruction hole: the quarter-resolution receiver
grid can provide no samples accepted by raw camera-height compatibility on a
sloping surface. It does not establish that every band originates there, nor
that depth visibility or shadow placement is correct.

The diagnostic `dz = 0` is not a production fix. Dropping rejection globally can
borrow illumination across disconnected surfaces and occluders. The first
reconstruction experiment should compare distance to a sample's tangent surface
plane instead of raw height difference, while retaining normal/coverage checks
and explicit discontinuity guards. Thin or sharply curved features may need
adaptive finer receiver coverage rather than broader acceptance. A sample's
surface identity or geometric continuity information would make rejection safer
if the existing buffers cannot distinguish adjacent disconnected surfaces.

### Controlled shadow placement

The MCP-authored `ShadowPlacementControl` in the disposable
`build/sdf-shadow-diagnosis/blocker-project` contains an orthographic camera, a
flat mesh receiver at elevation zero, one 8-by-70 blocker spanning elevations
28 to 40, and one radius-4 SDF emitter centered at x=-70, elevation 80. Ambient,
fog, bounce, specular, bloom and temporal filtering are disabled. The global
slab thickness is 12, matching the blocker. Capture size is 640x480.

The current shared visibility mask produces a narrow curved shadow. A diagnostic
pixel walk along each actual finite receiver-to-source segment produces the
expected broad projected blocker shape. It uses the same front-depth/slab data,
emission hierarchy, reconstruction and display settings. The walk interpolates
camera depth with reciprocal perspective depth, stops before the emitter, and
uses a small depth bias. The test scene is orthographic, so it does not validate
perspective motion. Its 1,024-step limit exceeds every tested segment, and is
deliberately unsuitable as a promised production budget.

A patch at world x=55..65 and y=-5..5 lies safely inside the blocker's projected
umbra. Mean displayed grayscale values on a 0..255 scale were:

| Variant | Patch mean |
| --- | ---: |
| Current depth visibility | 70.5275 |
| Current HRC plus depth | 70.5275 |
| Visibility disabled | 70.5550 |
| Finite-segment diagnostic | 0.0000 |
| Segment, blocker removed | 70.5550 |
| Segment, blocker moved beyond the emitter | 70.5550 |

These display values demonstrate missing occlusion, not a linear energy or
accuracy metric. The unobstructed controls show that the segment result is not
simply darkening all lighting. The existing blend is not responsible for this
fixture's missed shadow: depth-only and combined results match in the patch.

Reproduce after capture with `python3 build/sdf-shadow-diagnosis/check.py depth`:
`expected_shadow: false`, exit 1. The same command with `segment` reports
`expected_shadow: true`, exit 0. Both report `unobstructed_controls: true`.
The fixture and its two controls validate through MCP with no asset errors.

[Capture gallery](../../build/sdf-shadow-diagnosis/index.html),
[current shadow](../../build/sdf-shadow-diagnosis/blocker-depth/frame.png),
[segment reference](../../build/sdf-shadow-diagnosis/blocker-segment/frame.png),
[reconstruction holes](../../build/sdf-shadow-diagnosis/sdf-holes/frame.png).

The production renderer and original scenes are unchanged. All shader changes
are disposable diagnostic overrides. No performance claim follows from these
captures, and no claim is made that all banding is fixed.

Validation scope: native Metal frozen captures, three MCP-validated diagnostic
scenes, image-level shadow/clear-path checks, `git diff --check`, and matching
hashes for every preexisting file in the preservation snapshot. The whole copied
project's format check reports two inherited noncanonical behavior assets,
`SdfCameraOrbitYaw.kdl` and `SdfCameraYawSway.kdl`; they were not edited. No new
Web, GLES, moving-camera, or whole-engine test result is claimed for this study.

All screen-space proposals below still lack hidden and offscreen geometry. A
constant thickness slab is an authored approximation, not the actual object's
back surface. A front/back depth layer, world-space proxy, or acceleration
structure supplies additional information at additional cost.

## Recommended experiment order

1. Repair and validate the confirmed reconstruction holes using surface-plane
   compatibility or finer receiver coverage. Compare sloping meshes, rounded
   SDFs, thin silhouettes and adjacent disconnected geometry; retain the magenta
   fallback diagnostic so a smoother image cannot hide rejected samples.
2. Use the confirmed finite-segment reference to evaluate capped, light-directed
   shadow queries or a conservative depth hierarchy. Retain the unblocked and
   beyond-emitter controls. Test two differently colored emitters, thin gaps,
   moving blockers, perspective and hidden geometry before choosing a budget.
3. Separate visibility quality from receiver reconstruction in those comparisons.
   Account for finite ray length, endpoint bias and budget exhaustion explicitly;
   an exhausted query is not evidence of unobstructed light.
4. Replace binary angular edges with fractional coverage only if those
   comparisons show elevation quantization dominates. Prefer compact boundary
   information or a small number of sub-bin masks before a full float array.
5. Test a surface composition that chooses the depth direct estimate for the
   original emitters and retains HRC for indirect lighting. This requires a
   separately identified HRC indirect term; present combined HRC fluence cannot
   supply that separation by changing one blend expression.

This sequence can retain the fake-3D authoring model, surface buffers, emitter
pyramid and planar HRC. It tests a bounded approximation rather than requiring
full world-space 3D GI.

## Algorithm choices

### Visibility masks and fractional angular coverage

The visibility-bitmask paper models each sampled depth value as a finite-thickness
surface, marks covered angular sectors, and unions them using bit operations.
Unlike two horizon angles, this can retain disconnected visible ranges behind
thin objects. Its published mask is binary with a half-sector overlap criterion;
fractional per-sector storage is an extension, not a feature provided by the
paper. The authors explicitly retain single-layer limitations and fixed-thickness
leaks. [Therrien, Levesque and Gilet, 2023](https://arxiv.org/html/2301.11376v1)

For this renderer, fractional overlap would make a moving slab cross a sector
gradually. Overlapping blockers must be unioned, not simply added: two disjoint
partial intervals can occupy one sector, whereas coincident intervals should not
double coverage. Per-sector `max` is also not an exact interval union. Retaining
angular interval boundaries, sub-sector occupancy, or accepting a measured union
approximation are different tradeoffs.

The current 96 uint prefixes occupy 384 bytes of logical shader-local state.
Replacing each with 32 float coverages would occupy 12,288 bytes, 32 times as much
logical state. Actual register/spill behavior depends on shader compilation;
logical size does not establish resident memory or timing. Two or four packed
sub-bin masks would be a smaller experiment but still quantize boundaries.
Smoothing coverage trades hard steps for leakage/softening; it cannot recover
blockers never sampled. Mittring surveys compact masks as a general rendering
tool, not a free continuous visibility replacement.
[EA SEED coverage masks](https://www.ea.com/games/battlefield/news/coverage-bitmasks)

GTAO analytically integrates estimated horizons and uses spatial/temporal
distribution to reduce sampling noise. It is an ambient/indirect occlusion
method; its scalar AO should not be substituted for per-emitter direct
visibility. Its height-field assumptions are especially awkward for floating
SDFs and gaps. [Jimenez et al., GTAO](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf)

### Finite source segments and hierarchical screen-space traversal

Perspective-correct DDA visits a contiguous set of projected pixels without
the gaps or repeated visits of naïve fixed-step marching. McGuire and Mara also
describe multiple depth layers for robustness. This provides a useful reference
for our exact finite source segment, using the existing reconstructed position
and slab thickness instead of tracing full SDF graphs.
[McGuire and Mara, 2014](https://www.jcgt.org/published/0003/04/04/)

For production, bound the number of evaluated source candidates and ray steps,
report step-limit exhaustion separately from clear visibility, and compare
near-only rays against full-radius rays. The current emitter pyramid can remain
the candidate source estimator. A ray for every leaf emitter at every receiver
would abandon the intended many-light budget.

A depth hierarchy lets rays skip cells whose depth bounds cannot overlap the
segment. AMD's SSSR implementation has explicit hierarchy generation,
classification, intersection and denoising stages. The hierarchy is extra work;
it is not already supplied by our emission pyramid.
[AMD SSSR documentation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/)

In our slab representation, conservative min/max occupied depth bounds are safer
than blindly reusing a reflection-specific single min-depth test. A broad bound
can descend unnecessarily but must not declare empty visibility through occupied
geometry. Dense, overlapping slabs can reduce the hierarchy's skipping benefit.
An azimuth mask remains useful for broad diffuse visibility, while short direct
segments can repair important contact shadows with a capped budget.

### Composition and depth-aware radiance intervals

A clean target is `visible emission + depth-tested direct + HRC indirect` on
surfaces. This is a renderer-specific design recommendation, not a complete
implementation. To obtain the last term, solve or track bounced sources
separately from original emission, or propagate direct/indirect channels through
the same interval hierarchy. Additional channels or solves incur memory,
bandwidth and update costs. Subtracting two independently filtered totals can
produce negative values or instability; clamping hides rather than resolves it.

An interim explicit surface HRC weight can reveal overlap, but is artistic
tuning and should not be described as a direct/indirect decomposition. Taking
`max(HRC, depth)` would still prevent depth from darkening HRC-lit shadows, with
additional color-channel switching. Multiplying all HRC by a depth AO scalar
couples unrelated sources and bounce.

Radiance intervals jointly encode incoming radiance and attenuation, allowing
near occlusion to modulate farther radiance during merges. This is the useful
longer-term architectural principle: put visibility into transport rather than
add another independent brightness estimate at the end.
[Osborne and Sannikov, radiance intervals](https://doi.org/10.1093/rasti/rzae062)

Our planar intervals have no elevation dimension. Depth-aware interval transport
would need directional elevation samples, depth layers, or a geometry-aware ray
representation; carrying one max height cannot describe disconnected gaps.
Bounding radius or cascade count limits work and truncates transport. It does
not create missing dimensions or establish cheap whole-renderer timing. HRC's
published fixed-size 2D transport cost does not cover these added representations.
[Freeman, Sannikov and Margel, HRC](https://arxiv.org/abs/2505.02041)

### ReSTIR and world-space radiance cascades

ReSTIR DI resamples light candidates using spatial and temporal reservoirs,
reducing the number of expensive visibility queries for many dynamic emitters.
It still needs valid candidate PDFs, weighted resampling, visibility tests and
history handling; it solves direct lighting, not the HRC indirect term.
[Bitterli et al., 2020](https://research.nvidia.com/labs/rtr/publication/bitterli2020spatiotemporal/)

This is a possible later route if capped deterministic source estimates lose
important distinct emitters. It introduces stochastic color noise, reservoir
storage and motion/rejection requirements into a renderer already troubled by
moving lights. Using screen-space visibility leaves the same missing-geometry
limitations. It is not the smallest first fix for bands or additive overlap.

Split Radiance Cascades uses sparse world-space probes and ray splitting to
address the storage costs of detailed volumetric RC. It is a substantial
alternative pipeline, not a drop-in planar visibility repair.
[Freeman and Sannikov, 2026](https://arxiv.org/abs/2607.20384)

## Evidence needed before choosing a default

Capture stationary and moving mesh/SDF blockers at matched light power, with
one and multiple emitters, subpixel lights, thin gaps and blockers beyond the
source. Measure shadow-edge position, visible bands and temporal trails separately
from brightness. Report hierarchy build, source evaluation, visibility,
reconstruction and total frame time independently. Keep existing HRC-only output
as the baseline and preserve identical scene/camera conditions. A larger sample
budget is justified only when its measured quality improvement survives motion
and its complete-frame cost fits the intended runtime hardware.


## Implemented experiment

The optional implementation lives in the existing `sdf-scenes` worktree. The
original investigation and its disposable reference shader remain above as a
record of the diagnosis. The renderer now has three changes:

- Reconstruction uses symmetric distance to the two tangent planes, retaining
  normal, coverage and material rejection. It uses the existing filter and
  composite passes without another buffer or texture fetch.
- `surface_depth_visibility = "Bounded segments"` samples the finite path from
  receiver to a coarse source representative. It skips empty source tiles and
  exits when fully blocked. The source grouping uses canvas dimensions, avoiding
  a receiver-depth-dependent switch between different visibility representatives.
- `surface_depth_emitters = "Uniform material proxies"` captures uniform sources
  independently of visible geometry. One batched draw writes emission and
  weighted source metadata at one quarter of each canvas dimension. It avoids
  evaluating the original mesh/SDF surface shaders a second time.

The capture is refreshed every rendered frame. Two RGBA16F targets cost 16 bytes
per capture pixel, 619,520 bytes at the padded canvas used for the 960x632
examples. A bilinear footprint preserves subpixel source power. Storage scaling
increases FP16 additive headroom; it does not eliminate accumulation precision
loss with deep overlaps. Proxy metadata keeps a weighted centre, depth and radius.
Overlapping sources can merge. The radius terminates segment queries at the
emitting volume boundary, so an opaque sphere does not eclipse its own centre.
Perspective endpoints use the projected world-segment fraction. Nonspherical
objects use a radius derived from their world bounds; this can retain self-shadowing
or exclude nearby blockers that fall inside the approximate source volume.

Both new selectors are opt-in. The proxy mode replaces the Depth term's visible
emission field and supports uniform GameMaterial mesh/SDF emitters and plain 2D
circles. It excludes shader-authored emission and per-part shader materials.
Those excluded sources retain their original visible HRC contribution. Hidden
proxy emission is not injected into a separate HRC bounce solve. HRC still runs,
including its existing approximate GI when feedback is enabled. Its original
light can still fill a depth shadow; direct/indirect separation was not changed.

### Correctness evidence

The final 640x480 controlled renders use two frozen frames, no ambient, fog,
bounce, bloom, specular or temporal filtering. These are displayed grayscale
patch measurements, not linear radiance comparisons between algorithms:

| Control | Angular masks | Bounded segments | Segments with proxies |
| --- | ---: | ---: | ---: |
| Visible emitter, clear path | 70.5675 | 70.5675 | 76.8475 |
| Visible emitter, blocker before source | 70.5075 | 0 | 0 |
| Visible emitter, blocker beyond source | 70.5675 | 70.5675 | 76.8475 |
| Camera-hidden emitter, clear path | 0 | 0 | 76.8475 |
| Camera-hidden emitter, blocker before source | 0 | 0 | 0 |
| Emitter removed | 0 | 0 | 0 |

The separate camera cap covers the emitter from the camera but sits above it,
leaving its downward path to the receiver clear. Opaque sphere controls also
retain light: clear/blocked patch means are 76.8475/0 orthographically and
76.065/0 with focal distance 240. Proxy brightness differs because source shape,
area and emitting depth are approximated; this is not exact parity with the
visible surface source field.

The steep-plane GPU regression fails against the old filter/composite with a
zero minimum and passes with the replacement, minimum 0.0000696778. A detached
layer remains exactly dark. The test checks every patch pixel is finite and lit,
without demanding uniform illumination from a light at varying distances.

Other GPU checks cover subpixel source motion, asymmetric capture position,
additive overlap, 1,000 overlapping bright sources, weighted position/depth/radius,
source removal clearing both targets, resize and resource release. The asymmetric
check caught a vertical capture flip before the final recordings.

### Visual evidence

[Reconstruction before and after](../../build/sdf-segment-evidence/reconstruction-before-after.png)
shows the old capture on the left and the current angular-mask capture on the
right, isolating the reconstruction improvement from the visibility replacement.
The horizontal stripes on the bored box disappear in the inspected frame.

- [SDF orbit, bounded segments](../../build/sdf-segment-evidence/Sdf-segments.mp4)
- [SDF orbit, segments plus hidden-source capture](../../build/sdf-segment-evidence/Sdf-hidden-emitters.mp4)
- [Suzanne, segments plus hidden-source capture](../../build/sdf-segment-evidence/Suzanne-hidden-emitters.mp4)
- [Camera-hidden source with a separate blocker](../../build/sdf-segment-evidence/proxies-hidden-blocked/frame.png)
- [SDF poses](../../build/sdf-segment-evidence/Sdf-hidden-emitters-contact.png)
- [Adjacent Suzanne frames](../../build/sdf-segment-evidence/Suzanne-adjacent.png)

The videos contain 960 and 720 deterministic frames at an encoded 60 fps. They
are not a 60 fps performance claim. Eight SDF poses, six Suzanne poses and
consecutive-frame sheets were inspected. These show the intended lighting and
ordering, but do not prove all flicker, visibility aliasing or leakage eliminated.

### Bounds and remaining approximations

Low/Balanced/High visit at most 25/49/81 coarse source tiles with 32/48/64 blocker
samples per occupied tile. That is a conservative maximum of 800/2,352/5,184
sample locations per receiver, versus 96 locations in the angular-mask mode.
Each location can fetch several textures. Empty tiles and blocked paths reduce
actual work. The source refinement and receiver filter also cost work. More
emitters increase capture submission, overdraw and occupied source tiles, but
cannot grow the receiver's loops past this cap.

The walk distributes its budget across the entire segment. It can miss a thin
blocker between samples; it does not report an unfinished ray prefix as clear.
Several sources sharing a tile share a visibility result. Fixed source grouping
removes receiver-driven LOD switches but does not guarantee continuous visibility
when moving sources cross tile boundaries. A source radius is an approximation
for grouped or nonspherical emitters. Only the front layer and an authored
thickness are known. Hidden blockers, offscreen sources, overlapping depth layers
and some thin or sharply curved receiver geometry remain unresolved.

### Validation scope

Native build, Metal and GLES `--radiance` checks passed. This includes the depth
and material-raster regressions. GLES skips compute-only tests by design. The
serial Web build passed; this turn did not run an interactive browser session.
The three new lab scenes and all controlled variants validated through public
MCP authoring calls with project-root checks.

`make test` stopped at the pre-existing noncanonical Arena2D
`content/scenes/Sandbox.kdl`. It is not a passing whole-engine suite. Original
scenes and unrelated WIP were preserved, and changes remain uncommitted.


### Matched timing runs

Apple M4 Pro, 48 GiB RAM, native Metal Debug build with ASAN/UBSAN, under existing
system load. Each scene/mode ran twice in forward/reverse order, frozen after the
specified simulation frame, with 240 warmup frames and 96 retained measurements.
SDF has two emitters, Suzanne has 32, and the dense fixture has 1,016. Balanced
quality, near-source detail, softness 1 and temporal filtering are held constant.
The HRC-only baseline sets depth gain to zero. It omits the extra Depth lighting
and capture work, so it is a cost baseline with a different lighting result.

Ranges below span the two runs. Pass costs are command-buffer timestamp
attributions; frame costs include synchronized rendering and CPU work. A zero
capture attribution in the sparse SDF run does not mean that capture is free.
The stages can overlap in those timestamps, and the loaded system does not
support a precise speedup claim.

| Scene / output | Mode | Surface GPU ms | Capture GPU ms | Frame median ms | Frame p95 ms |
| --- | --- | ---: | ---: | ---: | ---: |
| sdf 960x632 | hrc | 0.000–0.000 | 0.000–0.000 | 4.123–5.728 | 8.290–8.353 |
| sdf 960x632 | masks | 0.617–0.819 | 0.000–0.000 | 5.159–5.583 | 7.619–9.049 |
| sdf 960x632 | segments | 0.496–0.542 | 0.000–0.000 | 5.317–6.110 | 7.411–10.999 |
| sdf 960x632 | proxies | 0.290–0.391 | 0.000–0.000 | 4.752–5.527 | 7.581–9.156 |
| suzanne 960x632 | hrc | 0.000–0.000 | 0.000–0.000 | 6.115–6.893 | 10.420–11.326 |
| suzanne 960x632 | masks | 1.209–1.323 | 0.000–0.000 | 6.384–7.026 | 9.417–10.043 |
| suzanne 960x632 | segments | 0.869–0.993 | 0.000–0.000 | 6.267–6.952 | 10.942–10.947 |
| suzanne 960x632 | proxies | 0.717–0.768 | 0.004–0.011 | 6.666–7.362 | 11.016–12.685 |
| dense 640x360 | hrc | 0.000–0.000 | 0.000–0.000 | 19.735–19.951 | 23.619–23.986 |
| dense 640x360 | masks | 2.776–3.480 | 0.000–0.000 | 22.251–23.784 | 25.318–27.233 |
| dense 640x360 | segments | 3.145–3.704 | 0.000–0.000 | 23.558–24.826 | 26.797–30.079 |
| dense 640x360 | proxies | 2.741–2.783 | 0.069–0.083 | 23.690–23.867 | 24.923–26.923 |

Capturing 1,016 sources costs about 0.07–0.08 ms of attributed GPU time here.
The full depth surface stage with proxies costs 2.74–2.78 ms in that fixture.
The full Debug frame remains around 24 ms, so these tests do not establish
60 fps with thousands of entities. Neither this Mac nor the asset-generation
4080 defines the game's target hardware. Higher resolution, more covered
receivers and more occupied source tiles can increase cost up to the stated
bounds. The results support keeping the experiment available, not enabling it
as a universal default or claiming all 2.5D scenes fit a fixed frame budget.

[Timing records](../../build/sdf-segment-evidence/timings.json),
[conditions and executable/shader hashes](../../build/sdf-segment-evidence/bench-conditions.json),
[local video and image gallery](../../build/sdf-segment-evidence/index.html).

Reproduce the controls with
`python3 build/sdf-segment-evidence/control.py --capture-only`, and the matched
runs with `python3 build/sdf-segment-evidence/bench.py`. Their disposable project
copies, public MCP requests, validation results and render logs remain alongside
the artifacts. The three persistent experiment scenes are in `projects/sdf_lab`.
