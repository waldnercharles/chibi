# Frame-time roadmap implementation

September 30, 2026. This implements the eight-item Web build checklist in
[the roadmap](frame-time-roadmap.md). The older option catalog remains future
work. All measurements below use this working tree; historical numbers in the
roadmap use earlier code and are not a matched baseline.

## Deterministic replay

Firefight's complete native and WASM controller responses match after 1,800
fixed steps, including ECS, physics bodies, contacts, materials and particle
hash parts. Its world hash is `4cbe66990fbe8d5c`. The independent
`HRCLabPinhole` scene matches after 120 steps with hash `16d38da6700d498b`.
Both requests report no behavior failures.

The investigation found several sources of different float bits:

- Native multiply/add contraction in field evaluation, particle seeding,
  transforms and fluid arithmetic. Those translation units now preserve
  separate operations. Cute bypasses upstream Box2D/Box3D CMake, so its object
  targets now receive upstream's `-ffp-contract=off` policy explicitly.
- Native and WASM `atan2f`, `cosf` and `sinf` in particle headings, visual
  rotations and fluid collisions. These paths now share the existing engine
  trig approximations. Fluid collisions and gas-flow boundaries use the same
  rotation helper.
- `hypotf` in gas confinement. The bounded vector calculation now uses
  `sqrtf(x*x + y*y)` with the same rounding on both targets.
- `expf` in fluid damping. Identical inputs produced adjacent float values at
  step 770. Fluid, gas and debris damping now share the accurate Arm/musl
  single-precision table and polynomial, with its MIT notice retained.

Behavior sine, cosine and vector-from-angle use the same helpers in native
JIT, bytecode and generated C. Tests check golden bits and approximation error.
The trig helpers retain the engine's approximation accuracy; these are not
full-precision replacements for libm. The replay evidence covers the two
scenes above, not every possible authored scene or rendered pixel. Other
simulation paths still use libm, including double particle drag integration.

## Shared runtime changes

Field baking evaluates one instruction across contiguous columns before
moving to the next instruction. Constants and X-only values are reused across
rows. Scalar sampling remains an independent reference. Partial-row bounds,
aliased inputs, primitive shapes and inline task execution have focused checks.

An experiment cached each gas cell's open pressure neighbors before projection.
It increased cell size from 72 to 76 bytes without a repeatable gain. Median
gas-flow time stayed at 0.447 ms per frame natively and moved from 0.609 to
0.628 ms in WASM across three paired runs. The cache was removed.
Profiling now separates Box2D from host synchronization, contacts, fluid
preparation, material phases, impulses and writeback. Normal execution leaves
these clocks disabled.

## Matched measurements

The reference machine is an Apple M4 Pro, 14 CPU cores and 48 GiB RAM, running
macOS 26.7. Native measurements use LLVM 23 Release builds; serial WASM uses
Emscripten 6.0.9 and Node 24.11.1. No task builds ran during measurement.
Other CPU and graphics activity remained on the machine. Compare individual
phases and retain the per-run variation; these runs do not establish a general
whole-frame or GPU speedup.

The baseline retains all new deterministic math and compilation policies. It
uses the previous scalar field evaluator. The candidate adds the row kernel.
Both use the original pressure-neighbor loop. Each side ran three processes in
alternating order; the field benchmark records five samples per process.

| Native field benchmark, median of 15 samples | Scalar | Row kernel |
| --- | ---: | ---: |
| Field IR bake | 80.466 ms | 20.389 ms |
| Bake including connectivity | 89.308 ms | 27.026 ms |
| Independent C reference | 20.470 ms | 20.502 ms |

The IR bake is 3.95 times faster. All 30 field hashes are
`62ac1e0ded142300`; occupancy differences against the C reference are zero.
[Raw field runs](frame-time-fields-results.json) include outputs and binary
hashes.

Firefight profiles run 60 warmup steps and 1,800 measured steps. The
synchronization phase includes terrain preparation and falls on both targets.

| Physics synchronization, median average per step | Scalar | Row kernel |
| --- | ---: | ---: |
| Native | 0.512 ms | 0.325 ms |
| Serial WASM | 0.629 ms | 0.396 ms |

| Whole headless frame, median of three distributions | p50 | p95 | p99 |
| --- | ---: | ---: | ---: |
| Native scalar | 2.759 ms | 4.296 ms | 6.106 ms |
| Native row | 2.705 ms | 4.918 ms | 6.991 ms |
| WASM scalar | 4.922 ms | 6.931 ms | 7.918 ms |
| WASM row | 4.666 ms | 6.735 ms | 8.605 ms |

The whole-frame tail has no repeatable improvement in these runs. Material
work varies substantially even though all twelve final hashes match
`69c6f22b974a4f7a`. The profile request includes warmup, so this hash differs
from the 1,800-step replay above. In the candidate, material-fluid relaxation
averages 0.487 ms natively and 1.623 ms in WASM; Box2D solving averages only
0.014 and 0.029 ms. Prioritize material work for this scene.
[Full profile runs](frame-time-profile-results.json) retain the correlated
worst frames, phase totals, requests and executable hashes. The rejected
[pressure-cache experiment](frame-time-pressure-cache-results.json) has its
own record.

The behavior smoke also measured eligible arithmetic inputs three times under
Node. Median bytecode/Wasm times were 2,409/320 ns for 16 operations,
4,502/343 ns for 32 and 8,005/645 ns for 64. Four operations measured
1,295/1,455 ns, so small regions need further crossover and warmup measurement.
These are input-kernel timings, not complete behavior or browser frame times.
[Raw behavior runs](frame-time-behavior-results.json) retain all samples.

