# Realtime optics before further HRC tuning

September 21, 2026. Worktree: `.worktrees/gi-research`, branch
`cwaldner/gi-research`, renderer baseline `0e8561f2`.

The first native GPU experiments and corrected hidden-window timing are recorded
in [gi-experiments.md](gi-experiments.md). This document preserves the earlier
research and implementation order; its historical renderer timings include the
old hidden-presentation path.

## Decision

Reflections, refraction and caustics are required features. Realtime performance
is the constraint. Accuracy helps us identify artifacts and choose approximations;
it does not require a production path tracer or a fully converged physical solve.
This supersedes the original research's order of first earning the diffuse/fog
budget and only then adding optional optics.

Keep the existing HRC implementation as a comparison. Build a bounded hybrid
with explicit optical paths, then compare cascade variants at the same complete
frame budget. A prettier slow solver and a faster renderer missing glass are
both insufficient comparisons.

## What the current results establish

The new persistent density field solves particle-sized gaps in fog coverage.
It has not established better overall rendering quality or performance than the
previous renderer. The implementation still lacks physical optical interfaces.

The existing captures report these frozen-scene times on the M4 Pro with other
editor processes running:

| Scene | Output | HRC scale | Median ms | p95 ms |
| --- | --- | --- | ---: | ---: |
| Persistent fog | 2560 x 1440 | .25 | 13.54 | 15.00 |
| Persistent fog | 2560 x 1440 | .5 | 28.13 | 29.58 |
| Persistent fog | 3840 x 2160 | .25 | 29.62 | 32.56 |

The 4K before/after result, 30.05 to 29.62 ms, compares two versions of the new
medium pass. It does not compare the previous game renderer against the new
system. The gain is too small to establish a general improvement under shared
desktop load. The low image difference is between HRC settings, not against a
reference light solution. Dark-background averages can also conceal local errors.
No new realtime timings were taken for this reassessment.

The earlier 1080p opaque-interior results in the main checkout used different
content and experimental luminance paths. Its 6.16 ms color-only result and this
13.54 ms fog result are not a matched regression test. The worktree starts from
committed code, not the main checkout's uncommitted experiments. Preserve both
until a comparison matches scene, exposure, display size, world extent, light
grid, history, material model and active feature set.

The artifact `build-release/fog-evidence/summary-final.json` originally omitted
the final 1440p .25 run. It now includes that run, with its provenance. See
[persistent-medium.md](persistent-medium.md) for the measured implementation and
limitations. Adding that row does not change the disappointing 4K result.

## Half-HRC and other implementations

The original research did not evaluate a specific Half-HRC implementation. The
source inspection here fills that gap; it is not a measured port comparison.
Our own lab note also used "half-HRC" for `scale = .5`, which is already supported.
That setting halves both work-grid dimensions; it does not change the algorithm.
The lab note now calls it "half-resolution HRC" to avoid conflating the techniques.

