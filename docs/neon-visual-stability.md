# Sandbox lighting and material response

> Historical implementation record. Commands, MCP recipes, helper scripts and
> validation results below describe the recorded revision. The MCP product and
> its authoring helpers are retired. Current authoring uses saved `.chibi`
> files and [native checks](runtime-authoring.md); see [testing](testing.md) for
> current checks. These results do not verify the current checkout.

This implementation stabilizes the existing 2D renderer and adds bounded contact
effects. It does not introduce MPM, a new BRDF, a coarse outer lighting field,
or full object motion vectors.

## Lighting controls

Sandbox's `HRC` prototype now uses:

| Setting | Value | Effect |
| --- | --- | --- |
| `lighting_margin` | 0 | Leaves offscreen sources and occluders out until the margin is redesigned. |
| lab `trace_levels` | 3 | The default; directly traces cascades 0, 1 and 2 before extending the hierarchy. |
| `radiance_hysteresis` | 1 | Uses current radiance without temporal averaging. |
| `bounce_hysteresis` | 1 | Uses the current bounce result without temporal averaging. |
| `feedback` | true | Keeps indirect bounce enabled. |

The hysteresis settings are current-frame weights: smaller values retain more
history. Radiance history is reprojected for camera translation, rejected when
the view size changes or sampling leaves the old view, and clamped to compatible
current neighborhood values. Bounce transport also reprojects its previous
input. Its fixed-function history blend resets when the camera moves. Moving
objects have no individual motion vectors; neighborhood clamping limits their
trails but does not provide complete disocclusion tracking.

The padded view follows a world-aligned grid at the current RGB work scale.
Camera motion does not resize buffers. A fixed reserve based on the color grid
at adaptive's lowest step also prevents adaptive resolution from changing
full-resolution raster dimensions.
The final crop follows the exact camera rectangle. Emission fades through the
outer half of the margin; occlusion stays intact. The margin shrinks per axis at
the 4096-pixel raster limit, with an unpadded fallback when alignment cannot fit.
Older authored HRC components without the new field retain a zero margin until
it is explicitly set.

Visible geometry stays at output resolution. Built-in solid, unbordered circular
emitters use a separate 1.5-pixel lighting filter with an analytic power
normalization, including radii below one pixel. Graph shapes, strokes, borders,
deformed shapes and metaballs retain their existing coverage.

Adaptive scaling changes `scale`, and a luminance transport keeps its ratio to
it.

## Material response and destruction

Gas and liquid impacts produce transient particles based on relative normal
speed, impulse, participating mass and fluid color/emission. Angular surface
motion contributes to collision and gas-grid boundary velocity. The response
has a 128-contact and 256-secondary-particle limit per tick, in addition to the
existing renderer particle limit. See [gas flow settings](neon-gas-flow.md).

When the rigid-fragment budget is full, surviving detached components remain
visible in the source mask and retry detachment when capacity is available.
Larger surviving components receive slots before cut chips. Cut-away chips can
become dust; they are never restored into a cut. This fixes budget-driven loss
of large pieces, not all cross-chunk connectivity or fracture-shape behavior.

## Performance and captured evidence

The measurements below record the initial stability pass. The later
[optimization follow-up](neon-hrc-optimization.md#sandbox-follow-up-september-26)
uses three traced cascades and fresh controlled baselines; its results should not
be compared directly with these older timings.

Material tiles now contain compact, stable lists of overlapping entities rather
than reserving every entity for every tile. Storage grows with tile count plus
actual overlaps. Empty tiles skip accumulating material batches. The CPU builds
the lists in two passes; the obsolete GPU binning pass is removed.

Measured on Apple M4 Pro, 20 GPU cores, Metal, Release, 1920x1080. The dense
fixture has 1,024 circular lights, fixed HRC scale 0.5, split 1 and feedback off.
Each row uses three sequential runs, 240 warmup frames and 120 measured frames.
Values below are medians of the per-run statistics and include submission and
GPU completion; they are not shader timestamps or gameplay FPS.

| Dense-light workload | Median | p95 |
| --- | --- | --- |
| Original renderer, 1952x1112 raster | 29.77 ms | 37.58 ms |
| New renderer, matched 1952x1112 raster | 29.38 ms | 36.43 ms |
| New renderer, 64-unit margin | 44.02 ms | 46.88 ms |

The matched timing difference is too small to establish a reliable FPS gain.
The wider domain has a substantial cost. At 960x540 output and a matching world
span it adds about 44% raster area. A short frozen Sandbox capture at 1080p,
scale 0.5 and its authored settings measured 51.62 ms median (32 samples).
These results do not establish a 60 FPS budget. The linked follow-up records the
pass breakdown and subsequent optimization measurements.

The moving 4-world-unit light fixture at 640x360 reduced detrended receiver
brightness variation from about 8.5% to 0.06% at scale 1 and 0.23% at scale 0.5.
Mean brightness differed by less than 4%. A separate 0.2-unit source at roughly
one world unit per pixel still has an 11% peak-to-peak phase range; filtering
reduces sampling error but does not eliminate it for every subpixel source.
This comparison isolates source
filtering with the new margin disabled. With a 64-unit margin enabled, the
offscreen fixture retained 97.3% of the wider reference view's illumination,
versus zero before. A 124-frame capture entering from beyond the margin and
crossing the screen edge had no sampled receiver-brightness step above one
8-bit level. These are fixture measurements, not guarantees for all materials
and motion.

Regression coverage includes camera crop/grid stability, adaptive scale and 4K
limits, camera history reprojection,
ghost rejection, subpixel emission power, odd-sized tiles and painter order,
destruction capacity, and bounded gas/liquid contact responses.

Validation completed with the Release build: full `make test` (generation,
reflection, reload/development tooling, MCP, canonical assets, backend checks,
runtime/editor tests and whole-function audit), followed by the complete test
executable and focused material checks after the final sweep bound. The native
Metal `test_suite --radiance` suite also passed, including the tightened emission
energy and resolution bounds. The original temporal shader fails the new
reprojection fixture; the updated shader passes. Changed C/H files pass
clang-format, and `git diff --check` is clean.
