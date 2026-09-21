# HRC interval transmission correction

This is the first implementation step from [the GI research](2d-gi-and-volumetrics.md).
It corrects how HRC combines two alternative paths, adds tests that execute the
production GPU shader, and enables matched 1440p/4K measurements. It does not
establish a 60 fps renderer or complete the proposed fog solver.

The work starts at `d3936f25`, with renderer source inherited from `1be98e1e`,
on `cwaldner/gi-research`. The main checkout's uncommitted renderer experiments
are not part of this baseline.

## Transport change

Serial segments combine as `L = Lnear + Tnear * Lfar` and
`tau = tauNear + tauFar`. Equal-probability alternative paths instead combine
as `L = (La + Lb) / 2` and `T = (Ta + Tb) / 2`.
This follows equation 20 in the [HRC paper](https://arxiv.org/abs/2505.02041).

The previous extension shader averaged optical depths for alternatives, giving
the geometric mean of transmission. For optical depths 0 and 10, the production
GPU test returned `0.006737947`; the analytic result is `0.500022700`.
For 0 and saturated half-float depth, the GPU returned zero instead of one half.
Serial-path radiance, uniform media and transparent-path checks already passed.

The correction stores the result in optical-depth form using
`min(a,b) - log(0.5 + 0.5 * exp(-abs(a-b)))`.
Equal depths return directly, including equal infinities. This avoids subtracting
two infinities and retains saturated opaque intervals without a finite clamp.
Empty intervals remain transparent, and serial composition remains additive.

Storage remains RGB16F plus half-float optical depth, eight bytes per interval.
A direct half-float transmission format would round `exp(-0.0001)` to 1.0.
Across 4096 such segments, exact transmission is 0.663916; half-float optical
depth gives 0.663871, while repeated half-float transmission gives 1.0.
This is a numerical storage example, not an end-to-end renderer benchmark.

The material field still uses the existing canvas-pixel optical convention.
This change does not resolve world-unit extinction, subpixel blocker filtering,
directional reconstruction errors, emission normalization or scattering energy.
The prefilter's average density is also a separate spatial approximation.

## GPU regression

`test/radiance_intervals.c` seeds half-float interval buffers, runs the production
`hrc/extend.shd`, decodes its output through GPU readback, and compares radiance
and transmission against a double-precision CPU oracle. It covers serial paths
and their mixture with transparent, uniform, unequal, weak and saturated depths.
The weak-fog check bounds extinction error tightly enough to catch transmission
rounded to one. The two saturated cases exercise half-float infinity explicitly.

Run it with the existing renderer checks:

```sh
build-release/test_suite --radiance
```

## Dense-fog fixture and desktop measurements

`HRCLabDenseFog` was authored and validated through Chibi MCP in this worktree's
`projects/hrc_lab` project. The camera spans 960x540 world units. A 1100x660
smoke volume covers the view and its border, with a source and a narrow opaque
blocker. The frozen measured state contains 5237 active material particles.
This exercises the existing particle-derived density field. It does not add MPM
or a persistent Eulerian fog solver.

Measurements use a Release build on the Apple M4 Pro with 20 GPU cores and the
Metal backend. Dense fog has three interleaved baseline/corrected pairs per
resolution. Pinhole and shadow have one image-comparison pair at 1440p. Each run
has 240 frozen warmup frames and 104 samples after discarding the first 16 of
120 measured frames. Adaptation is disabled, trace levels is 1, reconstruction
mode is 1, and simulation stays frozen. No build or other benchmark is launched alongside these runs.

The 2560x1440 output uses a 1280x720 transport grid. The 3840x2160 output uses a
960x540 grid. These deliberately exercise scalable configurations; comparisons
between resolutions also change transport sampling and are not quality parity.
Both variants use hidden windows. Times include scene submission and GPU
completion, excluding visible presentation. They are not GPU phase timestamps,
and frozen scenes omit advancing simulation costs. A visible/hidden dense-fog
check produced byte-identical PNGs. Early visible and hidden trials are retained
separately and excluded from the matched comparison. Final pairs use the same
binary, including the SDL background-app setting. The runner temporarily loads
the baseline or corrected extension shader and restores the corrected file on
exit. The other shaders and authored project hashes must match.

Another editor process was active during measurement, and timing variance was
substantial across preliminary runs. These numbers are provisional measurements
on a shared desktop. They do not establish a speedup, a regression budget or a
60 fps product target. GPU phase timestamps and an idle device are needed for
the next optimization decision.

The timing columns below are medians of the per-run median and p95 values,
not pooled percentiles. Pinhole and shadow have only one pair. RGB differences
are absolute display-channel differences on a 0 to 255 scale against the old
renderer, not error against physical ground truth.

| Scene/output | Baseline median ms (run range) | Corrected median ms (run range) | Baseline / corrected p95 ms | Mean / max RGB difference |
| --- | ---: | ---: | ---: | ---: |
| 2560-HRCLabDenseFog | 29.82 (29.74 to 30.39) | 30.22 (28.90 to 31.55) | 32.03 / 32.44 | 0.0015 / 1 |
| 2560-HRCLabPinhole | 16.92 (16.92 to 16.92) | 16.47 (16.47 to 16.47) | 17.75 / 17.02 | 0.2026 / 7 |
| 2560-HRCLabShadow | 16.54 (16.54 to 16.54) | 16.50 (16.50 to 16.50) | 17.81 / 17.71 | 0.3066 / 9 |
| 3840-HRCLabDenseFog | 22.40 (22.20 to 22.87) | 22.45 (22.26 to 22.54) | 25.51 / 24.07 | 0.0035 / 1 |

Dense-fog images changed by at most one display level. Pinhole and shadow
changed by up to seven and nine levels, respectively. Under 0.13% of pixels in
each of those fixtures changed by more than three levels. Each repeated
variant produced byte-identical PNGs. The analytic mixed-path test is the
evidence for the transmission correction; these images bound its visible
impact in the selected scenes.

The local [comparison viewer](../../build-release/gi-evidence/comparison.html)
keeps both images at the same size and clips the corrected overlay. Its
[JSON results](../../build-release/gi-evidence/comparison.json) retain all
per-run statistics and fingerprints.

For a matching run, change the dimensions, scale and scene as needed:

```sh
python3 tools/benchmark_hrc_lab.py \
  --project projects/hrc_lab --hrc-prototype HRCLabHRC \
  --scene HRCLabDenseFog --width 2560 --height 1440 --scale .5 \
  --trace-levels 1 --render-frames 120 --repeat 3 \
  --output build-release/gi-evidence/run
```

The preview controller, MCP schema and benchmark CLI now accept dimensions
through 4096 pixels. Hidden/no-graphics Cute applications set SDL's macOS
background-app hint before video initialization, avoiding foreground app and
Dock registration. An explicit environment hint still takes precedence.
Raw requests, copied projects, source/binary fingerprints,
images and timing reports are retained under `build-release/gi-evidence` in this
worktree. Generated captures are local evidence and are not committed assets.

## Validation

The local Release build and full `make test` command passed with sanitizers
disabled. That includes generation, reflection, reload, development watcher,
MCP, behavior backend, the main suite, scene documents, the whole-function audit
and headless app startup. The separate `test_suite --radiance` run passed all
eight new interval cases and the existing transport, bounce, fluid, reflection,
refraction and resizing checks. The baseline shader failed the three unequal
opaque-path cases before the correction.

The first full suite run caught an existing catalog mismatch: five committed
Arena2D HRC scenes were missing from the expected asset list. Updating that list
preserved its strict isolation check and allowed the full suite to pass.

Chibi MCP validation reported no errors or warnings for the new dense-fog scene.
The comparison verified equal frozen runtime states, project hashes and binary
hashes within each pair. Captures exercised actual 2560x1440 and 3840x2160 output;
the native preview also rejected widths 63 and 4097. The comparison viewer's
JavaScript passed its syntax check, and captured pinhole/fog images were inspected.
This is local Metal validation, not cross-GPU performance validation.

Build and full-suite command profile:

```sh
CCACHE_DISABLE=1 CMAKE_BUILD_PARALLEL_LEVEL=8 make test \
  BUILD_DIR=build-release \
  LOCAL_CMAKE_FLAGS='-DCMAKE_BUILD_TYPE=Release -DCHIBI_ENABLE_SANITIZERS=OFF -DCHIBI_ENABLE_WARNINGS=ON -DCHIBI_ENABLE_CCACHE=OFF'
```

Logs are in `build-release/gi-evidence/interval-red.log`, `interval-green.log`,
`make-test-final.log` and `matched.log`.

## Next implementation

The next implementation should establish world-unit optical coefficients and
analytic slab/source references, then measure GPU phases before changing HRC
scheduling or storage. A linear HDR comparison against a matching converged 2D
reference is still needed. Display-image differences alone cannot establish
physical accuracy. Moving lights, blocker motion, history response, a continuous
fog simulation, shallow depth and specular transport remain separate milestones.
