# Compact HRC radiance buffers

The first retained HRC optimization packs angular radiance into four bytes per
sample and removes unused terminal buffers. NeonSwarm keeps its authored HRC
scale, ray count, tracing levels, optical depth, temporal feedback and material
settings. This changes GPU storage, not simulation or scene composition.

## Representation and tradeoff

The merge ping-pong buffers and four final directional buffers previously stored
three half-float color channels plus an unused half-float channel. They now use
RGB9_E5 in a 32-bit storage-buffer element. Merge and gather share the codec in
`src/render/shaders/hrc_common.shd`. The encoding follows the finite-value rules
in the [Khronos shared-exponent specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_shared_exponent.txt).

RGB9_E5 uses nine mantissa bits per channel and one shared exponent. It preserves
HDR range, but a weak channel beside a much brighter channel has less precision.
Values saturate at 65,408. This is a deliberate precision tradeoff, not lossless
compression. Optical intervals retain their original RGB16F radiance and half-float
optical depth, so interpolating opaque and transparent paths retains its original
behavior. No additional sampling or backend-specific feature is required.

The terminal `T_N` allocation was never read. The terminal `R_N` was a large zero
buffer; its zero boundary is now explicit in the merge shader. Existing valid
buffers satisfy the bindings, and the terminal branches return before loading
them. Scratch allocation sizes now cover only cascades that execute.

## Controlled timing

Apple M4 Pro with 20 GPU cores, Metal, Release, 1920×1080. The travel endpoint is
frozen, with 240 warmup frames followed by 180 rendered frames and the first 16
discarded. Three baseline/candidate pairs ran sequentially in alternating order.
The table shows the median of the three per-run statistics. Both variants replay
the same saved project and must match its complete endpoint state.

| Fixed HRC scale | Before median / p95 | After median / p95 | Median improvement |
| --- | --- | --- | --- |
| 1.0 | 30.283 / 31.050 ms | 25.270 / 25.935 ms | 16.6% |
| 0.5 | 10.958 / 11.356 ms | 10.512 / 10.879 ms | 4.1% |

These are complete rendering-work measurements including GPU completion, not
per-pass GPU timestamps. They establish a local improvement; full-scale rendering
still exceeds the 16.67 ms budget before simulation. This is not a Steam Deck
measurement.

Requested hierarchy buffer storage, including intervals, merge ping-pong and
directional outputs, changes as follows. These totals exclude images, small
angular-weight buffers and driver allocation overhead.

| Work dimensions, including view padding | Before | After | Saved |
| --- | --- | --- | --- |
| 1952×1112, scale 1 | 428.0 MiB | 303.4 MiB | 124.6 MiB |
| 976×556, scale 0.5 | 102.9 MiB | 71.7 MiB | 31.2 MiB |

These are allocation-size calculations, not measured device residency or total
engine GPU memory. Packing halves radiance storage; removing the terminal buffers
accounts for the rest of the reduction.

## Experiments rejected

- A 64×4 thread group instead of 16×16 preserved the image but did not produce a
  reliable speedup.
- Storing transmission instead of optical depth was much faster but darkened
  broad areas. Half-float transmission underflows to zero before later angular
  averages can recover partially visible paths. This version was removed.
- Hoisting constant attenuation, using base-two exponentials, and an exact
  half-float optical-depth lookup table did not improve timing.
- Packing interval color to cache transmission while retaining optical depth
  was slightly slower than packing angular radiance alone. It was removed.

A temporary probe serialized pass completion and attributed roughly 8.8 ms to
extension and 14.4 ms to merging before optimization. Other HRC groups were below
1 ms each except the four trace groups together. Forced synchronization changes
scheduling, so this probe only guided experiments. It was removed from the code.
Native GPU counter inspection remains pending.

## Validation and artifacts

The GPU regression exercises the production RGB9_E5 codec against reference
encodings for zero, very dim light, unequal channels, HDR saturation and exponent
rollover. Existing independent rendering fixtures cover occlusion, translucent
media, colored fog/liquid illumination, indirect feedback stability, reflections,
reset and resource reuse after resizing.

`make build`, `make test` and the native `test_suite --radiance` GPU regression
all pass. The full suite includes runtime, authored assets, MCP, backend parity,
scene documents and headless startup checks.

