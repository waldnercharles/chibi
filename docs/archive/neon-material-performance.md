# NeonSwarm performance and gas appearance

For the expanded stress suite, frozen comparisons and replayable GPU traces, see
[GPU benchmarks and captures](neon-gpu-benchmarks.md). The measurements below
record earlier implementation passes.

## Measurement boundary

NeonSwarm now defaults to fixed HRC scale 0.5 with adaptation disabled. This is
a quality choice, not evidence that full-resolution lighting is cheap. Material
silhouettes and fluid density still render at output resolution.

`tools/benchmark_neon_materials.py` copies the project into a temporary directory,
pins scale through Chibi authoring, and runs the same move/fire input each time.
It advances 240 simulation frames, warms the renderer for 240 frozen frames,
then advances and renders 180 frames. Trace levels are pinned and recorded too.
The helper defaults to one directly traced level; historical measurements below
used three unless stated otherwise. Discarding the first 16 leaves 164 samples.
The reported frame time includes GPU completion and excludes the preview's
60 Hz pacing sleep. Simulation and rendering/presentation are host wall times.
The latter includes submission, window work and GPU waiting, not isolated GPU
execution. No per-pass GPU timestamps are exposed by the current backend.

```sh
python3 tools/benchmark_neon_materials.py --output /tmp/neon-half --scale .5
python3 tools/benchmark_neon_materials.py --output /tmp/neon-full --scale 1
python3 tools/benchmark_neon_materials.py --output /tmp/neon-800 --width 1280 --height 800
```

Tracy uses the same scenario and a symbolized build. The matching upstream
`capture` and `csvexport` tools can be built from `build-tracy/_deps/tracy-src`.

```sh
python3 tools/benchmark_neon_materials.py --build build-tracy \
  --mode live --scale .5 --tracy-capture build-tracy/capture/tracy-capture --output /tmp/neon-tracy
build-tracy/csvexport/tracy-csvexport -u /tmp/neon-tracy/travel-live-0.5-1/capture.tracy > /tmp/neon-zones.csv
```

For zone comparisons, select the last 164 `Preview simulation` intervals and
their corresponding render work. The capture also contains startup and frozen
warmup. `Scene preview frame` includes pacing, so its average is not the work
benchmark. Worker CPU totals overlap; do not add them to main-thread elapsed time.
Use release captures for the reported frame budgets, Tracy to explain them.

## Baseline findings

Local Apple M4 Pro (20-core GPU) release run, before this pass's optimizations,
with adaptation off:

| Output | Fixed HRC scale | Median | p95 | Simulation mean | Render/present mean |
| --- | --- | --- | --- | --- | --- |
| 1280×800 | 0.5 | 11.87 ms | 13.69 ms | 4.29 ms | 7.42 ms |
| 1920×1080 | 0.5 | 16.58 ms | 17.66 ms | 4.36 ms | 11.74 ms |
| 1920×1080 | 1.0 | 40.83 ms | 42.86 ms | 4.55 ms | 36.18 ms |

The scene ends with 144 entities, 161 ordinary visual particles and 8,787 fluid
particles, 5,296 active. A matching Tracy capture measured 2.55 ms/frame in fluid
reaction queries, 0.45 ms density, 0.45 ms relaxation, 0.29 ms collisions,
0.005 ms terrain/material edits and 0.21 ms material collection. Rendering scales
strongly with HRC quality; caching terrain edits cannot fix that cost by itself.
These short local runs are not a Steam Deck performance claim.

## Implemented changes

- Heat queries read an immutable particle/body snapshot and run on the existing
  worker pool. Ignition, fuel consumption and blast events commit in stable
  particle order afterward. Small ranges distribute localized burning regions.
  Serial/parallel tests compare particle state and blast order, including heat
  propagation. The material hash at frame 420 remains `36a09c555bccd718` before
  appearance tuning.
- HRC extension skips its duplicate second candidate for even ray directions.
  Both candidates contain exactly the same intervals in that case.
