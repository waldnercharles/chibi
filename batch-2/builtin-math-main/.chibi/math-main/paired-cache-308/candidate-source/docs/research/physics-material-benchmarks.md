# Physics and material workloads

Four authored fixtures exercise the current Box2D, destruction and fluid paths.
They use isolated copies of Arena2D, the existing Player/Weapon and fixed input
timelines. Preparing a fixture does not change the shipping project.

| Scenario | Load | Acceptance check |
| --- | --- | --- |
| `terrain_barrage` | Player fires about 240 bullets/s across a 120-degree fan into four destructible rock slabs | Live projectile velocities span the fan; material cells decrease in both measured halves; no-fire control preserves cells; all targets remain |
| `enemy_barrage` | Same wide player fire into four 80 by 80 destructible dynamic enemies with enough health to survive the window | Live projectile velocities span the fan; every target takes damage; cuts and health loss continue in both halves; all targets survive; no-fire control preserves cells and health |
| `enemy_collisions` | 20,000 tightly packed destructible enemies pursue the player, retaining Box2D collision response | Full population, hulls and material occupancy persist; profiled slow frames retain all bodies and at least one contact pair per two enemies |
| `fog_liquid` | Player moves through overlapping gas and liquid volumes and returns inside them | Both volumes remain; separate-volume controls prove each contributes active material; movement produces more transient particles than a stationary control |

Each case measures 240 frames at 60 Hz. Most use 60 warmup frames; the crowd uses
`max(60, ceil(count / 200) + 20)` to finish spawning and settle. At the default
20,000 enemies this is 120 warmup frames and 360 total replay frames. Barrages
begin at frame 60 and stop firing at frame 299. The authored Weapon's existing
loop drains its 1/240-second cooldown, producing roughly four bullets per tick.
A fixed coprime stride visits all 60 fan angles every second, and covers both
edges in the first eight firing ticks. Same-tick bullets share one direction.
Projectile lifetime is one second.
One-shot hit emitters retire after 0.5 seconds, beyond their particles' maximum
0.4-second lifetime. Barrage cameras include the wide fan; the crowd uses a fixed
overview camera. An authored spawner fills a square grid during excluded warmup,
avoiding thousands of scene entries. `--crowd-count 16..20000` adjusts the load;
the default is 20,000. The private Pursuit graph's player-detection radius spans
the full grid, so distant members also drive into contacts. The crowd keeps only
Pursuit on its private enemy prototype, omitting damage and effect event graphs
to hold the collision population stable. Every enemy still has its destructible
material hull and Box2D body; the separate enemy barrage retains the full damage
and effect behaviors. Inspection retains
at most 256 entity-detail rows and
reports truncation explicitly. Whole-world live/material counts, independent
replay failure reporting, and profiler body/contact counts establish full load;
bounded rows verify the named barrage targets and a sample of crowd hulls.

## CPU and replay

Build Release native and the serial WASM controller, then run sequentially with
a fresh output directory:

```sh
make build-release
make web
cmake --build build-web --target chibi_ctl --parallel 8
python3 tools/benchmark_physics_materials.py --wasm-build build-web \
  --repeat 3 --output /tmp/physics-material-cpu
```

Omit `--wasm-build` for native only. `--scenario enemy_barrage` selects one case.
Threaded WASM controllers can also be supplied as `--wasm-build build-web-threads`.

The runner authors and validates assets through Chibi MCP before timing. It saves
warmup, middle and final snapshots, no-fire/stationary controls, full replay hash
parts, requests and raw profiles. Every profile must reproduce its independent
complete fixture replay hash. Repetitions alternate native/WASM order, require stable
project and executable fingerprints, and compare complete replay responses
between platforms. A mismatch fails the run and retains its diagnostic report.

`summary.json` reports medians of the per-run p50/p95/p99/max, physics phase means,
fluid phase means and tracked allocation counts. Fixed-step controller timings
include profiler overhead and exclude rendering, presentation and GPU completion.
Allocation tracking covers `xalloc` requests, not every library allocation. Workload
inspection and MCP startup costs are outside measurement. Contact event graphs do
not appear in the profiler's Tick behavior rows; material/health changes and
no-fire controls establish barrage impacts.

## September 30 CPU baseline

Apple M4 Pro, macOS 26.7, Release native and serial WASM under Node. Values below
are medians of three run percentiles, each measuring 240 fixed steps. These are
local measurements with other user activity permitted, not isolated timing gates
or evidence of a speedup over the earlier, smaller fixtures.

| Scenario | Native p50 / p95 / p99, ms | WASM p50 / p95 / p99, ms |
| --- | ---: | ---: |
| Terrain fan | 0.610 / 1.738 / 3.154 | 1.207 / 1.875 / 2.390 |
| Enemy fan | 0.573 / 1.268 / 2.389 | 1.434 / 3.065 / 4.598 |
| 20,000-enemy collisions | 27.542 / 49.950 / 80.374 | 87.511 / 137.203 / 184.896 |
| Fog/liquid | 1.583 / 3.138 / 4.272 | 3.227 / 5.435 / 7.677 |