All 20 NeonSwarm cases completed: travel, dense fog, ignition, destruction and
camera turns, each live/frozen at fixed scales 0.5 and 1.0. Complete endpoint
states match the previous suite. Comparing the final images, the largest RGB
channel difference is 1/255; mean absolute channel differences range from
0.000040 to 0.003655 on an 8-bit scale. This checks those final frames, not every
animation frame or all possible HDR inputs.

All experiment artifacts are under `/tmp/neon-hrc-opt`. `alternating/summary.json`
contains the controlled timings. `scenarios/` contains the full NeonSwarm suite,
with fixed half/full scales, live/frozen runs, endpoint checks and final frames.
`image-comparisons.json` contains the paired final-image measurements.
The original suite is under `/tmp/neon-benchmark-suite/1080p`.

Use [the benchmark workflow](neon-gpu-benchmarks.md) to prepare another comparison.
Rebuilds change executable fingerprints, so intentional renderer comparisons need
`--allow-renderer-changes` when replaying earlier bundles.

## Sandbox follow-up, September 26

Sandbox now directly traces cascades 0, 1 and 2 (`trace_levels: 3`). The benchmark
tool defaults to the same setting. The 64-unit margin, scale 0.5, full directional
update each frame, and Sandbox's enabled bounce remain in place.

The pass probe now separates HRC tracing, extension and merging. A 64-sample
Sandbox capture attributed about 4.76 ms to merge, 3.59 ms to material raster,
2.85 ms to extension, 2.74 ms to composite, 2.38 ms to gather and 2.12 ms to tracing.
These are host intervals ending in GPU synchronization, not native GPU timestamps.
The waits change scheduling; use this breakdown to locate costs, not to infer
ordinary frame time. Reflections, final fog and presentation are not fully covered
by the marked intervals.

Three small changes were retained:

- Relief uses RG16F for height times coverage and coverage. It previously allocated
  RGBA16F despite consuming only those two channels. Precision and resolution stay
  the same. At the 2188x1348 padded raster this saves 11.25 MiB, reducing this target
  from 22.50 to 11.25 MiB. This is requested base-level texture storage, excluding
  driver overhead, not a device-residency measurement.
- Disabled feedback skips the bounce draw and its mipmap generation. Toggling
  feedback already resets history. Sandbox keeps feedback enabled; this saving
  applies to scenes that disable it.
- A temporal weight of 1 skips history sampling and neighborhood min/max work.
  Invalid or offscreen history also skips that work. The nine-tap spatial filter
  remains unchanged, including in the shared luminance shader.

The following comparisons use a saved pre-optimization executable and shader
tree against the same copied projects, with **three traced cascades on both
sides**. Hardware is Apple M4 Pro, 20 GPU cores, Metal, Release, 1920x1080. Three
pairs ran in alternating order, each with 240 warmup frames and 120 measured
frames. Values are medians of the three per-run statistics.

| Frozen workload, scale 0.5 | Before median / p95 | After median / p95 |
| --- | --- | --- |
| Sandbox opening frame, bounce enabled | 21.099 / 22.114 ms | 20.874 / 22.264 ms |
| 1,024 circular lights, bounce disabled | 16.364 / 16.986 ms | 15.242 / 15.992 ms |

The dense fixture improves by 6.9%; Sandbox's roughly 1% median difference and
slightly worse p95 do not establish a reliable FPS gain. Sandbox still exceeds
the 16.67 ms render budget before simulation. Earlier 29–51 ms measurements are
not the control for this comparison: fresh repeated baselines settled much lower,
so none of that earlier difference is credited to these changes.

One additional moving-camera/firing replay and a frozen full-scale Sandbox capture
check coverage, not statistical performance. Complete initial and final simulation
states match in every pair. Dense captures are identical; Sandbox captures differ
by at most 1/255 in a channel, including the moving and full-scale cases. The frozen
half-scale comparison changes six pixels out of 2,073,600. This checks final frames,
not every animated frame or every HDR input.

Experiments rejected in this pass: empty-radiance and transparent-interval math
shortcuts, explicit power-of-two indexing, per-cascade shader specialization,
16x8 extension/merge workgroups, native material-target clears, and compact active
tile dispatch. They preserved the tested pictures but did not show a useful gain
over matched steady baselines; some added overhead. Their implementations were
removed.

