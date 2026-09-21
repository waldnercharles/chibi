# First native GPU optics experiments

September 21, 2026. Worktree `.worktrees/gi-research`; starting commit `39228e2f`.

## What is implemented

A separate `gi_lab` native executable now runs three GPU optical transport
candidates without showing a window or taking desktop focus. Its shaders and
analytic fixture content live in `tools/gi_lab`, outside the game renderer.
`tools/benchmark_optics.py` runs matched references, repeats, HDR comparisons,
emitter-removal and moving-glass checks, and writes a comparison gallery.

- **Trace:** light-origin paths with deterministic Fresnel reflection/transmission,
  Snell directions, total internal reflection, colored glass absorption and
  persistent heterogeneous fog extinction. Four interface events, including the
  final outgoing segment. A depth-first stack bounds storage to five entries and
  execution to 31 path segments per source sample.
- **Iterate:** a ping-pong directional tile field, four semi-Lagrangian iterations
  per frame. Sampling changes direction at the same analytic optical interfaces.
  It carries history across frames. The intrinsic 2D transmission weight conserves
  radiance divided by IOR; forward power paths have no radiance IOR multiplier.
- **Coarse:** rebuild a coarse directional field, prolong to finer grids, then do
  four iterations at each finer level. Rebuilding removes stale-light history.
  This is coarse-to-fine initialization, not a residual-correction multigrid solver.

The optical work grid is independent of display size. Each candidate reconstructs
once into a display-sized image. This tests the bounded-grid architecture without
changing the production HRC defaults or inventing a renderer plugin interface.

The persistent density is the CPU reference's independent 32x18 world-space
field. Forward paths integrate exact lengths through output pixels, with density
sampled once per crossed pixel. Density integration is therefore approximate at
cell boundaries. Fixed-point integer atomics make path accumulation reproducible.
The grid/event limits keep the accumulator below overflow; resolution and path
count affect footprint and quantization error.

## Grant Kot's references