| Implementation | What the source actually does | What to learn or test |
| --- | --- | --- |
| [Half-HRC](https://github.com/brucelevis/Holographic-Radiance-Cascades/tree/8fbe114f0d5144847a09b7568184be3feb2601c9/Half-HRC) | Four cardinal frusta, power-of-two rays per probe plane, distance-field marching and extra neighboring-probe samples when merging. The README reports small-source instability. The marching function explicitly excludes volumetrics. | A different layout/merge experiment, not a half-resolution setting or demonstrated replacement for dense fog. Compare thin blockers and tiny moving lights before porting. |
| [Yaazarai's Volumetric-HRC](https://github.com/Yaazarai/Volumetric-HRC/tree/ed153d18f284acce9515880614b1091e9669adcf) | Volumetric ray extensions. The author explores extending from the earliest cascade and proposes halving probes along each plane; the latter is described as unimplemented. | Test early extensions as a stability/blur tradeoff, and reduced perpendicular probe density as a distinct experiment. Claimed speedups are not our measurements. |
| [Sannikov's LegitCascades](https://github.com/Raikiri/LegitCascades/tree/82993030166be694004fee806c81d9cc2e496316) | Includes energy-conserving and polygonal HRC experiments. `PolygonalHRC45.ls` gathers by iterating probes and directions per pixel. | Learn from beam coverage and energy weighting. That inspected gather is research code, not a ready fast production pass. |
| [Conventional RC](https://jason.today/rc) | An independent implementation explaining direction layout, pre-averaging and hardware interpolation. | A smaller diffuse challenger with continuity fixes. Do not assume opaque-scene performance transfers to dense participating media. |
| [Tantalum](https://benedikt-bitterli.me/tantalum/) | Forward light paths and spatial reconstruction of 2D fluence; handles specular chains that naive pixel-origin sampling misses. | Use its measurement model and sampling reasoning for caustics. Its beautiful images are not evidence for our full-frame 4K60 budget. |
| [Radiance Cascades Glass & Caustics](https://github.com/BlackLineInteractive/radiance_cascades_glass/tree/dbe5b60e2c4bb97e2efc5e8d028fa730ca06a3ac) | A 3D demo with separate glass shading, floor photon splats and diffuse cache. Its own analysis reports caustic normalization and cache-quality problems. | The separation of passes is useful. Floor-only caustics, forced exits at a bounce cap and mismatched brightness do not satisfy our fog/optics requirements. |

No external implementation code was copied. The named Half-HRC prototype is not
the same as the later Volumetric-HRC proposal to reduce probe density. Neither
automatically adds sharp reflection, refraction or caustics to a gathered RGB field.

## Why the surrounding renderer needs to change

These are source findings, not a ranked GPU profile:

- [radiance.c](../../src/render/radiance.c) ties the transport grid to canvas
  size. It also allocates four full-resolution RGBA16F radiance images and a
  bounce canvas. Lowering `scale` does not lower every pass's resolution.
- [gather.shd](../../src/render/shaders/hrc/gather.shd) performs a near-field
  directional gather at canvas resolution. [blur.shd](../../src/render/shaders/hrc/blur.shd)
  also runs there, with a 3x3 filter and history sampled at the same UV. This
  makes display resolution and camera movement expensive concerns throughout GI.
- The final RGB fluence has integrated away direction. It cannot answer a sharp
  mirror query. Four quadrant averages would still be too broad for a narrow lobe.
- [medium.shd](../../src/render/shaders/hrc/medium.shd) writes fog extinction into
  emission alpha, and transport changes source interpretation when a medium is
  active. [bounce.shd](../../src/render/shaders/hrc/bounce.shd) combines surface
  albedo and medium scattering by extinction weighting. Those shortcuts complicate
  adding glass boundaries and can hide errors behind tuning.
- Existing [reflection](../../src/render/reflections.c) and liquid UV distortion
  are inexpensive presentation effects. They do not transport light through
  reflected/refracted paths or produce optical caustics.

Refactor by responsibility, when each responsibility gets a real caller:

1. **Material and density input.** Keep geometry boundaries, normals, material
   identity and IOR separate from the participating-medium density. Use world
   units for extinction and source strength. A gas solver or later MPM supplies
   density/velocity without choosing the GI algorithm. Preserve material IDs at
   interfaces; do not average glass/air IOR into a blurred coefficient.
2. **Diffuse and medium lighting.** Give this a bounded world-aligned grid with
   a maximum cell count. Retain light/history/bounce at that resolution where
   possible. HRC and a later challenger consume the same fields. Avoid introducing
   a general renderer plugin interface before the second solver exists.
3. **Optical paths.** Share intersection, normal and material rules between
   reflection/refraction queries and light-origin caustic paths. It gets explicit
   ray, interface-event and buffer budgets.
4. **Reconstruction and presentation.** Reconstruct once to display resolution.
   Keep exposure, bloom and artistic fog controls out of transport comparisons.
   Reproject history with camera/world coordinates and reject it at changed
   material interfaces. Sharp optical detail needs its own history/rejection.

The immediate objective is to remove duplicated display-sized lighting work,
not to turn `radiance.c` into many files with the same resource schedule.

## A practical realtime feature set

Use planar transport first. It is compatible with dense 2D fog and a bounded
lighting grid. Keep presentation separate: a 2D fluence image is not a camera
image. Recognizable reflections on camera-facing glass need a chosen view model,
such as shallow-depth surfaces with authored normals and thickness. The existing
planar mirror is a useful cheap presentation tier while that view model is tested.

| Feature | First realtime approach | Cost control and acceptable compromise |
| --- | --- | --- |
| Reflection | Explicit reflected rays at optical interfaces; retain planar image reflections for suitable authored surfaces. | Trace only reflective regions. Rough reflections may use the diffuse cache after a short ray. Preserve a sharper path for mirror-like surfaces. |
| Refraction | Snell direction, Fresnel split/selection, entry/exit state and Beer absorption. | Start with 2-4 interface events, plus the final outgoing segment. Use Schlick Fresnel where its error is inconspicuous, but handle total internal reflection explicitly. Track discarded throughput instead of forcing trapped rays through a surface. |
| Caustics | Trace from emitters through specular interfaces; deposit redirected energy onto receivers and along fog segments. | Fixed path count and event count, quarter/half-resolution target, finite footprints, spatial filtering and short validated history. Permit broad caustics at lower tiers. Avoid solving a random glass chain for every display pixel. |
| Continuous fog | Persistent density on its own grid, extinction along optical paths, diffuse illumination from the light grid. | Coarser density integration, isotropic scattering and limited feedback iterations initially. Directional scattering can be a later quality tier. |
| Multiple bounce | Diffuse cache feedback plus the caustic scattering/receiver source. | Reuse stable history; allow delayed low-frequency bounce response. Reject trails at moving lights, occluders and optical boundaries. |

For smooth glass, deterministic reflected/transmitted splitting is useful for a
small number of prominent paths. It can grow exponentially with interface count;
the realtime tracer should use branch budgets or Fresnel sampling beyond that
point. Stable source samples reduce shimmer. Rotate or scramble them only where
history can reject stale samples. A perfectly sharp singular caustic is not a
useful finite-resolution target; emitter size and footprint should bound it.

Fresnel, Snell and TIR follow the
[PBRT specular model](https://pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission).
Our first implementation can approximate their weights, but must retain the
right ray directions and blocking behavior. Fixing a physically subtle lobe is
lower priority than eliminating visible flicker or saving several milliseconds.

Avoid duplicate light. As an initial hybrid partition, the diffuse solver treats
specular boundaries as blockers. Light tracing supplies paths redirected by those
boundaries, and deposits their first diffuse/volume interaction as a new source.
Do not add the full light-traced direct field on top of the existing direct field.
This partition initially misses some diffuse-to-specular chains; document and
judge that bias before adding an expensive general path sampler.

Use provisional lighting budgets of about 8 ms at 1440p and 10 ms at 4K, leaving
part of the 16.67 ms frame for simulation, rasterization and presentation. Within
lighting, first investigate roughly 1-2 ms for sharp optics at the baseline tier.
These are experiment allocations, not measured feasibility or a chosen minimum
GPU. If optics misses its allocation, reduce its samples/footprint resolution or
scope before spending the rest of the frame. GPU contention with future MPM must
be measured with both running.

## Implementation order and acceptance

1. Establish small mirror, glass slab, lens, prism and fog-beam references.
   Keep analytic checks for directions and gross energy errors. Do not require
   full physical convergence of the production renderer.
2. Implement one GPU optics slice with fixed budgets: an emitter, moving glass,
   a reflected path, a focused caustic and a diffuse receiver in continuous fog.
   Integrate it into the authored lab and a second scene without combat. Resolve
   the camera-facing reflection model explicitly instead of calling UV distortion
   physical refraction.
3. Measure that slice with diffuse GI, fog and presentation all enabled. Move
   lighting/history off the display grid and fix resource ownership as needed
   to give optics room. This is structural scaling work, not tuning an incomplete
   feature set to its best screenshot.
4. Compare early-extension HRC, reduced probe density and corrected conventional
   RC behind the same optical scene. Adopt a variant only for a better image at
   a matched frame budget, including memory and moving-image artifacts.
5. Tune quality tiers with camera pans, zoom, tiny moving emitters, rotating thin
   mirrors, moving glass, emitter removal and a dense-fog stress case. Record
   complete-frame p50/p95/p99, work-grid sizes, memory and backend. GPU labels
   alone are not GPU timings.

Compare linear regional energy and feature positions where diagnostic, but decide
on fixed-exposure images and motion at equal time. A cheap broad caustic can win
over a sharper noisy one. Slower high-quality reference captures remain offline.
All runtime tests must keep their hidden background windows; pure reference tests
use neither a window nor a GPU.

## What changed in this reassessment

Added [optics_reference.py](../../tools/optics_reference.py), a standard-library,
CPU-only light tracer used solely for optical reference captures and checks.
It supports mirror paths, dielectric entry/exit, nested IORs, TIR, colored interior
absorption and lens/prism focusing. A separate density grid attenuates beam
segments and records the first-scattering source. Exact cell path lengths make
attenuation independent of output resolution. Output includes linear HDR PFM,
fixed-exposure PNG and a power ledger including unresolved paths at the event cap.

This tool does not solve diffuse/volume rescattering, spectral dispersion or a
view-dependent camera image. It is not a new realtime backend. Its CPU render time
must never be presented as our GPU cost. The runtime renderer is unchanged by
this reassessment; GPU optics integration remains the next implementation step.

`make test-optics` runs 13 checks without a graphics context. They include an
analytic infinite internal-reflection series through absorbing glass, a lens's
paraxial focus, exact transmission through a heterogeneous density field at
different output sizes, redirected mirror power, nested water/glass and trapped
rays. The ordinary `make test` target includes these checks too.

Example reference capture, not a realtime benchmark:

```sh
python3 tools/optics_reference.py --scene lens --rays 4096 \
  --width 480 --height 270 --events 12 \
  --output build-release/optics-evidence/lens-reference
```

The independent [2D transport formulation](https://cs.dartmouth.edu/~wjarosz/publications/jarosz12theory.html)
and Tantalum motivate the fluence measurement. Forward paths carry power, so
they do not receive the radiance-mode eta-squared factor. The reference uses a
box pixel footprint and deterministic midpoint source quadrature. Sample-count
convergence and event-budget bias must be evaluated separately.

The first lens experiment supports trying a small interface budget. At 480 x 270
with 2048 source samples, four events leave at most **0.00946%** of emitted power
unresolved. Against twelve events with the same source samples, relative linear
L1 difference is **0.00376%** over the image and **0.00516%** in the focal region.
Power accounting closes within `3e-12` across the lens runs.

Separately, doubling source samples from 2048 to 4096 at twelve events changes
the image by **1.14%** relative L1 overall and **0.0209%** in the focal region.
Thus the reference still has source-quadrature error, especially along the
collimated input beam. It should not be labeled fully converged ground truth.
The focal region spans normalized X `.52..68` and Y `.39..61`.

These results bound image bias for one lens, not realtime GPU performance or the
necessary path depth for every optical scene. The trapped-ray test deliberately
retains all power as unresolved. Lens, mirror and prism captures, HDR data and
the separate comparison metrics are in the ignored
`build-release/optics-evidence/comparison.html` and `comparison.json` artifacts.
