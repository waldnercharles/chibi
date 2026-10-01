# Material border rectangles

## Scope and overlap check

September 30, 2026. Compare `codex/performance-defaults` at
`9f4cd89554dbc441c9e4382cf293a990ce29ba17` before changing this checkout.
Its live checkout has no tracked changes beyond that commit. Its implementation
covers behavior compiler/managed storage/query fusion, gas pressure layout/SIMD,
and web execution defaults. It changes no renderer files or physics contact
collection. This batch changes material border geometry in the shared renderer.

Existing integration overlap remains in CMake, controller reporting/input replay,
and tests from earlier batches. In particular, our field-SIMD default must be
reconciled with the other branch's zero-SIMD compatibility bundle during integration.
This renderer change does not change those build or web policies.

## Implementation

The 20,000-enemy fixture's render/present phase averages about 263 ms. Each full
20-by-20 mask currently expands its two-cell border into 36 horizontal row boxes,
plus one fill. Four disjoint border rectangles cover the same cells, reducing
the ordinary full hull to five primitives. Each primitive is a 1,360-byte CPU
record and uploads a 336-byte prefix plus 1,024 parameter bytes.

The renderer builds exact cardinal-distance border cells into vertically merged rectangles
using retained renderer scratch. Occupancy remains the source of truth. The
scratch is rebuilt for every visible mask, so cuts, fragments and authored border
width changes need no new cache invalidation contract. Preserve tile groups,
border-before-fill order, material coefficients, rotation and antialiasing.
Physics, damage and simulation do not change. There is no enemy prototype, shape,
size or intact-rectangle shortcut. Any occupancy mask uses the same extraction.
Curved, jagged and fragmented masks may merge less; their border coverage remains
the same. Analytic shapes without a destructible mask keep their existing renderer.

- `src/render/material.c` and `.h`: narrow border geometry helper and scratch lifetime.
- `src/render/scene_renderer.c`: consume those rectangles instead of row boxes.
- `test/material_sampling.c`: independent occupancy/no-overlap checks and GPU
  comparisons over rotations, subpixel phases and irregular masks.

## Matched native results

Apple M4 Pro, macOS 26.7, LLVM 23 Release, native Metal. Both pinned binaries include
the earlier scenario fixes. The same retained project, input timeline and shaders
run at 1280 by 720, HRC resolution 0.5, Full gather, three trace levels and 64-pixel
margin, with adaptation disabled. Each run holds the warmup state for 240 rendered
frames, advances 240 simulation frames, and excludes the first 16 advancing frames.
No owned builds or browser workloads ran during these measurements. Other user
activity remains a source of variance.

Three crowd pairs alternate baseline/candidate order. Values are medians of each
run's metric; host phase averages cannot be added to reconstruct frame percentiles.

| 20,000-enemy collision workload | Baseline, ms | Merged borders, ms | Reduction |
| --- | ---: | ---: | ---: |
| GPU-complete frame median | 270.258 | 80.165 | 70.3% |
| Frame p95 | 311.639 | 88.758 | 71.5% |
| Frame p99 | 382.795 | 110.582 | 71.1% |
| Host render/present mean | 256.586 | 52.771 | 79.4% |

Every crowd pair improves median and p95. Baseline p95 ranges from 279.090 to
346.288 ms; candidate p95 ranges from 79.193 to 110.768 ms. This is a substantial
rendered-workload improvement, but the 20,000-enemy case still exceeds a 60 Hz
budget. Native simulation means also vary; no physics or simulation speedup is
claimed. These measurements do not isolate GPU shader duration.

Each version's complete initial/final simulation and material inspection state
matches the retained fixture. Crowd captures differ at 41 of 921,600 pixels,
with maximum channel difference 1/255 and mean absolute channel difference
0.0000117 in byte units. The difference is identical across the three pairs.

One control pair each covers sustained terrain and enemy destruction. Their
captures match byte-for-byte. Terrain p95 is 9.378 versus 9.889 ms; enemy p95 is
10.729 versus 9.453 ms. These single pairs establish workflow/image parity, not
a performance gain or regression for the smaller workloads.

The [retained results](material-border-results.json) include all ten runs,
percentiles/ranges, project/binary/source fingerprints, endpoint checks and image
comparison metrics. Raw responses, PNGs, pinned binaries and the comparison
driver remain in `/private/tmp/chibi-border-comparison`. They are temporary local
artifacts; the numeric results are retained in the repository.

## Validation

The tests use an independent cardinal-distance occupancy oracle for exact,
nonoverlapping coverage. Full, cut, holed, disconnected, round, elliptical and
diamond masks cover several border widths. Thin and empty masks and changing
widths exercise scratch reuse; the warmed workload requires zero `xalloc` calls.
GPU checks compare the original row boxes with merged rectangles across rotation
and subpixel phases for scene, source, albedo, normal and emission-cover targets.
The allowed absolute half-float difference is 0.002, two half-precision steps near
unit magnitude. This accepts floating accumulation differences, not changed
occupancy. Build and final workflow results are recorded with the numeric evidence.

Final validation passes the full native `make test` with ASan/UBSan, followed by
the sanitized `--radiance` suite. The expanded seven-mask GPU comparison's maximum
target error is 0.000244, and warmed border extraction makes zero tracked
allocations. `make web` builds successfully. Real Chrome/WebGL2 checks show the
[terrain fan and cuts](material-border-frames/browser-terrain-fan.png) and a
[512-enemy crowd](material-border-frames/browser-crowd.png) with intact emissive
borders, with no page exceptions. Named/direct replay checks pass; owned browser
sessions and servers were closed. The existing collaboration overlay appears in
Play mode. Native endpoint captures are retained for the
[20,000-enemy crowd](material-border-frames/enemy_collisions.png),
[terrain](material-border-frames/terrain_barrage.png) and
[enemy barrage](material-border-frames/enemy_barrage.png).

The C extraction and emission paths are shared by native and WASM. Browser
performance gains are not measured by the native comparisons.