Raw captures, paired states, logs, requests and shader experiments are in
`/private/tmp/hrc-opt-current`. `final-comparisons.json` records timing and image
comparisons; `sandbox3-detail.json` records the pre-optimization pass breakdown.

The full Release `make test` run and native Metal `test_suite --radiance` pass.
The latter covers transparent and opaque transport, indirect feedback, split
luminance, reflections, material channels, temporal reprojection, subpixel
emission energy and resource resizing. Changed C/H files pass clang-format,
canonical asset checks pass, and `git diff --check` is clean.

## Resolution and storage follow-up, September 26

The earlier approximately 21 ms Sandbox measurement used one RGB transport solve
at `scale: 0.5`, with `luminance_scale: 0`. It was not a half-resolution renderer.
At 1920x1080 output, the 64-world-unit margin and alignment reserve produce a
2188x1348 material canvas. RGB transport runs at 1094x674; material raster,
near-field gather, radiance filtering and feedback textures retain the padded
canvas dimensions. Composite renders the visible viewport. Surface lighting uses
its own block-resolution seed and blur.

`luminance_mode: 0` with a positive `luminance_scale` adds independent monochrome
transport. Mode 1 derives luminance from the existing filtered RGB result; it
cannot restore detail lost by the RGB solve. Adaptive scaling changes the RGB work
grid only. Independent luminance keeps its configured scale, and full-canvas
passes do not shrink with the RGB grid.

### Resolution comparison before this follow-up's storage changes

Same M4 Pro/Metal, Release, three directly traced cascades, bounce enabled,
`split_frames: 1`, fixed scales, frozen Sandbox opening frame. Each mode ran twice
in opposite order, with 240 warmup frames and 120 measured frames after discarding
16. Values below average the two per-run statistics. Pass timing was disabled.

| RGB scale | Luminance mode / scale | Median ms | p95 ms | Mean RGB error against unified 1.0 |
| --- | --- | ---: | ---: | ---: |
| 1.0 | Disabled | 50.09 | 51.96 | 0 |
| 0.5 | Disabled | 19.98 | 21.47 | 0.077 |
| 0.33 | Disabled | 15.61 | 16.59 | 0.350 |
| 0.25 | Disabled | 14.26 | 15.21 | 0.365 |
| 0.25 | Independent / 0.5 | 24.03 | 25.20 | 0.228 |
| 0.25 | Independent / 1.0 | 53.14 | 54.91 | 0.202 |
| 0.25 | Reuse / 1.0 | 14.44 | 15.36 | 0.366 |

Image errors are mean absolute channel differences on an 8-bit scale in the final
captured frame. Half scale differs by more than three channel values at 0.0017%
of pixels; quarter scale does so at 1.74%. The dark opening frame is not a general
quality test or a motion-stability test. All 14 runs match the reported state
fields except the world hash, which includes the intentionally different HRC
configuration.

Process samples every two seconds reported substantial CPU activity from an
unrelated virtual machine and compilation jobs. No benchmark or build from this
task ran concurrently with another GPU benchmark. These are exploratory timings
under observed background load, not an idle-machine FPS guarantee. The samples
do not measure GPU contention or establish the cause of the earlier timing
variation. `xcrun --find xctrace` found no usable Instruments CLI.

One live camera/firing replay gave 24.90 ms median / 49.50 ms p95 at half scale
and 19.15 / 29.58 ms at quarter scale, including simulation. The reported
non-configuration state fields matched. These single runs show why the frozen
14.26 ms result does not establish sustained 60 FPS during gameplay; differences
in simulation time and background activity are not renderer improvements.

Four shader-only experiments preserved final captures byte-for-byte but did not
establish a repeatable improvement: shared angular-weight loads, grouping merge
lanes by probe parity, delaying unused composite texture reads, and an early
empty-material-tile clear. They remain outside the repository implementation.

### Compact monochrome intervals

Independent luminance transport previously used the RGB interval layout despite
having three identical radiance channels. Its Trace, Extend and Merge variants
now pack one half-float radiance value and one half-float optical depth into four
bytes, instead of eight. Loads broadcast radiance into the existing RGB arithmetic.
RGB transport and the RGB9_E5 angular-fluence codec are unchanged. This preserves
both optical-depth precision and the established angular rounding.