The crowd preserves 20,000 material hulls, 8,000,000 cells and 20,001 Box2D bodies.
Slow-frame contact-pair samples range from 37,546 to 50,307 in the first native
and WASM runs. This exceeds a 60 Hz CPU budget on both builds and establishes a
substantial stress case. Native crowd p95 varies from 39.648 to 75.207 ms across
the runs; WASM varies from 126.338 to 173.363 ms. Compare future changes in matched
run groups and retain those ranges; the median alone does not establish stable
frame tails. Complete replay responses and hash parts match between platforms
for all four fixtures, and every repeated profile matches its independent replay.

## Rendered workflow

Use the same four fixtures through the existing native renderer benchmark:

```sh
python3 tools/benchmark_neon_materials.py --scenario physics-all --mode live \
  --resolution .5 --width 1280 --height 720 --render-frames 240 --repeat 3 \
  --output /tmp/physics-material-render
```

The renderer holds the warmup state still for 240 rendering frames, then advances
the fixture's 240 measured simulation frames. It excludes the first 16 advancing
frames from timing, leaving 224 samples. HRC adaptation is disabled; resolution,
gather quality, trace levels and lighting margin are fixed and recorded. Native
GPU-complete frame timing includes submission and GPU waiting. It is not a shader
duration and must not be added to the CPU controller results.

`--mode frozen` renders the same final state without advancing gameplay, providing
a rendering comparison. Every rendered endpoint must match a headless replay.
The bundle retains the prepared project, launch request, binary/shader fingerprints,
image, workload checks and replay script. Physics fixtures require 240 render frames
so their checks cover the same input window. Legacy `--scenario all` still selects
the five original material scenarios.

## September 30 rendered baseline

Native Metal at 1280 by 720, HRC resolution 0.5, Full gather, three trace levels
and 64-pixel lighting margin. Values are medians of three run metrics. Frame
percentiles cover GPU-complete work; simulation and render columns are host phase
averages from those same rendered runs. They have different aggregation rules
and should not be summed to reconstruct a percentile.

| Scenario | Frame median / p95 / p99, ms | Simulation mean, ms | Render/present mean, ms |
| --- | ---: | ---: | ---: |
| Terrain fan | 5.994 / 8.869 / 9.538 | 0.754 | 5.580 |
| Enemy fan | 6.894 / 9.919 / 12.715 | 0.764 | 6.414 |
| 20,000-enemy collisions | 288.136 / 327.480 / 372.466 | 30.002 | 262.934 |
| Fog/liquid | 7.111 / 10.208 / 14.195 | 2.016 | 5.682 |

All twelve endpoints reproduce their headless replay state and pass workload
checks. The crowd's render/present phase dominates its native rendered cost.
These host phase measurements do not identify which GPU pass or submission work
causes that cost. The report's `scene_gpu_ms` is a final-frame HRC query, not an
average over the timing window.

Retained [numeric results and fingerprints](physics-material-results.json)
include every run's percentiles, phase means, workload counts, replay checks and
frame hashes. Native endpoint images show the [terrain fan](physics-material-frames/terrain_barrage.png),
[enemy fan](physics-material-frames/enemy_barrage.png),
[crowd](physics-material-frames/enemy_collisions.png) and
[fog/liquid](physics-material-frames/fog_liquid.png). Full raw profiles and
replayable private project bundles remain at the local paths recorded in the JSON;
those temporary bundles are not committed artifacts.

## Validation

The full native `make test` passes with AddressSanitizer and UndefinedBehaviorSanitizer.
Release native and serial WASM material-engine checks pass. All four complete
native/WASM replay responses match, and three profiled repetitions per platform
match their independent replays. Twelve native rendered runs pass endpoint and
workload checks. Prepared project, binary and shader fingerprints remain intact.

Chrome/WebGL2 smoke checks initialize the serial web runtime and visibly show the
[wide barrage](physics-material-frames/browser-terrain-fan.png) and
[fog/liquid wake](physics-material-frames/browser-fog-liquid.png), with no page
exceptions. Private named scenarios reproduce their equivalent direct input
timelines. This browser check establishes functionality, not browser performance
or threaded-host deployment. The existing collaboration overlay remains visible
in play mode.

The fog fixture's transient particles include player exhaust. Moving-versus-stationary
counts therefore do not independently prove fluid contact emission. Separate
gas-only and liquid-only controls exceed the no-volume material baseline, and
the captured workflow shows displacement around the moving player. The final
combined fixture has 88 transient particles, versus 63 with neither volume and
zero while stationary; these counts are retained without assigning every
particle to a fluid contact.

## Measured next optimizations

These are candidates for matched experiments, not established speedups:

