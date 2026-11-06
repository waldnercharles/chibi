# Retained field evaluation and SIMD

September 30, 2026. This continues from `5a74676f`, the previous frame-time
batch merged to remote main. The change targets terrain generation and streamed
16-row slices. These measurements do not establish a whole-frame or GPU gain.

## Implementation

Each material stream owns a prepared field session. It parses its parameter
JSON and binds scalar values once, then retains one instruction-major workspace
per executor worker. Workspaces grow when necessary and serve narrower edge
tiles without another allocation. After warmup, a slice at the same or a smaller
width needs no parameter parsing, binding allocation or row-scratch allocation.
Pending tile samples and terrain masks still have their own allocations.

The compiled program remains immutable. Bakers snapshot parameter values and
seed, borrow the program, and finish each submission before another call.
Independent callers use independent bakers. Sessions check catalog identity and
field revision before using borrowed program memory. The ECS terrain path
already recreates streams when their field or generation settings change;
direct stream calls reject a changed revision before modifying tile state.

A failed slice now keeps its row cursor. Previously it advanced despite failure,
allowing a retry to skip unwritten rows. A regression fixture divides by zero at
row 20, then confirms repeated updates fail at that slice without publishing it.

The first retained-scratch implementation regressed native whole-field time by
about 6%. Explicit `restrict` annotations on the evaluator's disjoint program,
workspace and bound-parameter storage removed that regression. Scalar sampling
remains an independent interpreter for bitwise comparison.

## SIMD selection