Two baseline/candidate pairs ran in opposite order for each split configuration,
with the same warmup, sampling and background-load recording as above. Each
comparison used the identical saved project on both sides. These results isolate
scalar storage, before the surface-blur experiments.

| RGB / independent luminance scale | Before median / p95 ms | After median / p95 ms | Median reduction |
| --- | ---: | ---: | ---: |
| 0.25 / 0.5 | 23.76 / 26.72 | 21.68 / 24.49 | 8.8% |
| 0.25 / 1.0 | 52.22 / 55.79 | 40.41 / 44.05 | 22.6% |

All four final-image comparisons are byte-identical, and complete initial/final
reported simulation states match. At the tested padded dimensions, requested
luminance interval-buffer storage falls from 83.48 to 41.74 MiB at scale 0.5,
or from 356.32 to 178.16 MiB at scale 1.0. These figures exclude angular buffers,
textures and driver overhead. Unified RGB and luminance reuse do not receive
this storage reduction because they do not run an independent scalar hierarchy.

Split mode is faster than before, but unified half scale remains the better
measured cost/quality point for this Sandbox opening frame. Its authored default
remains unchanged, including three directly traced cascades.

Two additional surface-blur experiments were rejected. Combining light and
direction layers reduced four dispatches to two and preserved the image exactly,
but did not improve the paired whole-frame measurements. Combining adjacent
Gaussian taps through hardware linear filtering was also neutral and changed
1,476 pixels, with a maximum channel difference of 3/255. Neither change remains
in the implementation.

### Larger opportunities identified before the follow-up experiments

- Separate broad diffuse-light reconstruction/filtering from full-resolution
  near-field detail. Material silhouettes and local visibility still need crisp
  inputs, but broad illumination need not necessarily use full-canvas history and
  filtering. This targets the cost that remains when lowering the cascade grid.
  Compare moving thin occluders and small emitters, not just frozen images.
- Cache static material tiles in world coordinates and invalidate affected tiles
  on destruction, asset changes or animation. Dynamic entities and fluid still
  need current-frame composition in painter order. Time-dependent shader graphs
  cannot be treated as static merely because their entities did not move.
- Use a coarser outer lighting region, or attenuated projected offscreen sources,
  while retaining occluders and fading at the boundary. The current margin adds
  about 42% to material-canvas pixel count. That is not a 42% whole-frame cost,
  since composite already renders only the viewport.
- Inspect native GPU counters before a broader buffer-layout or dispatch rewrite.
  The existing host timing marks identify expensive regions but cannot distinguish
  shader arithmetic, memory traffic and scheduling stalls. Apple's
  [Metal profiling workflow](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app)
  provides the system/GPU evidence missing from these process samples.

Simply cropping Gather to the visible viewport is not a safe shortcut. Surface
lighting reads blurred offscreen fluence and direction fields, and liquids sample
displaced incident light. At the authored surface radius, the Gaussian alone
reaches 192 canvas pixels from its center; seed blocks and gradient sampling add
to that support. Camera reprojection also needs valid incoming history. A cropped
or reduced-resolution design must account for these dependencies explicitly.

The raw requests, project copies, before/after shader trees, process samples,
render responses and PNGs are in `/private/tmp/hrc-next`.

Validation of the retained scalar-storage change: full Release `make test` and
native Metal `test_suite --radiance` pass, including split-luminance bounce,
transport through opaque/translucent media, surface lighting, camera history,
resource resizing and material sampling. The four matched scalar captures are
byte-identical to their baseline captures. Changed C passes clang-format and
`git diff --check` is clean. The rejected shader experiments are not in the
working implementation.


## Feature costs and follow-up experiments, September 26

These measurements use the retained scalar-interval implementation as their
baseline. Sandbox renders at 1920 x 1080, unified RGB transport scale 0.5,
three directly traced cascades, and full-resolution material/gather/filter
buffers. Independent luminance is disabled. Adaptive scaling and frame splitting
are disabled. Each frozen run warms for 240 frames and measures 120 frames after
discarding the first 16. Two repetitions run in opposite order. The table gives
the mean of the two run medians and p95s.