- **Crowd rendering:** attribute mask drawing, uploads, submission and HRC passes.
  The rendered crowd's host render/present mean is about 263 ms, versus 30 ms
  for simulation. The [border rectangle follow-up](material-border-optimization.md)
  now reduces border primitive expansion for arbitrary occupancy masks, with
  matched native rendered improvement. Further candidates include batching moving hulls, retaining unchanged mask data and
  updating transforms. An optical level of detail could reduce rendering work
  while retaining exact gameplay masks, but needs image-quality checks.
- **Crowd behavior and contacts:** mean behavior execution is about 8.1 ms native
  and 37.2 ms WASM. Box2D stepping is 11.5/28.0 ms and gameplay pair collection
  is 5.3/21.2 ms. Profile the Pursuit graph and repeated contact traversal,
  sorting and snapshots before changing the solver. Retain begin/end events,
  swept queries and deterministic ordering in any reduction of duplicate work.
- **Allocation and scratch:** the crowd requests about 54 MB native and 32 MB
  WASM per frame through `xalloc`, at roughly 583 requests. Attribute call sites
  and retain contact, snapshot and scratch capacity where lifetimes permit.
  Requested bytes are not live heap growth, and these counters exclude Box2D's
  internal allocations.
- **Idle fluid preparation:** the crowd's first native run spends about 1.7 ms
  preparing body/obstacle proxies despite having no active fluid particles.
  Test skipping that preparation when no active fluid or volume needs it, while
  preserving emissions, reactions and material fades. Gas-grid work also remains
  in this case; zero fluid particles alone is not a safe guard for all material
  work.
- **Destruction body updates:** mask revisions currently recreate the Box2D body.
  Test updating changed shapes while preserving velocity, mass and contact
  semantics. Profile lifecycle work before considering pooling; the barrage's
  Box2D step itself is only about 0.02–0.07 ms. Disabled-body pooling remains
  discouraged by the [Box2D assessment](box2d-performance-and-simple-solver.md).
- **Liquid kernels:** density and relaxation account for about 84% of the native
  fog/liquid fluid phase and 91% in WASM. Test contiguous neighbor data and
  targeted SIMD with ordered reductions and full replay parity. The earlier
  pressure-neighbor cache had no repeatable benefit, so another cache needs
  evidence. MPM should be compared for quality as well as complete-frame tails.

## Failures exposed by the fixtures

The CPU profiler previously accepted `input_timeline` but ignored it. Its endpoint
hash differed from the independent barrage replay, exposing an idle measurement
instead of the requested load. It now shares the replay sequence stepper, including
inputs during warmup. Regression checks cover sparse movement/firing, named
scenario replay and invalid timelines.

The real enemy damage path also exposed a saved-expression conversion failure in
`health -= amount`. Connecting the real-valued arithmetic result directly to the
integer component field retyped the subtraction but retained its incoming real
conversion. The lowerer now inserts the numeric conversion at the field write. The enemy
fixture keeps its inherited behaviors and tests health loss alongside cuts; it
does not bypass the damage expression.
The formatter updated the inferred Result metadata in `HullDamage` and
`MaterialImpactHealth` to `double`; stored health remains an integer. Existing
projects still load, so this does not require a project-version change.

The rotated terrain barrage exposed two floating-point differences. Platform
`hypotf` rounding changed the crater distance field and detached-fragment launch
velocity. Those float norms now square in double and round once after IEEE
`sqrt`, preserving large and subnormal float inputs. Native multiply/add
contraction also changed the terrain-local damage point by one float ULP.
Destruction now uses the existing separate-operation compilation policy. A
regression sends the recorded hit through the destruction host and checks the
resulting SDF sample. These changes retain Box2D's timestep, substeps, contacts
and swept queries.
Mask membership uses the same policy; a boundary regression checks a cell that
would be excluded by either fused multiply/add ordering.

The wider fan also exposed `hypotf` rounding while normalizing input aim. A
native unit direction had length one while WASM produced one ULP below one,
changing all four bullets fired that tick. Input normalization now uses the same
double-product/square-root norm; an actual player-aim regression pins the result.

Enemy fragments also exposed contraction in the glow fade calculation and
platform `sinf` differences later in the run. The fade uses separate operations
and the engine's existing deterministic sine approximation. This slightly
changes the cosmetic flicker waveform; authored duration and flicker strength
stay the same. A 17-tick regression pins the resulting emission value.

## MPM

The [MPM/Box2D assessment](mpm-box2d-coupling.md) describes a bounded liquid
experiment that exchanges body boundary velocities and point impulses with
Box2D, including angular reaction. The current liquid coupling returns only a
linear impulse at body centers, and the gas pressure solve returns no body force.
MPM needs its own particle/grid transfers, material model and stability substeps;
lower frame times are not established. Compare it at matched visual quality and
measure complete-frame tails before adopting it. Fog should also have a scalar
density-advection comparison using the existing velocity-grid path.