- HRC quadrants share interval and ping-pong scratch buffers. Dispatches already
  finish each quadrant's solve before starting the next; only the four final
  frusta need separate storage. This removes 75% of those scratch allocations,
  without reducing resolution, cascade count or color precision. It does not
  remove 75% of total GPU memory or work.
- Tracy now includes fluid stages and material/HRC submission zones. Renderer
  library instrumentation is enabled only in profiling builds.

## Previous particle-solver measurements

Same local release scenario with the final fog/material appearance included:

| Output | Fixed HRC scale | Median | p95 | p99 | Simulation mean | Render/present mean |
| --- | --- | --- | --- | --- | --- | --- |
| 1280×800 | 0.5 | 10.29 ms | 11.27 ms | 11.77 ms | 2.80 ms | 7.51 ms |
| 1920×1080 | 0.5 | 14.31 ms | 15.21 ms | 15.44 ms | 2.78 ms | 11.45 ms |
| 1920×1080 | 1.0 | 37.77 ms | 39.22 ms | 39.60 ms | 2.83 ms | 34.96 ms |

The 1080p/0.5 p95 improved about 14%, simulation mean about 36%. Full-scale HRC
still exceeds a 60 Hz budget. The final material hash is `23e3989a815ac7ae` at
all three settings; the appearance tuning intentionally changes it from the
baseline. Particle populations and material reactions are unchanged.

The final runtime optimization's Tracy capture measured 1.045 ms/frame in
reactions versus 2.554 before, with density/relaxation/collision work roughly
unchanged. Initial 256-particle reaction jobs gave only a small improvement:
burning work is localized, so 32-particle jobs balance it more effectively.
This capture preceded camera-only fog/color tuning; the final table includes it.

Scratch allocation arithmetic at 1080p includes the 16-pixel view padding:

| HRC work dimensions | Interval + ping-pong buffers before | After | Saved |
| --- | --- | --- | --- |
| 976×556 (0.5) | 314.8 MiB | 78.7 MiB | 236.1 MiB |
| 1952×1112 (1.0) | 1325.0 MiB | 331.3 MiB | 993.8 MiB |

These are requested buffer sizes, not a GPU memory-residency measurement. Shared
scratch is primarily a memory improvement; do not attribute the whole speedup
to it. Before appearance tuning, the optimized 1080p frame differed by at most
one 8-bit color value from the baseline. GPU transport/shadow/refraction checks
also pass after reuse.

Artifacts for this run are in `/tmp/neon-material-world`: `fixed-before-*`,
`fixed-optimized-*`, `fixed-scratch-*`, `tracy-{before,optimized,scratch}.tracy`,
their exported zone CSVs, `tracy-comparison.json`, and final `final-fixed-*`
directories containing reports and rendered frames. Temporary captures are not
committed assets; the commands above reproduce the scenario.

## Fog and material appearance

The cold-gas render kernel has 1.5 times the old support radius, with density
divided by 2.25 to compensate for area. Velocity stretches it along flow and
narrows it across flow. Conservative ellipse bounds avoid binning the whole
square around a long wisp. A broad density fade suppresses isolated parcels,
and the old four-lobe contours are removed. This is analytic density rendering,
not a screen blur or additional simulated particles. Exhaust's smooth union
and combustion's hot cores retain their separate presentation.

Fuel gas is amber and has a stronger camera visibility floor; liquid is teal
with a faint body floor. Per-particle color and visibility are combined before
averaging, so ordinary fog does not desaturate the fuel's visibility contribution
as strongly. These values do not enter HRC emission or bounce feedback. The
existing liquid remains nonflammable; this pass changes its appearance.

GPU checks cover connected gas, faint isolated parcels, rotated wisp support
across tile boundaries, HRC illumination and wall shadows, cold-liquid zero
emission, and refraction confined to liquid. A 24-second movement capture still
shows dense pockets as flow slows. Eliminating those entirely requires changing
the gas motion model, replenishing atmosphere, or a more aggressive visual fade;
that pass did not provide a persistent pressure field. The follow-up below changes the gas motion model.

Final validation passed: `make build`, `make build-release`, `make test`, the
native `--materials` and GPU `--radiance` suites, and
`tools/check_neon_swarm.py --build build-release`. The benchmark tool completed
all three fixed-scale configurations. `git diff --check` is clean.