| Margin in world units | Material canvas | Total cascades | Median ms | p95 ms |
| --- | --- | ---: | ---: | ---: |
| 0 | 1932 x 1092 | 10 | 14.11 | 16.19 |
| 32 | 2060 x 1220 | 11 | 17.05 | 19.62 |
| 64 | 2188 x 1348 | 11 | 19.68 | 22.26 |
| 128 | 2444 x 1604 | 11 | 25.35 | 28.33 |

The 64-unit margin adds 5.57 ms against zero margin in this fixture, or 28.3%
of the default frame time. The canvas grows by 42.2% against the visible
viewport, but that area ratio is not a timing measurement. Even zero margin
reserves 12 pixels for grid alignment. At 32 units the half-resolution transport
width crosses 1,024, adding an eleventh cascade. This helps explain why margin
cost is not a simple pixel-area ratio. Removing the margin also removes
offscreen illumination; the zero-margin case is a cost ablation, not a quality
recommendation.

This is the per-frame cost of shading and transporting light over the extra
area, not repeated canvas creation. Material and radiance resize functions keep
their textures when dimensions are unchanged; allocation happens on resize.

| Disabled feature, all other default settings retained | Median ms | Frame-time reduction ms |
| --- | ---: | ---: |
| Bounce feedback | 19.04 | 0.65 |
| Surface lighting, radius set to zero | 18.46 | 1.22 |
| Bloom, tone mapping retained | 19.28 | 0.40 |

These savings are measured separately and are not additive. They depend on the
scene, material coverage and lighting. All frozen initial/final state reports
match. Cross-setting reports match after excluding the world hash, which includes
the changed HRC settings.

A separate pass-timing run synchronizes after every marked region. Its means
include CPU submission and GPU completion waits, and the synchronizations slow
the frame. These are not hardware GPU timestamps and must not be added to or
substituted for the ordinary frame-time results above.

| Region | Zero margin ms | 64-unit margin ms | 128-unit margin ms |
| --- | ---: | ---: | ---: |
| Material raster | 1.85 | 3.13 | 4.50 |
| Fluid raster | 0.78 | 0.95 | 1.01 |
| Background | 0.04 | 0.05 | 0.06 |
| Emission mipmaps | 0.50 | 0.67 | 0.77 |
| HRC prefilter | 0.34 | 0.37 | 0.44 |
| HRC trace | 1.89 | 2.43 | 2.87 |
| HRC extend | 1.97 | 3.05 | 3.96 |
| HRC merge | 3.28 | 4.69 | 6.13 |
| HRC gather | 1.69 | 2.25 | 2.89 |
| HRC filter | 0.55 | 0.62 | 0.84 |
| HRC bounce | 0.77 | 1.05 | 1.18 |
| Surface light | 1.12 | 1.50 | 1.96 |
| Composite | 2.75 | 2.78 | 2.74 |
| Tone map and bloom | 1.26 | 1.29 | 1.10 |

Material rasterization, merge and extension account for most of the measured
margin increase. Composite remains nearly constant because it renders only the
viewport. Reflection and final presentation are outside these pass labels.

Raw requests, copied projects, binary/shader baselines, responses, captures, and
process-load samples are in `/private/tmp/hrc-features`. No builds or other GPU
benchmarks from this task overlap these runs. Background processes remain active;
the process samples do not establish GPU contention or thermal state.

### Native Metal profiling trial

A separate run with `MTL_CAPTURE_ENABLED=1` successfully wrote the warmed frame
`/private/tmp/hrc-features/baseline.gputrace`. The capture occupies about 599 MiB.
Xcode and Instruments are not installed; `xcrun --find xctrace` cannot resolve the
tool. Detailed replay and counter inspection remain unavailable on this host.

Apple's [Metal Performance HUD](https://developer.apple.com/documentation/xcode/monitoring-your-metal-apps-graphics-performance)
works here with `MTL_HUD_ENABLED=1`, `MTL_HUD_LOG_ENABLED=1`, and
`MTL_HUD_ENCODER_TIMING_ENABLED=1`. Its logged GPU-time entries after HUD frame
256 have a median of 18.05 ms, range 16.30–20.81 ms. The log contains repeated
entries and is not aligned exactly to the preview's measured-frame window, so
this is supporting aggregate evidence, not a per-pass breakdown or another
matched performance result. Encoder counters add instrumentation overhead.
The hidden preview window was unavailable to computer-use inspection, and its
capture excludes the HUD, so encoder labels were not verified visually.