The author's [Refracting Radiance Cascades post](https://ko-fi.com/post/Refracting-Radiance-Cascades-J3J01V1XZ9)
confirms an iterative lighting solver combined with the directional tile layout
of radiance cascades. Redirecting samples between tiles bends light and produces
caustics. He relates this to his older multigrid lighting work. The linked
[video](https://www.youtube.com/watch?v=N6hcS7Ub2pU) has the same description;
[TSP](https://tsp.grantkot.com/) provides an interactive browser demonstration.

These sources do not disclose a complete solver, convergence schedule, boundary
filter, precision, resource budget or a matched performance measurement. The two
directional candidates here are independent experiments inspired by that account.
Their artifacts are not evidence that Grant's implementation has the same faults.
No external solver source was copied.

## Measurement correction

Hidden GPU tools were still acquiring the window swapchain when ending a frame.
On this macOS host, an empty hidden present cost about 7.4 ms in the diagnostic
run, even with immediate presentation selected. The tracer and 1440p reconstruction
in that run took about 0.8 ms including GPU completion.

`cmake/patches/cute-hidden-present.patch` skips swapchain acquisition for hidden
SDL GPU windows. Offscreen rendering, command submission, synchronization and
readback still run. Visible windows keep the existing presentation path. This
complements the earlier background-app patch that prevents focus and Dock changes.

Old results that included the hidden swapchain wait remain historical evidence.
Do not subtract a constant 7.4 ms from them or call the whole difference a renderer
optimization. The new HRC comparisons rerun both settings with the same fix.

Timings below include CPU dispatch, optical GPU work, one display reconstruction,
and an explicit GPU-completion wait. Compilation, warmup and HDR readback are
excluded. They are **component timings**, excluding scene rasterization, diffuse
GI, simulation and game composition. The machine remained a shared desktop.
Three repeats have 64 measured frames each. Tables use the median of their
medians and the largest repeat p95. Logical buffer/image sizes exclude driver
allocation overhead and the small framework canvas.

## Optical results

Lens scene; all methods include reflection/refraction, absorption and fog.
The reference uses the same source quadrature and four-event budget. Agreement
with it tests implementation consistency, not convergence to arbitrary-scene truth.

| Candidate | Light grid | Display | Median ms | Worst p95 ms | Relative linear L1 | Field resources MiB |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| Trace, 1024 paths | 240x135 | 1440p | 0.445 | 0.532 | 0.0072% | 14.9 |
| Iterate, 32 directions | 240x135 | 1440p | 0.634 | 0.797 | 25.7652% | 30.4 |
| Coarse, 32 directions | 240x135 | 1440p | 1.319 | 2.600 | 33.7354% | 35.6 |
| Trace, 2048 paths | 480x270 | 4K | 1.079 | 1.274 | 0.0020% | 35.1 |
| Iterate, 64 directions | 480x270 | 4K | 3.633 | 4.890 | 24.3359% | 160.2 |
| Coarse, 64 directions | 480x270 | 4K | 7.611 | 9.157 | 26.5793% | 201.8 |

The tracer also matches the slab, prism and mirror references within 0.007% L1
at the 240x135 setting. Increasing fog extinction from .2 to 2 world units^-1
raises disagreement to 0.0404%, consistent with the coarser density integration.
The dense-fog tracer case measures 0.447 ms median and 0.577 ms worst p95.

The initial directional methods use ordinary bilinear interpolation within each
direction tile. They produce a caustic, but angular bands, spatial diffusion and
interpolation across material boundaries damage its shape. The 32-direction lens
focal-region errors are 51.1% for iteration and 57.6% for coarse-to-fine. The focal
ROI is normalized X `.52..68`, Y `.39..61`; it is meaningful for the lens, not a
mirror image with zero reference fluence there. Undefined relative errors are null.

Emitter removal occurs at frame one after warmup:

| Candidate | Remaining fluence, frame 1 | Frame 16 | Frame 63 |
| --- | ---: | ---: | ---: |
| Trace | 0% | 0% | 0% |
| Iterate | 97.8% | 67.0% | 0.78% |
| Coarse | 0% | 0% | 0% |

At 60 fps, frame 16 is about 267 ms after removal. This persistence is unacceptable
for sharp optics. Moving the lens .1 world units in one frame also leaves the old
focus in the iterative field. The tracer updates immediately and matches the
shifted reference within 0.008% L1. The coarse solve clears history immediately,
but retains a broad and biased focus. These are step-response tests, not a complete
moving-camera or continuous-animation stability evaluation.

## Boundary sampling follow-up

Visual inspection found that the initial directional prototype skipped an
intersection at distance zero. When a grid center lay on the mirror, the direct
beam leaked through that point and propagated downstream. Directional queries
now accept that hit and select the material on the outgoing side. The forward
tracer still rejects self-intersections after offsetting its new ray origin.
A GPU regression checks the downstream beam region of this grid-aligned mirror.

The final main matrix includes that correction. The original captures remain in
`optics-initial` for diagnosis and must not be used to judge the solver family.

An optional boundary filter rejects bilinear taps from the wrong glass material
or across a mirror. After the intersection fix, the lens iteration L1 error falls
from 25.8% to 23.7%, but costs 1.515 ms median at 240x135 instead of about .6 ms.
The filtered coarse lens measures 3.664 ms and 32.8% L1. The filtered iterative
mirror measures 2.280 ms and 5.16% L1; the filtered coarse mirror measures 4.999 ms
and 17.3% L1. These candidate costs include the same 1440p reconstruction.

Filtering does not solve propagation delay. At frame 16 after light removal, the
filtered iterative field still contains 70.3% of its earlier fluence. Boundary
identity helps spatial correctness, but needs a more efficient implementation and
a different update strategy before it is a useful sharp-optics solution.

## HRC scheduling result

Fresh frozen `HRCLabPersistentFog` captures, scale .25, two repeats, 240 warmup
frames, 96 render frames with the first 16 discarded. These include the scene's
rendering and GPU completion, unlike the isolated optics table above.

| Output | Traced levels before extension | Repeat medians ms | Repeat p95 ms |
| --- | ---: | --- | --- |
| 1440p | 3 | 10.22, 10.81 | 11.39, 12.24 |
| 1440p | 1 | 9.78, 10.21 | 11.29, 11.82 |
| 4K | 3 | 22.08, 18.55 | 23.56, 19.96 |
| 4K | 1 | 22.99, 18.60 | 24.36, 21.51 |

Early extension offers a small possible 1440p gain and no demonstrated 4K gain.
It changes fog brightness: mean RGB display difference is about 2.4/255, maximum
18/255 at 1440p and 20/255 at 4K. About 35% of pixels differ by more than 3/255.
These are differences between settings, not errors against a physical reference.
There is insufficient benefit here to change the default. 4K60 remains unproven.

## Decision and remaining experiments

Bounded forward tracing is the strongest current candidate for sharp planar
optical paths. It earns continued integration work. The directional experiments
need better boundary handling and propagation before they can compete for this
role. Their potential as low-frequency diffuse/fog solvers is still open.

The next integration must consume authored optical materials and an acceleration
structure, deposit the caustic scattering/receiver source into diffuse GI, avoid
double-counting direct light, and choose the camera-facing reflection model.
The current executable measures ballistic planar fluence. It does not yet provide
camera-surface reflections, diffuse receivers or multiple scattering, nested
materials, arbitrary scene geometry, or a production renderer replacement.
Analytic intersections are much cheaper than arbitrary-scene traversal; these
numbers must not be extrapolated to scene complexity we have not tested.

Experiments now running are bounded tracing, iterative directional transport,
coarse-to-fine propagation, material-aware sampling, independent optical/display
resolution, and early HRC extensions. A Half-HRC layout port, reduced perpendicular
probe density, conventional RC, and moving the production HRC gather/history/bounce
off the display grid remain separate outstanding experiments. None is implemented
by merely selecting a smaller HRC scale. The engine's full-resolution lighting
passes still need structural work before adding a costly simulation such as MPM.

## Reproduce and inspect

```sh
make build BUILD_DIR=build-release \
  LOCAL_CMAKE_FLAGS='-DCMAKE_BUILD_TYPE=Release -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_CCACHE=OFF'
make test-optics-gpu BUILD_DIR=build-release
python3 tools/benchmark_optics.py --output build-release/gi-experiments/optics
python3 tools/benchmark_optics.py --filtered --output build-release/gi-experiments/filtered-boundary
```

The GPU regression command compares all four optical fixtures against independently
implemented CPU references with a 0.3% L1 bound. It also checks finite, nonnegative
HDR output and immediate emitter removal for the stateless candidates. The
iterative candidate's measured lag is a failing design property, not hidden by an
assertion that accepts it as correct motion behavior.

Artifacts, including commands and source/binary hashes:

- `build-release/gi-experiments/optics/comparison.html` and `summary.json`.
- `build-release/gi-experiments/filtered-boundary/comparison.html` for boundary filtering.
- `build-release/gi-experiments/hrc/` for repeated HRC captures and comparisons.
- `build-release/gi-experiments/make-test.log` for the full native suite.

Keep these artifacts ignored; the executable, scripts, shaders and this measured
record are versioned. The CPU-only `make test-optics` remains windowless and needs
no GPU.

Validation completed: the full Release `make test` suite, the hidden native
`test_suite --radiance` checks, the new physical optics checks and the new
mirror-boundary regression. The local build uses the project's native Metal
backend. There is no browser/WebGL optics implementation in this change.