## Reference techniques and decisions

The [Tutti Space Program](https://tsp.grantkot.com/) settings currently expose
particle merging and low-resolution gas. Its [served renderer source](https://tsp.grantkot.com/assets/singleton-CC0WmPkn.js)
was fetched again and matches the previously inspected version. It interpolates
particle poses, deforms disk splats, changes size/color with temperature, and
uses a separate optical field for lighting. Its WASM interface exposes occupancy
merge/split and cheap-gas controls. That establishes available controls, not the
details of its compiled solver or automatic FPS-based adaptation.

For our engine, merge/split is a possible later CPU optimization. It needs to
preserve mass, momentum, fuel and heat, and avoid visual pops; adding particles
when FPS rises would otherwise change gameplay. Interpolating render positions
can improve motion above the fixed simulation rate, but does not reduce solver
cost. Neither is necessary to replace the current fog's beaded density surface.

Mike Turitzin's [SDF engine video](https://www.youtube.com/watch?v=il-TXbn5iMA),
including its description and auto-generated transcript, explains cached sampled
distances (4:15), sparse surface bricks (8:43), clipmaps (11:44), and spatially
bounded incremental updates (12:58). The most relevant transfer is to cache
expensive field evaluation and rebuild only affected regions. Our cave contours
already rebuild on edits; a GPU distance atlas would also avoid re-rasterizing
many contour primitives. Today's measurements do not make that the first priority.
In a top-down 2D view, perspective distance does not shrink distant visible
objects, so the 3D clipmap detail rule cannot simply be copied.

[Valve's distance-field magnification paper](https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf)
supports reconstructing crisp boundaries from interpolated distance samples.
That is directly useful in 2D. It does not preserve arbitrarily small topology
or eliminate rounded sharp corners in a coarse single-channel field.

[Söderlund, Evans and Akenine-Möller](https://jcgt.org/published/0011/03/06/paper-lowres.pdf)
study 3D grid traversal, intersections and continuous normals. Their hardware
ray-tracing results are not evidence that our 2D raster needs RTX hardware.
Sparse storage and coherent boundary sampling transfer more directly.
[Geometry clipmaps](https://hhoppe.com/geomclipmap.pdf) offer bounded incremental
cache updates, but represent heightfields. Our caves need a field with holes.

The [Quilez article index](https://iquilezles.org/articles/) and direct 2D distance
articles returned access errors during this pass. No new implementation claim
is based on unread article contents.

## Lower-resolution color with full-resolution luminance

The proposed reconstruction is valid when a useful full-resolution luminance
image already exists. It preserves coarse RGB ratios and restores brightness.
It cannot recover a sharp shadow from a luminance image derived from the same
coarse lighting, nor distinguish two nearby differently colored lights.

Our cascade buffers already pack RGB radiance plus scalar optical depth into
eight bytes. A monochrome variant could pack luminance/depth into four, but it
would still traverse all full-resolution rays. Adding a second coarse RGB solve
also adds dispatches, reads and temporal history. The shader multiply alone is
not a lighting speedup. Full-resolution silhouettes and emission already remain
sharp in the current reduced-resolution lighting pipeline.

A useful future experiment would compare full RGB HRC against full-resolution
monochrome transport plus half-resolution chroma, at identical scenes and shadow
quality. Clamp unreliable chroma near black; validate saturated crossing lights,
thin occluders, moving shadows and temporal color trails. Add actual GPU pass
timestamps before undertaking that larger pipeline change.

## Next measured experiments

1. GPU timestamps for trace, extension, merge, gather and material raster; host
   submission zones cannot distinguish ALU cost from memory bandwidth.
2. Compact or spatially index reaction occluders if heat queries remain dominant
   during large simultaneous ignitions. Preserve blocking through thin walls.
3. Cache terrain distance tiles if traversal/destruction captures show material
   preparation or rasterization growing. Include neighboring samples and dirty
   borders so edits do not introduce seams.
4. Compare gas grid advection or conservative merge/split with the particle
   solver only when population cost warrants it. Keep simulation quality separate
   from HRC quality and record both in every benchmark.

## Persistent flow and HRC follow-up

The follow-up implements [persistent gas flow](neon-gas-flow.md), shorter render
ellipses, earlier HRC ray extension, a full-scale gather shortcut, and GPU labels.
HRC scale remains fixed at 0.5 in NeonSwarm. Direct tracing now stops after one
level instead of three; higher levels use the existing extension algorithm.

Fresh local release comparisons made before changing gas motion:

| 1080p variant | Scale | Median | p95 |
| --- | --- | --- | --- |
| Three trace levels, original gather | 1.0 | 34.26 ms | 35.04 ms |
| One trace level, original gather | 1.0 | 33.32 ms | 33.91 ms |
| Three trace levels, specialized gather | 1.0 | 33.99 ms | 34.95 ms |
| One trace level, specialized gather | 1.0 | 33.46 ms | 34.46 ms |
| Three trace levels | 0.5 | 13.71 ms | 14.24 ms |
| One trace level | 0.5 | 13.46 ms | 13.97 ms |

The full-scale gather fetches one probe per quadrant when work and output grids
match, avoiding redundant reconstruction. Its isolated measured gain is small
enough to need GPU timing before attributing a reliable speedup. Earlier extension
saves about 0.9 ms at full scale and 0.25 ms at half scale in these short runs.
These are complete frame measurements, not pass timings.

The gather-only image differs by at most 3/255 per channel, with a mean absolute
difference of 0.00195/255. Earlier extension differs by at most 5/255, mean
0.1922/255. These image comparisons use the same simulated state, whose material
hash is `23e3989a815ac7ae`. They establish small differences in this scene, not
universal equivalence. Liquid lighting and wall-shadow tests pass at both settings.

The gas field changes simulation work as well as appearance. Its frame-420 state
has 8,783 particles, 5,675 active, and no burning particles, versus 8,787, 5,296
active and nine burning before. Fuel particles remain at 267. Both runs include
the same move/fire inputs, but their different reactions prevent treating the
whole simulation-time difference as an optimization result.

Final release measurements, one directly traced HRC level, adaptation off:

| Output | HRC scale | Median | p95 | p99 | Simulation mean | Render/present mean |
| --- | --- | --- | --- | --- | --- | --- |
| 1280×800 | 0.5 | 8.87 ms | 9.19 ms | 9.44 ms | 1.72 ms | 7.17 ms |
| 1920×1080 | 0.5 | 12.34 ms | 12.70 ms | 13.08 ms | 1.62 ms | 10.72 ms |
| 1920×1080 | 1.0 | 32.20 ms | 32.85 ms | 33.15 ms | 1.70 ms | 30.57 ms |

These sequential runs had no concurrent build. The final material hash is
`180c8b2f62132bf4` at all three settings. Full-scale HRC still misses 60 Hz on
this M4 Pro. The 1280×800 result is a local resolution check, not a Deck result.

A matching Tracy capture measures gas flow at 0.431 ms/frame, reactions at
0.049 ms, density at 0.199 ms, relaxation at 0.343 ms, collisions at 0.118 ms,
indexing at 0.054 ms and material collection at 0.200 ms. Total simulation
elapsed time in that instrumented run is 1.684 ms/frame. The large decrease in
reaction work reflects the changed burning state described above. These CPU
scopes do not explain the remaining GPU rendering cost.

Validation passed: debug, release and Tracy builds; `make test` with native
macOS watcher access; material tests including flow and worker parity; GPU
lighting tests at both trace settings; NeonSwarm acceptance; and an independent
noncombat gas scene. `git diff --check` is clean. Reports and frames are in
`/tmp/neon-flow-implementation/final-{half,full,800}`; the matching Tracy capture,
zone CSV and stage summary are in `tracy-final` in the same directory.

Full Xcode GPU profiling remains unavailable on this machine: only Command Line
Tools were found. GPU command labels are ready for trace, extension, merge,
gather, filtering, bounce and fluid density captures. Tracy's new `Gas flow`
zone measures CPU work. See [GPU profiling tools](tracy.md#gpu-pass-profiling).