The preview response now marks runs with `MTL_HUD_ENABLED=1` as instrumented.
The older saved baseline executable did not recognize this environment flag;
its HUD responses therefore contain `instrumented:false` despite the active HUD.
Do not compare those frame timings to ordinary runs. The normal benchmarks above
ran without any HUD variables. Current synchronized pass timing remains useful
for locating expensive regions, while hardware per-pass attribution still needs
a trace analysis tool or a timestamp seam in the renderer backend.

### Static material tile cache, rejected

The candidate cached the six base material planes before fluid composition,
restored stable tiles, and rerasterized changed tiles in painter order. Tile
signatures included entity inputs, shader parameters and content revisions;
animated shader graphs retained their time dependency. Camera-domain changes
invalidated the cache. Full-quality frozen captures matched the baseline exactly.

The first implementation increased the material timing region from 3.13 to
8.27 ms. Reusing signatures for unchanged upload data and avoiding unused
parameter hashing removed most of that regression, but did not establish a win:

| Workload | Baseline median ms | Revised cache median ms | Baseline p95 ms | Cache p95 ms |
| --- | ---: | ---: | ---: | ---: |
| Frozen Sandbox | 19.99 | 20.02 | 21.28 | 21.11 |
| Moving camera and firing | 24.09 | 27.13 | 32.59 | 35.10 |

Values average two run medians/p95s. The cache also allocated another 44 bytes
per canvas pixel, about 124 MiB at the default margin. The candidate was removed;
the existing compact material tile lists remain. The experiment indicates that
caching this screen-aligned raster is not worthwhile for this workload. A cache
of static world-space chunks would need a different composition design before
it could avoid invalidation during camera movement. It was not implemented here.
The rejected candidate is preserved in `/private/tmp/hrc-features/rejected-cache.patch`
and the `cachefast` binary/shader snapshots.

### Reduced diffuse reconstruction

`GameHRC2D.reconstruction_scale` independently controls Gather, diffuse history,
filtering, and bounce-image dimensions, from 0.5 through 1.0. It defaults to 1.0.
Material rasterization and final composition keep their full dimensions. Reduced
light is reconstructed using absorption-guided weights to limit interpolation
across opaque boundaries. Changing this scale reallocates the affected images
and resets their history. Surface-light seeds sample the reduced light by UV;
their geometry mask still comes from the full-resolution material image.

This is separate from `scale`, which controls the RGB transport grid, and
`luminance_scale`, which controls independent scalar transport. Both independent
and reused luminance follow `reconstruction_scale` for their output/history.
Adaptive scaling still changes transport resolution only. The default Sandbox
therefore remains RGB transport 0.5, reconstruction 1.0, no separate luminance,
and three directly traced cascades. It is not an entirely half-resolution renderer.

Authored HRC components explicitly retain reconstruction 1.0. Project manifests
advance to version 2 because the new required field changes the asset contract.
The benchmark tool accepts `--reconstruction-scale` and `--lighting-margin`,
records both in its manifest and report, and changes only its copied project.

With the cache removed, two repetitions in opposite order isolate the reconstruction change:

| Workload, RGB transport 0.5 | Reconstruction 1.0 median / p95 ms | Reconstruction 0.5 median / p95 ms | Median reduction |
| --- | ---: | ---: | ---: |
| Frozen Sandbox | 19.87 / 20.65 | 18.02 / 18.81 | 9.3% |
| Moving camera and firing | 24.01 / 32.76 | 22.13 / 30.14 | 7.8% |

The moving cases include simulation. Render/present mean falls from 21.58 to
19.83 ms; simulation mean also varies from 3.81 to 3.62 ms, so the whole-frame
reduction is not entirely a rendering improvement. These measurements do not
establish sustained 60 FPS. Frozen state is unchanged, and reported states match
across reconstruction settings apart from the configuration-dependent world hash.
At reconstruction 1.0 the captures are byte-identical to the pre-experiment
baseline. At 0.5, the frozen Sandbox mean RGB difference is 0.015/255, maximum 8,
with 0.0057% of pixels differing by more than 3. The moving replay's final capture
has mean 0.041, maximum 20, and 0.0524% of pixels over 3. These are final-frame
image statistics, not a flicker measurement.

