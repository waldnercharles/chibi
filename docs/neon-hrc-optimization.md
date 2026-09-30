# Compact HRC radiance buffers

This log predates the 2026-09-28 field redesign. In its measurements, `scale`
is the RGB grid. `luminance_scale` of 2, 4 or 8 times `scale` is now
`resolution` with `color_resolution` Half, Quarter or Eighth, and
`reconstruction_scale: 0.5` is `gather_resolution: Half`. See
[the HRC lab](hrc-lab.md#resolution-settings).

The first retained HRC optimization packs angular radiance into four bytes per
sample and removes unused terminal buffers. NeonSwarm keeps its authored HRC
scale, ray count, tracing levels, optical depth, temporal feedback and material
settings. This changes GPU storage, not simulation or scene composition.

## Representation and tradeoff

This storage-buffer layout was later replaced by render targets; see
[Render-target transport](#render-target-transport).

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
at `scale: 0.5`, with no luminance transport. It was not a half-resolution renderer.
At 1920x1080 output, the 64-world-unit margin and alignment reserve produce a
2188x1348 material canvas. RGB transport runs at 1094x674; material raster,
near-field gather, radiance filtering and feedback textures retain the padded
canvas dimensions. Composite renders the visible viewport. Surface lighting uses
its own block-resolution seed and blur.

The independent luminance transport and reuse mode measured in this section
were later replaced by `luminance_scale` ratios; see [Split luminance](#split-luminance).
Full-canvas passes do not shrink with the RGB grid.

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

This is separate from `scale`, which controls the RGB transport grid.
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

## GPU timestamps and a half-resolution far field, September 27

### Per-pass GPU time

Pass timing no longer has to wait for the GPU at every mark. On Metal,
`chibi_ctl` installs a command-buffer clock (`src/automation/gpu_clock.m`). Each
mark then submits its work without waiting (`cf_gpu_submit`, added by
`cmake/patches/cute-gpu-submit.patch`), and every committed command buffer
reports its `GPUStartTime`/`GPUEndTime`. A pass is charged only the part of its
interval that follows the end of all earlier GPU work. Overlap therefore is not
counted twice, and idle gaps are not counted at all. Work that no mark covers,
such as UI and presentation, accumulates as `Unmarked`. Other backends keep the
waiting fallback. A preview request selects this with `"pass_timing": true`;
`"pass_timing_detail": true` also times every cascade level. The response's
`pass_timing_source` names the method. The attribution rule has a regression
test in `test/radiance.c`.

The splits add one command-buffer boundary per mark. With detail enabled, that
is roughly a hundred command buffers per frame. The totals stay close to the
frame's GPU time, but compare timed runs only with other timed runs.

### What the timestamps showed

These are Apple M4 Pro, Metal, Release, 1920x1080 measurements of frozen scenes:
240 warmup frames, then 120 measured frames. `HRCLabManyLights` is the new
fixture. It contains 120 small colored emitters, a thin wall, a tilted slab, a
pinhole and smoke.

- Every merge level costs about the same, 0.3–0.43 ms for four quadrants at
  scale 0.5, because each HRC level holds about W*H samples. Top levels are
  cheaper only because most of their rays leave the domain.
- The composite's full-screen `cf_draw_quad_fill` cost 1.85 ms. The same cost
  appeared with a shader that returned a constant. Cute bins shapes into
  screen tiles, and one screen-sized shape is expensive. Drawing a canvas quad
  instead (`src/render/fullscreen.h`) costs 0.55 ms for a byte-identical image.
- The surface-light seed was latency-bound: 46k threads with 64 serial taps.
  Hardware-filtered 2x2 taps halve its cost, 1.0 → 0.5 ms, with identical
  output.
- Full mip chains for emission, absorption and bounce were generated every
  frame, but only one 2x2-averaged level was ever read. Prefiltering now
  averages each work cell's footprint directly. Emission mipmaps remain only
  for reduced reconstruction, which samples them.
- Most of gather's 1.75 ms is its per-pixel near-field taps. The 16 fluence
  loads cost little, and reading hardware-filtered quadrant images saved only
  about 0.1 ms.

### Retained changes

| Change | Image effect | GPU effect |
| --- | --- | --- |
| No per-frame mip chains; prefilter box-averages its footprint | max 1/255 | ~0.5 ms |
| Composite drawn as a canvas quad, not a Cute shape | identical | 1.85 → 0.55 ms |
| Bounce written by a compute pass | identical | ~0.1 ms |
| Surface seed with 2x2 filtered taps, one tap per lane and a workgroup reduction | identical | 1.0 → 0.3 ms |
| Gather reads per-quadrant fluence images (`resolve.shd`) | max 1/255 | ~0.1 ms |
| Gather loads its near-field inputs into a shared-memory tile | identical | 1.75 → 1.22 ms |
| Emission and optical depth in one texture, two trace fetches instead of three | max 1/255 | ~0.2 ms |
| `GameHRC2D.far_level` half-resolution far field (Sandbox: 2) | see below | transport 6.3 → 4.2 ms |

Sandbox frame medians include CPU submission and GPU completion. These are
three alternating uninstrumented runs each:

| Build | Median frame | Image difference from HEAD |
| --- | ---: | --- |
| HEAD (`d837dbe7`) | 17.7 ms | — |
| Exact changes only | 15.1 ms | max 1/255 |
| Exact changes and `far_level: 2` | 13.1 ms | mean 0.14/255, max 15, 0.02% of pixels over 3 |
| Plus gather tiling and the parallel seed | 12.2 ms | as above |

### Half-resolution far field

`far_level` K keeps levels 0..K at the authored scale and solves every higher
level on a hierarchy at half the scale. Distant light needs less spatial detail:
HRC keeps every row at every level, beyond what the penumbra condition requires
far from the probe. `refine.shd` hands off between the grids. Coarse level K
probes lie on the fine level K+1 columns and cover the same interval lengths
with half the directions. Angular fluence is a sector mean, so both fine
sub-directions copy their coarse sector's value, and fine rows interpolate
between coarse rows. The fine hierarchy allocates and extends only levels up to
K+1. The view snaps to two work cells while the far field is active, so the
coarse grid stays world-aligned as the camera moves.

For the many-lights fixture, compared with the full solve:

| `far_level` / traced levels | Transport ms | Mean / max difference | Pixels over 3/255 |
| --- | ---: | --- | ---: |
| 0 / 3 (full) | 6.32 | 0 / 0 | 0% |
| 3 / 3 | 4.80 | 1.44 / 9 | 10.7% |
| 2 / 3 | 4.21 | 1.53 / 10 | 14.2% |
| 1 / 3 | 3.52 | 1.70 / 12 | 21.8% |
| 2 / 2 | 4.08 | 0.93 / 10 | 1.5% |
| 1 / 2 | 3.65 | 1.01 / 11 | 2.5% |

Most of the difference is a slight darkening of distant light, about 2/255 in
green and blue. Averaging optical depth over larger cells over-attenuates
heterogeneous cells: exp(-mean τ) is at most mean exp(-τ). Averaging
transmittance instead would leak through thin walls, so the far grid keeps the
mean of τ. Wall rims and near shadows keep the fine grid. Light through the
pinhole is slightly fainter. Two traced levels brighten the image by a similar
small amount, which is why they partially cancel above. Shifting the camera by
one, two and three world units changed the overlapping image by a mean of 0.2 to
0.5/255, with a maximum of 5, matching the full solve. The radiance suite
checks receiver light, occlusion and exact recovery when `far_level` returns to
zero.

A full scale 0.25 transport costs 1.4 ms, but thin walls get jagged rims and
emitter cores soften. Half-resolution reconstruction saves 1.3 ms in
gather/filter, but produces stair steps along diagonal walls; it stays off.

### Where the Sandbox frame goes now

Timestamped Sandbox, `far_level: 2`: material raster 2.14, fluid 0.40,
prefilter 0.42, far field 1.42, trace 1.25, extend 0.20, merge 1.26, gather
1.22, filter 0.38, bounce 0.29, surface light 0.37, composite 0.60, tone map
0.26, reflections 0.06 and unmarked 0.16 ms. The total is 10.4 ms.

- Material raster is one dispatch, dominated by the cave's Basalt material
  graph over the padded canvas. The 64-unit margin adds about 40% of its cost.
  An optical-only evaluation in the margin would require material graph
  support.
- Removing texture fetches from the trace saves only 0.3 ms of its 1.1 ms.
  The rest is per-ray arithmetic and interval writes, so a fused
  shared-memory trace would not pay for its complexity.
- The surface seed now reads each full-resolution input once, which is its
  bandwidth floor.
- `trace_levels: 2` saves about 0.3 ms at a maximum difference of 4/255.
- A second resolution step for the top levels, quarter scale above a higher
  handoff, follows the same refine rule.

### Split luminance

The first split made `luminance_scale` an independent second HRC: a full
monochrome hierarchy with its own prefilter, gather, filtering and bounce.
On the many-lights fixture, color 0.25 with luminance 0.5 cost 16.6 ms of GPU
time against 9.6 ms for unified RGB 0.5 with the far field. HRC cost scales
with probe count, not color channels, and the split duplicated the
full-resolution passes. A reuse mode derived luminance from the RGB result,
which adds no detail.

The current `luminance_scale` replaces both, following
[jason.today/hrc](https://jason.today/hrc):

- A monochrome transport runs at `luminance_scale`, 2, 4 or 8 times the RGB
  transport's `scale`, so its cells split the RGB cells evenly. Its levels
  store half-float luminance and optical depth, 4 bytes per interval, and
  half-float luminance, 2 bytes per direction.
- Luminance takes its levels above `far_level` from the RGB transport, which
  is already the coarse grid, so the RGB transport solves every level and
  needs no far field of its own. Color levels that came from its own far
  field showed star-shaped artifacts, and dropping it cost no time.
- The luminance transport prefilters the canvas; color box-averages it.
- One gather, filter and bounce serve both. Per quadrant, gather rescales RGB
  fluence to the luminance before reconstructing on the luminance grid.
- The view snaps to one RGB cell.

Two earlier switches failed. `fine_luminance` doubled brightness above
`scale` and required `max_scale` of at most 0.5; with the default of 1,
turning it on failed validation and disabled HRC. `coarse_color` halved color
instead, which fixed the ratio at 2. Any other `luminance_scale` ratio now
fails with a message naming the rule.

### Whole-pixel cells

The padded canvas is now rounded up to a whole multiple of 8 pixels, at most
7 more per axis. Before, whether cells were whole pixels depended on the
output size. RGB at 0.5 had them on even canvases. Luminance at 0.5 over color
at 0.25 had them only when the canvas was a multiple of 4. Fractional cells
cost a lot: RGB at 0.5005 measured 3.14 / 41 against 1.03 / 33 at 0.5 on the
many-lights fixture. Thin lit walls straddle fractional cells, and the bounce
they reflect is the product of two cell averages, so it drops. Adaptive
scales stay fractional.

The prefilter also weights each pixel by the area it shares with a cell.
Before, each 1.998-pixel cell averaged optical depth over 3x3 whole pixels,
thickening every absorber: a thin haze passed 13% less light at scale 0.52
than at 0.5, and 0.7% more after the change.

### Split luminance measurements

GPU time is the sum of per-pass timestamps at 1920x1080, frozen, the mean of
two runs. Error is the mean and maximum 8-bit RGB difference from RGB at scale
1 without the far field, and the share of pixels more than 3 away.

Sandbox, with its current `fog: 0.66` authoring:

| `scale` | `luminance_scale` | `far_level` | GPU ms | Mean / max | Pixels over 3 |
| ---: | ---: | ---: | ---: | --- | ---: |
| 1.0 | 0 | 0 | 39.9 | — | — |
| 1.0 | 0 | 1 | 22.9 | 0.25 / 7 | 0.08% |
| 1.0 | 0 | 2 | 25.1 | 0.23 / 7 | 0.03% |
| 1.0 | 0 | 3 | 27.1 | 0.22 / 9 | 0.02% |
| 1.0 | 0 | 4 | 30.0 | 0.21 / 5 | 0% |
| 0.5 | 1.0 | 0 | 31.4 | 0.04 / 3 | 0% |
| 0.5 | 1.0 | 2 | 21.9 | 0.22 / 7 | 0.05% |
| 0.25 | 1.0 | 0 | 29.8 | 0.12 / 6 | 0.01% |
| 0.25 | 1.0 | 1 | 17.3 | 0.60 / 38 | 3.4% |
| 0.25 | 1.0 | 2 | 19.2 | 0.60 / 22 | 3.5% |
| 0.25 | 1.0 | 3 | 20.8 | 0.55 / 20 | 2.5% |
| 0.25 | 1.0 | 4 | 21.1 | 0.52 / 27 | 1.8% |
| 0.125 | 1.0 | 2 | 16.3 | 1.08 / 59 | 10.5% |
| 0.5 | 0 | 0 | 13.1 | 0.21 / 10 | 0.05% |
| 0.5 | 0 | 2 | 11.1 | 0.57 / 25 | 2.5% |
| 0.25 | 0.5 | 0 | 11.9 | 0.27 / 10 | 0.09% |
| 0.25 | 0.5 | 2 | 9.9 | 0.55 / 25 | 2.5% |

Many-lights fixture (`HRCLabManyLights`):

| `scale` | `luminance_scale` | `far_level` | GPU ms | Mean / max | Pixels over 3 |
| ---: | ---: | ---: | ---: | --- | ---: |
| 1.0 | 0 | 0 | 41.2 | — | — |
| 1.0 | 0 | 1 | 22.3 | 1.58 / 23 | 13.9% |
| 1.0 | 0 | 2 | 24.5 | 1.48 / 17 | 10.0% |
| 1.0 | 0 | 3 | 26.6 | 1.41 / 13 | 8.2% |
| 0.5 | 1.0 | 2 | 19.9 | 1.71 / 18 | 25.8% |
| 0.25 | 1.0 | 0 | 25.5 | 1.34 / 19 | 7.0% |
| 0.25 | 1.0 | 1 | 13.4 | 4.08 / 36 | 67.0% |
| 0.25 | 1.0 | 2 | 15.0 | 3.54 / 29 | 63.2% |
| 0.25 | 1.0 | 3 | 16.0 | 3.33 / 22 | 61.4% |
| 0.5 | 0 | 0 | 11.1 | 1.03 / 33 | 7.3% |
| 0.5 | 0 | 2 | 8.9 | 2.46 / 33 | 39.0% |
| 0.25 | 0.5 | 0 | 9.9 | 1.58 / 32 | 18.8% |
| 0.25 | 0.5 | 2 | 8.0 | 2.66 / 32 | 47.1% |
| 0.25 | 0 | 2 | 6.5 | 3.91 / 52 | 59.3% |

Luminance at twice the color scale matches RGB at the luminance scale with
the same `far_level` and saves 1.2 ms at 0.5 and 3 ms at 1.0. Luminance at
four times the color scale saves more but adds error. Without the far field it
stays accurate, but with it the luminance levels above `far_level` come from a
grid four times coarser with a quarter of the directions, and Sandbox shows
stronger ray streaks and an axis-aligned line. Raising `far_level` does not
remove them: longer intervals make the angular blocks more visible. A higher
`far_level` always costs more, about 2 ms per level at scale 1.0, since it
solves more levels at full resolution. Color ratios blur across boundaries
between differently colored sources.

Blending the refined far field between neighboring coarse directions, rather
than copying each coarse sector, was tried against the streaks of luminance
at four times the color scale. The streaks stayed (a high-pass measure moved
from 0.232 to 0.228) and the mean error rose from 1.72 to 1.78. At equal
distances a grid four times coarser has a quarter of the directions, and a
blend of two wide sectors cannot recover four narrow ones.

### Comparison with jason.today/hrc

Its demo ran in the Claude desktop browser (WebGL on Metal) at a 1112x688
canvas with one probe per pixel, measured as completed-frame throughput. Ours
ran the Sandbox at a 1094x674 output with no lighting margin, which pads to
the same 1112x688 canvas; the HRC columns sum the prefilter, transport,
luminance, gather, filter and bounce passes, fastest of three runs.

| Mode | jason.today ms | Storage buffers | Render targets | Render targets, `far_level: 2` |
| --- | ---: | ---: | ---: | ---: |
| Full color | 5.5 | 7.3 | 5.7 | 3.4 |
| Luminance + color at 0.5 | 4.2 | 6.3 | 6.7 | 3.8 |
| Luminance + color at 0.25 | 3.7 | 5.1 | 7.3 | 2.7 |

With render targets, full color matches the demo, and the far field puts every
mode ahead of it. Luminance without a far field was slower than before: the
old storage-buffer transport paired each direction with its opposite, and the
render-target one did not, until
[pairing returned for luminance](#pairing-opposite-directions-in-luminance).

### Render-target transport

Apple GPUs losslessly compress textures and render targets, but not storage
buffers or textures that compute shaders write. HRC levels are mostly empty
space or smooth light. A Metal benchmark that reads two 8-byte elements and
writes one per element over 2224x1376 elements:

| Data | Storage buffer | Compute-written texture | Render target |
| --- | ---: | ---: | ---: |
| Zeros | 0.39 ms | 0.36 ms | 0.03 ms |
| Constant | 0.37 ms | 0.36 ms | 0.04 ms |
| Smooth half-float gradients | 0.41 ms | 0.37 ms | 0.17 ms |
| Sparse random blocks | 0.37 ms | 0.35 ms | 0.05 ms |
| Random noise | 0.39 ms | 0.37 ms | 0.40 ms |

Replaying intervals and fluence dumped from a Sandbox frame through the same
pattern ran 2.6 to 9.3 times faster as render targets, and our whole merge
chain for one quadrant ran in 0.55 ms instead of 1.07. Trace, extend, merge,
refine and resolve are now fragment passes; see `hrc/hrc_pass.shd`. Each level
is a render target: RGB intervals and fluence in RGBA16F. Luminance pairs
opposite directions, with intervals in RGBA16F and fluence in RG16F. Quadrants
1 and 3 store their levels transposed, so both orientations share each level's
texture.

Frame GPU time at 1920x1080, interleaved runs, fastest of three to five,
storage buffers against render targets:

| Sandbox configuration | Frame | Trace + extend + merge + far field + luminance |
| --- | --- | --- |
| `scale: 0.5`, `far_level: 2` (authored) | 11.9 → 9.7 ms | 4.6 → 2.4 ms |
| `scale: 1.0`, no far field | 50.1 → 27.2 ms | 42.9 → 19.9 ms |
| `scale: 0.5`, `luminance_scale: 1.0`, `far_level: 2` | 25.5 → 18.2 ms | 18.1 → 10.2 ms |

Images match the storage-buffer transport to a mean of 0.03 and a maximum of
3 out of 255; fluence is half-float rather than RGB9_E5, so it is slightly
more precise.

Kept, each exact unless noted:

- Merge weights come from a small texture. Computing the six arctangents per
  fragment made the merge arithmetic-bound: 0.9 ms of 3.3 at 1112x688.
- Each fluence level has its own render target sized to it. Two shared
  targets sized for the largest level cleared and stored 2.7 times the area
  each pass: 0.5 ms slower in total.
- Level-0 intervals are not stored. One crosses only its probe's cell, the
  same for both of its directions, so the merge takes one sample instead.
  This drops the largest interval level. Tracing level 1 in the merge too
  saved memory but cost 0.1 to 0.6 ms more than storing it.
- The far field stores intervals only from `far_level` up; its level 1 was
  never read.
- Fluence rows store even probes before odd ones, and interval rows each
  probe's even directions before its odd ones. Fragments that run together
  then take the same branch: merge 1.39 → 1.13 ms, extend 6.47 → 5.91 ms at
  scale 1.0.

Measured and not kept:

- Fluence in R11G11B10F: 0.07 ms faster, but a mean shift of 0.42/255.
- Pairing each direction with its opposite in RGB, as the demo does. It
  halves fragments and passes, but each level then holds two quadrants, which
  doubled level memory, and RGB was slower at scale 1.0 (28.3 against 27.1 ms).
  Luminance pairs its directions; see
  [Pairing opposite directions in luminance](#pairing-opposite-directions-in-luminance).
- Pairing with both lanes' fluence packed as RGB9_E5 in one RGBA16 unsigned
  texel: memory back down, but decoding made the merge 3 ms slower at scale
  1.0.
- All four quadrants of a level in one atlas and one pass: 13% faster merges
  in the Metal benchmark, but four times the level memory.
- Clearing against loading targets: loading saved time only for the shared
  fluence targets that per-level targets replaced.

Transport memory, estimated for a 1920x1080 frame:

| Configuration | Storage buffers | Render targets |
| --- | ---: | ---: |
| `scale: 0.5`, `far_level: 2` | 95 MB | 134 MB |
| `scale: 1.0`, `far_level: 2` | 385 MB | 547 MB |
| `scale: 0.5`, `luminance_scale: 1.0`, `far_level: 2` | 329 MB | 344 MB |
| `scale: 1.0`, no far field | 535 MB | 920 MB |

Half-float fluence takes twice the RGB9_E5 bytes, and the transposed layout
pads each level to fit both orientations. Cleared targets can also be cycled
by SDL while the GPU still reads them, which these estimates omit.

Not yet tried: moving gather, filter and bounce to fragment passes so their
outputs compress too (together 2.1 ms at the authored Sandbox setting), and
solving the far field every other frame.

Signed-distance marching and cone tracing through mips were not adopted. Neither
integrates heterogeneous media exactly, and both have scene-dependent cost or
leak through thin walls. After the changes above, long-range transport is
1.45 ms of the frame, so the remaining budget sits in full-resolution passes.
Caustics need direction-changing transport that HRC does not provide; budget
them as a separate pass.

### Reconstruction as fragment passes

Prefilter, downsample, gather, filter, bounce and surface lighting now run as
fragment passes like the transport, so their render targets compress too; HRC
dispatches no compute. Images are bit-identical to the compute versions.

- Gather writes its three images as one canvas with three render targets.
- Bounce alpha-blends albedo times gather's far light into its image, with the
  hysteresis as alpha, so it needs no second copy.
- The two surface-light images share a two-target canvas, so the separable
  blur is two passes instead of four.
- Prefilter and downsample write each grid's source and bounce inputs as one
  two-target canvas.

Sandbox at 1920x1080 with `far_level: 0`, fastest of three interleaved runs:

| Pass | Resolution 1 before → after (ms) | Resolution 0.5 before → after (ms) |
| --- | ---: | ---: |
| Prefilter | 0.49 → 0.21 | 0.35 → 0.24 |
| Gather | 0.80 → 0.49 | 1.17 → 1.33 |
| Filter | 0.38 → 0.27 | 0.37 → 0.27 |
| Bounce | 0.30 → 0.14 | 0.30 → 0.13 |
| Surface light | 0.38 → 0.42 | 0.37 → 0.41 |
| Frame | 24.0 → 22.0 | 10.5 → 10.1 |

Gather is slower below resolution 1. The compute version cached each
workgroup's near-field inputs in shared memory; the fragment version reads
emission, optical depth and reflected light for up to eight near samples per
pixel from textures. Without those samples gather takes 0.65 ms at resolution
0.5 and 0.25 alike, so they cost about 0.7 and 1.1 ms. Packing emission and
optical depth into one texture, which a fragment material raster can write as
an extra target, would drop one read per sample.

### Material and fluid rasters as fragment passes

The material raster is a full-screen fragment pass into four RGBA16F render
targets, so the frame dispatches no compute:

| Target | RGB | A |
| --- | --- | --- |
| scene | premultiplied color | coverage |
| source | emission | optical depth, the mean absorption |
| albedo | albedo | material height times coverage |
| normal | tilt, edge light or refraction | gloss or confidence |

Relief coverage is scene's alpha, which the same over-recurrence accumulates.
Emission and albedo alpha never reached a color, and consumers only used the
mean of absorption. Packing optical depth beside emission lets prefilter and
gather's near samples read both in one fetch. Batches after the first
continue from the previous batch's targets on a second canvas. They do not
store absorption coverage, so scene coverage stands in for it where absorbers
from different batches overlap; frames rarely have more than one batch.

Filter and bounce-weighted gather used to sample absorption's mip levels,
which were allocated but never generated, when gather ran below the canvas.
Source's mipmaps are generated in that case, so `gather_resolution: Half` now
reads real data.

Fluid's camera-only visibility is a fifth target on the material canvas; the
raster writes zero there. `cmake/patches/cute-eight-targets.patch` raises
Cute's limit to SDL_GPU's eight color targets. WebGL2 guarantees four, so on
GLES a canvas with more targets than the device's `GL_MAX_DRAW_BUFFERS` fails
to create; desktop browsers report eight. Fluid draws over the tiles its
particles cover: it copies those tiles, then writes the material in place
from the copy, in one pass with one particle loop.

Sandbox at 1920x1080 with `far_level: 0`, fastest of three interleaved runs;
images match the compute rasters to a mean of 0.0001 and a maximum of 1/255:

| Resolution | Material | Fluid | Prefilter | Gather | Composite | Frame |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 2.08 → 2.02 | 0.39 → 0.68 | 0.21 → 0.05 | 0.49 → 0.39 | 0.57 → 0.34 | 21.97 → 21.81 |
| 0.5 | 2.07 → 2.00 | 0.39 → 0.68 | 0.24 → 0.06 | 1.33 → 0.92 | 0.55 → 0.35 | 10.05 → 9.38 |
| 0.25 | 2.07 → 2.01 | 0.39 → 0.68 | 0.46 → 0.20 | 1.77 → 1.15 | 0.55 → 0.34 | 7.78 → 6.80 |

Fluid remains slower than the compute version, which ran one loop per 16x16
workgroup sharing its tile's particle list. With visibility in a separate
pass it took 1.04 ms: running over fluid's tiles alone instead of the whole
canvas saved nothing, so the cost is the loop, not untouched pixels.

`split_frames` is removed: solving the four directions over several frames
left visible artifacts, and it complicated gather and the luminance resolve
with per-quadrant view shifts.

### HRC on WebGL

With every pass a fragment pass, the web build runs the same HRC renderer as
Metal instead of drawing each entity as its own Cute draw. That fallback cost
90 ms or more per Sandbox frame: about 2,700 draws, each with its own uniform
block and storage upload. Porting needed four changes:

- Material uniform blocks are named `uniform_block` and draw-shader blocks
  `shd_uniforms`. Cute's GLES backend matches uniforms to blocks by name, and
  it dropped every uniform in the old `Params` blocks.
- Cute emulates a storage buffer on GLES as texel fetches of one anonymous
  `vec4` or `uvec4` array, so the raster reads entities and tile lists as
  `uvec4` arrays (`entity_at`, `tile_offset`, `tile_entity`), and fluid reads
  particles, offsets and indices the same way.
- WebGL forbids `?:` on structs, so `merge.frag` selects intervals with `if`.
- The host registers the shader directory on every backend, so includes
  resolve on the web too.

`test_suite --radiance-gles` runs the radiance suite on desktop GLES. With
ANGLE's `libEGL.dylib` and `libGLESv2.dylib` on `DYLD_FALLBACK_LIBRARY_PATH`,
its output matches `--radiance` on Metal line for line.
`cmake/patches/cute-gles-float-readback.patch` lets GLES read back
RGBA16F and RGBA32F canvases, which the suite's material checks need.

WebGL has no command buffer timestamps. GPU spans instead time the scene with
`EXT_disjoint_timer_query_webgl2` where the browser offers it, so the scene GPU
chart and adaptive resolution work in Chrome. Without timestamps, adaptive
resolution judges the whole frame's interval instead. That interval includes
the wait for the display, so a frame that fits measures about the budget:
intervals are held to 1.25 times the budget, stepping down only once frames
are missed.

Sandbox in the Studio Scene view at about 1170x740 in Chrome on an M4 Pro,
scene GPU time from timer queries:

| Resolution | Scene GPU |
| ---: | ---: |
| 1 | 17.5 ms |
| 0.5 | 9.5 ms |

Adaptive resolution settled on 0.5 at 60 fps.

### Moving masks and partial coverage

Destructible enemies flickered as they moved. Their lighting depended on
where they sat against the pixels: shifting the six Swarmers in a frozen
Sandbox frame by half a unit changed the frame's received light by 1.7%, and
by 7.7% at resolution 0.5. Three causes stacked:

- A material mask draws its border as boxes of whole cells, 0.1 units of
  antialiasing wide. At half-pixel offsets every shared edge landed on pixel
  centers, and both neighbors counted the pixel whole: the border's emission
  rose 22.6%.
- The fill and border pieces layered over each other as independent
  coverage, so a partly covered silhouette pixel counted about 75% where 50%
  of it was covered.
- The raster and prefilter averaged optical depth over partly covered pixels
  and cells. Transmittance is not linear in optical depth: a half-covered
  opaque pixel came out nearly opaque, and its light 38% too dim.

Mask pieces are now boxes, each cut by at most one line where marching squares
cuts a cell. The border's boxes span row runs of cells. The pieces carry a
`tile_group`, and the raster adds their exact coverage of a pixel-wide square
footprint, so pieces that tile a shape cover each pixel exactly once. The
border's group comes first, and the fill beneath it adds only where it covers
more than the border. The raster stores a partly covered absorber with
transmittance scaled by its coverage, emitting its own radiance where it
covers (see the emission law below). Prefilter
and downsample average light and transmittance over a step of the cell's
length (`hrc/source_average.shd`). A test ring of one-unit boxes now keeps its
emission within 0.3% at every subpixel offset and turn; the old layering
varied by 20%.

Lighting needs that whole-pixel light, but display must show emission
premultiplied by coverage, or antialiased edges glow at full strength and
outlines look a pixel thicker. A sixth, R16F material target, `cover`, holds the
scale from the transport's emission back to display's. The composite applies
it per texel before filtering. Dividing in the
prefilter instead, keeping source premultiplied, failed where fluid overlaps
a solid's edge: fluid adds a uniform medium to the whole pixel, and one
coverage cannot describe both. Marking fluid pixels as fully covered brought
the flicker back under gas (1.1% instead of 0.12%); leaving the solid's
coverage would divide gas's absorption by it too, turning gas transparent at
the solid's edge. Dividing in the raster happens before fluid composes, so
fluid adds to whole-pixel light and only adjusts the display scale.

Received light across half-pixel Swarmer shifts:

| Resolution | Before | After |
| ---: | ---: | ---: |
| 1 | 1.7% | 0.12% |
| 0.5 | 7.7% | 1.9% |

Averaging transmittance treats coverage within a cell as uncorrelated along
a ray. A thin wall that absorbs only partly now passes more light when rays
cross it head-on: in the reconstruction test, a 1.6-pixel wall with an
optical depth of 1 per pixel lets through 47% instead of 32% at resolution
0.5. Walls that are opaque over a whole cell still block.

Grouping costs 0.55 ms of material raster at 3440x1440 and resolution 0.5
(2.28 to 2.85 ms) and 0.19 ms at 1080p (1.04 to 1.23 ms). The cover target
adds 0.08 ms to the raster and 0.18 ms to fluid at 3440x1440. Two approaches
proved slower: clipping each piece against the footprint as a polygon (arrays
the GPU spilled even where they went unused), and keeping the fill and border
as separate layers.

### In-between adaptive resolutions

Adaptive steps between resolution 1 and 0.5 do not bring the image closer to
resolution 1. At 1080p, RMS difference from resolution 1 in 8-bit levels:

| Resolution | Sandbox | Swarmers in view |
| ---: | ---: | ---: |
| 0.875 | 0.59 | 0.80 |
| 0.75 | 0.66 | 0.83 |
| 0.625 | 0.57 | 0.91 |
| 0.5 | 0.30 | 0.53 |

The differences are low-frequency banding in received light. HRC's
grid-aligned artifacts line up between resolution 1 and 0.5, one grid a subset
of the other, but not at fractional cells. Snapping the view by steps of whole
cells and whole pixels, so the crop never resamples the raster, moved the
0.875 difference only from 0.33 to 0.31 mean levels. The ladder therefore keeps
halving the resolution.

### A continuous emission law

The drifting marker strobed as it drifted into gas, and engine exhaust
flashed. A trace step emitted by density, light times length, when its
opacity was under 1/512, and by radiance, opacity times light, above it, a
jump of up to about 500x. Faint gas and exhaust particles (absorption 0.01)
sit at that threshold, so as density shifted, a clear emitter in gas flipped
between full light and almost none. Deep inside the cloud, the marker gave
off almost no light at all. Averaging light over a cell's opacity made the
jump worse, because it divided by opacities just above the threshold.

Sources now hold emission density λ and optical depth τ, and a step of
length L emits λ (1 - exp(-τ L)) / τ (`hrc/source_average.shd`). That is λ L
through clear cells and λ / τ through opaque ones, with nothing to switch
between. Absorbing solids and fluid store their radiance times their optical
depth, so their light is unchanged; clear emitters store their emission.
Display converts back through `cover`.

Marker brightness in a window around it, at resolution 0.5 over 60 frames:

| Case | Before | After |
| --- | --- | --- |
| At the cloud's edge | 39 to 48 | 51 to 53 |
| Beside exhaust | 36 to 45 | 51 to 53 |
| Inside the cloud | 15, no light cast | 35 |

Sandbox away from gas changes by 0.5% in mean brightness, with 0.03% of
pixels more than 3 levels off.

### Pairing opposite directions in luminance

Luminance did nearly as much work per element as RGB, so split lighting was
slower than Full color at resolution 1 with `far_level: 0`: the luminance
transport took 9.1 ms against 10.3 ms for Full color's RGB one, before Half
color's own transport. Merges are about half limited by fetches: skipping
every fetch took a merge from 5.2 to 2.85 ms, while skipping every `exp`
changed nothing.

Packing four rows per texel, with a vertical quadrant folding its row blocks
into columns, sped merges up by about 12% but slowed extend and trace by about
35%, and the transport's total did not change. Four consecutive rows usually
straddle two texels, so each read takes two fetches and a lane selection, and
the trace still marches each row on its own. It was not kept.

Luminance now pairs each direction with its opposite, as
[jason.today/hrc](https://jason.today/hrc) does. Quadrants 0 and 2 solve
together, then 1 and 3. An interval texel holds (light, depth) for both
directions and a fluence texel both lights. Opposite quadrants share every
level address, so each fetch serves two directions without a lane selection,
and one fragment computes both. Only the trace differs per lane: each marches
its own quadrant's rotation. A far field's refine pass writes one quadrant's
lane, keeping the other's with a color write mask. Color solves quadrants in
the order 0, 2, 1, 3, so that luminance can solve each pair after both of its
color quadrants. The output matches the unpaired transport exactly on Metal and
GLES.

At 1080p, resolution 1, `far_level: 0`, medians of three interleaved rounds:

| Sandbox | Scene GPU before | Scene GPU after |
| --- | ---: | ---: |
| Full color | 14.5 ms | 14.3 ms |
| Half color | 16.2 ms | 13.2 ms |
| Quarter color | 14.9 ms | 11.1 ms |

Per-pass times moved between labels. With the new order, color's passes
measured about 2 ms more and luminance's 5 ms less. Running each luminance pair
between its two color quadrants reversed that shift but left the scene's total
unchanged, so the times overlap rather than add.

Whole-scene GPU time, fastest of three to seven runs, and RMS difference from
Full color at resolution 1, in 8-bit levels:

| Resolution, color | Sandbox | Swarmers in view |
| --- | --- | --- |
| 1, Full | 13.8 ms | 14.5 ms |
| 1, Half | 12.7 ms, 0.11 | 12.8 ms, 0.16 |
| 1, Quarter | 10.9 ms, 0.16 | 11.2 ms, 0.25 |
| 0.5, Full | 6.3 ms, 0.30 | 6.4 ms, 0.36 |
| 0.5, Half | 6.2 ms, 0.31 | 6.4 ms, 0.39 |
| 0.5, Quarter | 6.0 ms, 0.35 | 5.9 ms, 0.89 |
| 0.25, Full | 4.7 ms, 0.57 | 4.9 ms, 0.69 |

At resolution 1, Quarter color is about twice as close to Full color as
resolution 0.5 is, and costs 21 to 23% less than Full. At resolution 0.5 the
transport is a small part of the frame, so coarser color saves at most 0.5 ms. Level memory at 1080p
is about 460 MB with Half color and 370 MB with Quarter, against 490 MB for
Full color; unpaired luminance with Half color took about 300 MB.

### Passes outside the transport at resolution 0.5

Sandbox at 1080p, resolution 0.5, Full color, fastest of three runs: the
scene took 6.5 ms. The transport's merge, extend and trace took 2.6 ms, the
material raster 1.4, fluid 0.74, gather 0.71, composite 0.39, the surface
seed 0.37, tone mapping and bloom 0.28, and filter 0.18.

Probes that replaced parts of a shader located the time:

- Material raster: writing its six targets costs 0.26 ms, and walking tile
  lists adds nothing measurable. Without shader graphs it takes 0.42 ms, so
  about 1 ms is the cave's Basalt material: 885 expression nodes with 20
  noise calls. Evaluating each tile group's material once per pixel, or
  skipping the copy of 32 parameters into a local array, changed nothing.
- Fluid: the copy of its tiles and the load and store of the material canvas
  cost 0.27 ms, and the per-particle loops 0.48. About 1,050 tiles hold fluid,
  with 50 particles each on average and 124 at most.
- Gather: the near-field steps between a pixel and its probe take 0.34 ms of
  0.76. Each pixel repeats its neighbors' steps.
- Surface seed: at 8-pixel blocks, each of about 32,000 fragments fetches 16
  taps of three images in turn, which leaves the GPU waiting on fetches.
  With one tap it takes 0.08 ms.

Kept:

- Shader graphs inline a copy of a function per call, so Basalt repeats 532
  of its 885 nodes, including 27 of 37 sines and 3 of 20 noise calls. The
  GLSL emitter writes a node equal to an earlier one, with equal inputs, as
  a copy of it. The material raster went from 1.48 to 1.24 ms in Sandbox and
  from 1.37 to 1.24 ms with swarmers in view (medians of six), and images
  are identical.
- Blocks of 4 pixels or more seed 2-pixel blocks first, one tap each, and a
  second pass averages them (`hrc/surface_reduce.frag`). The seed went from
  0.39 to 0.10 ms. The intermediate is half-float, so images differ by a mean
  of at most 0.0003 levels.
- When the work grid is coarser than the canvas, a pass stores each canvas
  pixel's one-pixel step (`hrc/near.frag`): its light, reflected light
  included, and its optical depth. Gather reads it for the pixel and its
  steps toward each probe, and scales a longer step by the ratio of source
  lengths, which also holds for reflected light. Gather and the new pass
  together went from 0.74 to 0.65 ms at resolution 0.5 and from about 1.0 to
  0.77 at 0.25. Whole-scene minimums fell by 0.06 and 0.14 ms, within the
  runs' noise. Images differ by at most one level, and the canvas-sized
  image adds 16 MB at 1080p.

Together these took Sandbox at resolution 0.5 from 6.80 to 6.26 ms in its
fastest whole-scene run, and from 7.05 to 6.37 in the median of eight.

Tried and not kept: storing a particle's kind in its position so that a pixel
it misses costs one fetch instead of four. Fluid time did not change, and
images differed by a level in a few pixels.

Left open:

- Fluid inside the material raster's last batch would remove the tile copy,
  the extra load and store, and the 87 MB copy canvas at 1080p. That raster
  then binds three more storage buffers. On WebGL each is a texture, and
  the accumulate batches already approach its 16 texture units.
- Basalt is authored content; its cost is a visual choice.

### Far field removed

`far_level` solved the levels above it on a grid twice as coarse, or took them
from color's coarser grid under split lighting, and handed that fluence to the
finer grid. At the same interval length a grid twice as coarse has half the
directions, so the handoff copies each coarse direction into two fine ones,
and the angular blocks show as ray streaks that grow with the level. Blending
neighboring coarse directions did not remove them; see
[Split luminance measurements](#split-luminance-measurements). At resolution
0.5 it put 2.5% of pixels more than 3 levels off, against 0.05% without it.
Paired luminance with coarser color now gives cheaper steps at resolution 1
with a smaller change, so the far field, its refine pass, its hierarchy and
the view's snapping to two cells are removed.

### Adaptive color steps

After halving gather, when that is Auto, adaptive now coarsens color one step
at a time down to Quarter at the authored resolution, before it halves the
resolution. Halved rungs return to the authored color. In Sandbox at 1080p,
the rungs from resolution 1 run 13.8, 12.7 and 10.9 ms with RMS differences
from the first of 0.11 and 0.16, then 6.3 ms and 0.30 at resolution 0.5.


### Lighting margin cost

The margin enlarges every canvas-sized pass, and Sandbox's margin is mostly
cave, whose Basalt material dominates the raster. At 1080p, whole-scene GPU
time:

| `lighting_margin` | Resolution 1, Full | Resolution 1, Quarter | Resolution 0.5, Full |
| ---: | ---: | ---: | ---: |
| 0 | 14.0 ms | 10.8 ms | 6.6 ms |
| 32 | 19.5 ms | 13.8 ms | |
| 64 | 23.4 ms | 16.9 ms | 10.6 ms |
| 128 | | | 13.9 ms |
| 256 | | | 20.9 ms |

At 256 and resolution 0.5, the material raster took 7.4 ms against 1.2 without
a margin, and merges 5.7 against 1.7. Sandbox now keeps the default margin of
0, where adaptive holds resolution 1 at 14.4 ms. With 32, adaptive fell to
resolution 0.5.