Both four-lane intrinsics and compiler vectorization were tested. Native LLVM
already vectorizes row arithmetic. Emscripten needs `-msimd128` to enable WASM
SIMD and its auto-vectorizer. See the
[official SIMD guidance](https://emscripten.org/docs/porting/simd.html).

Handwritten add/subtract/multiply intrinsics added less than 1% to the WASM
results and offered no native advantage. They were removed. The retained change
adds `-msimd128` only to `field_program.c` in web builds. Existing
`-ffp-contract=off` and scalar error handling stay in force. Configure
`-DCHIBI_FIELD_SIMD=OFF` to compare the web field kernel without that flag;
native compiler vectorization remains active.

The [initial experiment](field-optimization-initial-results.json) and
[SIMD comparison](field-optimization-simd-comparison-results.json) retain all
samples, output hashes and executable hashes. The comparison uses three process
orders and five samples per process, with one untimed warmup for sliced work.

| Median of 15 samples, ms | Merged baseline | Reuse, SIMD off | Compiler SIMD | Intrinsics |
| --- | ---: | ---: | ---: | ---: |
| Native whole field | 19.106 | 19.089 | 19.303 | 19.398 |
| Native streamed slices | 57.276 | 55.619 | 55.028 | 55.668 |
| Serial WASM whole field | 37.072 | 37.212 | 23.525 | 23.441 |
| Serial WASM streamed slices | 96.316 | 95.889 | 58.472 | 58.062 |

The comparison supports compiler SIMD without handwritten intrinsics. The
reference is an Apple M4 Pro with 14 CPU cores and 48 GiB,
LLVM 23 Release, Emscripten 6.0.9 and Node 24.11.1. Other user CPU/GPU activity
remained; task builds did not run during measurement. The slice fixture changes
views across 256 pending 128-by-128 tiles, baking seven 16-row slices each and
abandoning each before mask construction. It isolates streamed evaluation,
including pending sample allocation, rather than a complete visible-terrain load.

Whole-field hashes match `62ac1e0ded142300` and the tile reference hash matches
`f7914e01fd99adb1` on every variant and target. Occupancy differences against the
independent C reference are zero. Slice correctness has separate bitwise tests;
the benchmark's printed tile hash is a full-field reference, not a hash of
unpublished pending samples.

Run the same fixtures with `test_suite --bench-fields` and
`test_suite --bench-field-slices` natively, or pass those switches to
`node build-web/material_engine_smoke.js` after building `check-material-engine`.
The baseline was rebuilt from the merged field modules with the same
existing-API slice benchmark appended to its field tests.

## Final matched measurements

The [final-binary field results](field-optimization-results.json) repeat three
alternating process orders with five samples per process. The candidate is the
final native Release test suite and serial-WASM material smoke binary used for
validation, with retained bindings and compiler SIMD enabled.

| Median of 15 samples, ms | Merged baseline | Final candidate |
| --- | ---: | ---: |
| Native whole field | 19.262 | 18.856 |
| Native streamed slices | 58.601 | 56.337 |
| Serial WASM whole field | 37.219 | 23.843 |
| Serial WASM streamed slices | 97.671 | 58.524 |

Native whole-field time is effectively unchanged; slices improve 3.9%. WASM
whole-field time falls 35.9% and sliced time falls 40.1%. All output/reference
hashes match, with zero occupancy errors.

The [matched Firefight profiles](field-optimization-profile-results.json) use
60 warmup frames followed by 1,800 measured frames in three alternating pairs
per target. Terrain generation runs inside the physics synchronization phase.
The table reports median phase averages and median frame percentiles across the
three runs; CPU controller measurements exclude GPU completion and browser
scheduling.

| Measurement, ms | Native before | Native after | WASM before | WASM after |
| --- | ---: | ---: | ---: | ---: |
| Synchronize average per frame | 0.297 | 0.258 | 0.381 | 0.278 |
| Frame p50 | 2.090 | 2.092 | 4.095 | 4.140 |
| Frame p95 | 3.107 | 2.990 | 5.593 | 5.659 |
| Frame p99 | 3.972 | 3.597 | 6.651 | 6.840 |

Synchronization averages fall 13% natively and 27% in serial WASM. Whole-frame
percentiles remain variable, and WASM percentiles are slightly higher in this
run set. This supports the field/phase gain, without establishing a repeatable
whole-frame tail improvement. All 12 profiled runs produce world hash
`69c6f22b974a4f7a`, equal before/after and across native/WASM.

## Physics assessment

The [Box2D and crowd-solver assessment](box2d-performance-and-simple-solver.md)
reviews official v3.1.1 guidance and the supplied Neon-Swarm source. Reusing
disabled Box2D bodies is unlikely to help; stable entities already retain their
bodies, and upstream discourages disable/re-enable as a performance technique.

Three fresh 20,000-enemy Arena runs identify a real crowd bottleneck: Box2D
averages 7.10 ms, pair collection 3.27 ms and contact dispatch 1.50 ms. A separate
uniform-circle solver is a justified experiment for soft crowd separation.
Its broadphase must also serve gameplay contacts and queries to avoid keeping
the current pair work alongside a second simulation. The existing grid solver
needs bounded task storage and a defined reduction order before adopting it for
native and threaded WASM. Firefight continues to have a different bottleneck,
material-fluid relaxation.

## Validation

The complete native `make test` suite passes under ASan/UBSan, including
generation, reflection, reload, development, MCP, assets, backend checks and the
whole-function audit. The final native Release app/controller/test targets
build, and the app starts with the Arena project in a three-frame headless smoke.
`make web`, `make web-threads`, and both builds' `check-material-engine` targets
pass. The threaded material tests use the real four-worker executor.

Field tests cover bitwise
scalar results at widths 1 through 9, high-water workspace reuse, partial rows,
parameter snapshots, signed zero, overflow, real worker-pool baking, hot apply
and failed-slice retries. The [complete replay responses](field-optimization-parity-results.json)
match native against serial WASM for Firefight at 1,800 frames
(`4cbe66990fbe8d5c`) and HRCLabPinhole at 120 (`16d38da6700d498b`).

A [fresh browser smoke](field-browser-smoke-results.json) serves both final apps
over plain HTTP with no isolation headers. Serial Firefight runs for 59 seconds
with 3,227 advancing frames. Threaded Firefight runs for 48 seconds with 2,746
advancing frames, service-worker isolation, `SharedArrayBuffer`, and three
pthreads. Both page-error logs are empty. These browser timings are correctness
smokes, not comparative performance measurements. `make check-format` and
`git diff --check` pass. Source stayed unchanged throughout final validation
and measurement; subsequent edits only record this evidence.