The separate shadow and pinhole fixtures show larger local differences, up to
104 and 106 channel values respectively, concentrated in under 0.4% of pixels.
Their mean errors remain below 0.16/255. Coarse reconstruction is an explicit
quality tradeoff; this is why the authored default remains 1.0.

Separate synchronized pass measurements locate the saving: Gather falls from
2.23 to 0.68 ms, filtering from 0.64 to 0.35 ms, and bounce from 0.94 to 0.57 ms.
Composite rises from 2.77 to 2.89 ms because reduced light needs guided
reconstruction. As above, these instrumented means are not hardware GPU times
and cannot be summed to predict the ordinary frame result.

Single paired frozen runs also exercise both luminance paths:

| RGB / luminance transport | Reconstruction 1.0 median ms | Reconstruction 0.5 median ms |
| --- | ---: | ---: |
| 0.25 / independent 0.5 | 21.99 | 18.48 |
| 0.25 / independent 1.0 | 40.46 | 38.46 |
| 0.25 / reused RGB | 14.53 | 12.39 |

These are supporting single-pair results, not the repeated main comparison.
Their final images differ by mean 0.014, 0.035 and 0.015/255 respectively, with
maximum channel changes of 8, 8 and 7. State checks match as above. Full-resolution
independent transport remains expensive; shrinking its reconstruction does not
reduce the cost of tracing and merging that transport grid.

### Offscreen source projection prototype

The isolated open/blocked fixtures use a directly authored circular emitter at
x=534, outside the 32-unit canvas and inside the 64-unit canvas. The actual padded
right edges are x=518 and x=550 because the view includes a grid-alignment reserve.
The projected input moves the emitter to x=496, keeping its 15-unit radius outside
the visible camera edge at x=480. Its emission is multiplied by 0.41491, an
illustrative `1 / (1 + (38 / 32)^2)` attenuation for the 38-unit displacement.
These inputs were baked into copied scenes. No runtime projection policy is
installed in Sandbox.

The corrected fixture uses bare material prototypes. The first captures inherited
Crystal's larger destructible-body geometry and are excluded from the experiment;
use files ending in `-baseline-bare`, not `-baseline-a`.

| Open fixture | Median ms, 24 samples | Mean RGB difference from 64-unit reference | Pixels with a channel difference >3/255 |
| --- | ---: | ---: | ---: |
| Full 64-unit margin | 16.73 | 0 | 0% |
| 32-unit margin, original source position | 14.80 | 4.261 | 12.43% |
| 32-unit margin, projected source | 14.55 | 0.622 | 5.30% |

The projected capture retains 96.1% of the reference's summed display RGB values,
versus 4.35% without projection. This is a tone-mapped image statistic, not conserved
physical radiance. Shadow shape still differs, with a maximum channel difference
of 52/255. The short single runs demonstrate an input/quality experiment, not a
repeatable gameplay speedup.

For the blocked case, the x=490..510 wall intersects the source-to-proxy segment,
so the baked candidate suppresses the proxy. It matches the ordinary 32-unit
control byte-for-byte and differs from the full reference by mean 0.023/255.
Conservative suppression loses some light that the full solver carries around
the wall. A runtime implementation would also need continuous boundary transitions,
shader-emitter handling and broader visibility testing. The current result supports
further projection work but does not establish an equivalent replacement for the
full margin. Sandbox keeps its 64-unit margin.

### Validation of the retained follow-up

Release `make test` and native Metal `test_suite --radiance` pass. The new
reconstruction test places a nonabsorbing albedo receiver behind a subpixel
source and compares it with a thin wall inserted between them. Full reconstruction
receives 27.67 display-channel units in the open case and 8.96 behind the wall;
half reconstruction gives 27.50 and 8.89 in both luminance modes. It also checks
resizing. Existing transport, surface, reflection, history and emission-energy
regressions pass. The first version of this test sampled empty space with fog
disabled, so both values were correctly black; it was corrected to use a receiver.

The benchmark CLI's copied project and manifest were checked with reconstruction
0.5 and margin 32. The Metal HUD response reports `instrumented:true`. Changed C
passes clang-format; the Python tool compiles; assets were formatted with
`chibi_ctl --format`; `git diff --check` passes. Raw results and source/binary
fingerprints are indexed by `/private/tmp/hrc-features/manifest.json`.