## Browser execution

The optional threaded bundle uses one shared worker budget, four total workers
by default, with three pthreads prestarted before play. Serial web builds remain
the default. [The build and packaging commands](../tracy.md#threaded-web-build)
describe the optional `/threads/` directory.

GitHub Pages supplies no COOP/COEP headers. The threaded bundle registers a
service worker scoped to its directory, adds the headers to same-origin
responses, and reloads before starting the runtime. It requires HTTPS or
localhost and service-worker support. This follows the
[static-host isolation approach](https://github.com/gzuidhof/coi-serviceworker)
and Emscripten's [pthread requirements](https://emscripten.org/docs/porting/pthreads.html).
The hosted site has not been deployed or tested as part of this change.
Each deployment must explicitly include the threaded artifact.

The final packaged app passed a fresh first-load test on plain HTTP localhost
without server isolation headers. The threaded page's service worker controlled
`/threads/` and reloaded the document. It then reported cross-origin isolation,
SharedArrayBuffer and three active pthreads. The serial root had no controller
and stayed non-isolated. Both exposed the runtime and frame-log APIs.

Sequential HeadlessChrome 154 captures at 1280 by 577, DPR 1, each retained
7,200 rows through multiple 30-second Firefight resets. The serial WebGL2
renderer was ANGLE Metal on the Apple M4 Pro.

| Retained browser window | Duration | Interval p50 / p95 / p99 | Max interval | Max steps |
| --- | ---: | ---: | ---: | ---: |
| Serial | 122.695 s | 17 / 21 / 26 ms | 141 ms | 8 |
| Threaded | 120.082 s | 17 / 21 / 23 ms | 82 ms | 5 |

Both retained windows have zero dropped milliseconds and no negative entries.
They are rolling windows with earlier startup samples evicted, on a loaded
machine, so they do not establish a paired browser speedup. The largest serial
hitches coincide with resets near 30 and 120 seconds, with 125 and 121 ms of
physics work respectively. Retain the eight-step cap and target that reset work;
lowering the cap does not remove the expensive preparation that precedes it.
The log measures sample spacing before presentation, not GPU completion.
There were no JavaScript page exceptions. Console messages contain GLES
startup and successful RmlUi font-loading information at error severity.
[Metadata and summaries](frame-time-browser-results.json),
[serial CSV](frame-time-browser-serial.csv) and
[threaded CSV](frame-time-browser-threaded.csv) preserve the evidence.

Eligible behavior numeric regions contain 16 to 256 F32/F64 arithmetic
operations, at most eight scalar inputs, and at least four operations per input.
Main-thread compilation stays asynchronous; bytecode handles pending
compilation, failures, tracing and unsupported regions. Pthread workers
synchronously compile a bounded module copy on first use and retain exports in
a bounded cache. The threaded smoke verifies that eligible samples actually
execute Wasm, measures the first miss separately from warmed worker execution,
compares the bytecode interpreter on the same graph, and reports inner worker
time separately from the condition-variable roundtrip.
Its operation/input matrix covers the 16-operation and four-operations-per-input
gates. These measurements have not yet been run in the current validation slot,
and no Firefight whole-frame gain is established.

## Checks and external blocker

The headless collaboration checkpoint crash came from querying Cute app state
and initializing renderer resources without an app. A small upstream patch
exposes `cf_app_exists`; scene authoring uses it before app-dependent work.

Hosted CI remains blocked. Repository Actions are disabled, and the latest
[failed job](https://github.com/waldnercharles/c-blueprints/actions/runs/36082791733/job/107908155400)
reports failed account payments or a spending limit. Restoring hosted execution
requires an account owner. No billing, Actions or deployment setting was changed.

Final checks passed after removing the pressure-cache experiment:

- Full native `make test`, including ASan/UBSan, generation, reflection, reload,
  development-host, MCP, asset, backend and whole-function audit checks.
- Native application build, serial `make web` and `make web-threads`.
- Serial and threaded `check-material-engine` and behavior/runtime backend
  smoke checks under Node.
- Complete native/WASM replay responses for Firefight and HRCLabPinhole,
  [recorded with binary hashes](frame-time-parity-results.json).
- Formatting and `git diff --check`.

Native checks use LLVM 23 and the installed macOS 26.5 SDK; the newer installed
SDK is incompatible with the current Cute dependencies. The native file-watch
checks ran outside the filesystem sandbox to permit the required macOS APIs.

## Further opportunities

1. Reuse fluid neighbor indices and distances between density and relaxation
   when occupancy permits. Preserve visit order and measure memory growth;
   dense neighborhoods can make an unbounded pair cache worse than recomputing.
2. Binding and worker scratch reuse are implemented in the
   [next field batch](field-optimization-implementation.md). Compact storage for
   constant/Y-only values remains an experiment.
3. Warm required terrain and resources around scene startup and resets.
   Correlate allocation and required-tile work with browser catch-up events.
4. Scoped field SIMD is implemented in the
   [next field batch](field-optimization-implementation.md). Compiler
   vectorization had the same benefit as handwritten intrinsics, which were removed.
5. The worker cache and numeric Wasm path are implemented. Measure the first
   worker module miss, warmed calls, and one-job handoff before considering
   prewarming, batching, or a threshold change. Existing main-thread timings are
   input-kernel measurements, not worker-dispatch evidence.
6. A fresh native 20k contact-heavy measurement is recorded in the
   [Box2D assessment](box2d-performance-and-simple-solver.md). Equivalent web
   scale and GPU-completion measurements remain separate evidence requirements.
