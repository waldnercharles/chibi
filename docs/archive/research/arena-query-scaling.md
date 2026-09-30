# Arena targeting at scale

This report records the query-reuse baseline. The subsequent
[capacity and memory work](arena-capacity.md) removes the fixed entity ceiling
and measures 10,000 and 20,000 enemies; the limits below describe this earlier run.

Whole compilation alone did not meet the scale goal. With the actual ArenaEnemy
prototype, each Enemy Tick performed two nearest-entity searches. Each search
iterated all entities with Transform and Collider before filtering for players.
A generated-C body using that same helper would inherit the same quadratic cost.

The engine now automatically reuses category-filtered membership across
consecutive synchronous broadcast bodies when compiler analysis proves that
they preserve the query's component dependencies. The compiler records direct
component writes through nested functions and checks declared C-helper effects.
Unknown effects, writes to Transform or Collider, structural operations,
inspection and queued activations end the reuse scope. Broadcast completion also
ends it. The broadcaster retains attachment order.

The query caches candidate handles, not nearest results or positions. Each call
still checks its radius, origin, exclusion, retirement, current coordinates and
stable handle tie-breaking. Eight retained buffers bound the number of cached
category masks. Additional masks use ordinary scans. Allocation capacity is
reused between broadcasts. The service remains native C; its work is reduced
through a compiler proof, without an authored graph change or System rewrite.

## Controlled scale comparison

```sh
python3 tools/bench_arena_scale.py \
  --before build-performance/test_suite-before-query-cache \
  --counts 100 1000 9999 --frames 16 --trials 3 \
  --output docs/research/arena-query-scale-results.json
```

Every enemy has the real prototype's components and all five authored behavior
attachments. Both EnemyBehavior and CombatFeedback receive Tick through the
normal world broadcaster and pump. One target has player category 1. Positions
are fixed to isolate Tick; physics, rendering, collisions, wave spawning and
other Scene behavior are excluded. All final velocities are checked against the
expected pursuit direction and speed, and every attachment must report the
selected backend. All executions must have zero oracle calls.

Each invocation uses a fresh process, eight warmup ticks and 16 measured ticks.
Three paired trials rotate executable and backend order. Release runtime code
uses Clang 23 with sanitizers and profiling disabled. Compilation and world
creation are outside Tick timing. Setup and peak process RSS are reported
separately. No builds or other benchmarks ran concurrently. The saved baseline
contains the preceding whole motion, nearest-query and material improvements.

Median of the per-run percentiles, in milliseconds per Tick, from the
[raw samples and executable/content hashes](arena-query-scale-results.json):

| Backend | Enemies | Before p50 | After p50 | Before p95 | After p95 | p50 speedup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SLJIT | 100 | 0.122 | 0.018 | 0.129 | 0.018 | 6.8x |
| SLJIT | 1,000 | 10.011 | 0.178 | 10.310 | 0.195 | 56.2x |
| SLJIT | 9,999 | 975.476 | 4.363 | 1033.226 | 4.719 | 223.6x |
| Bytecode | 100 | 0.173 | 0.071 | 0.195 | 0.076 | 2.4x |
| Bytecode | 1,000 | 10.533 | 0.619 | 11.514 | 0.744 | 17.0x |
| Bytecode | 9,999 | 985.966 | 8.715 | 1006.272 | 9.206 | 113.1x |

At 1,000 enemies, a separate instrumented run counts 64,000 searches over 32
measured ticks but only 32,032 world candidates and 32 cache builds. The old
algorithm would examine 64,064,000 world candidates for those same searches.
Each search still visits the matching player candidate. This accounts for the
change in scaling. Instrumented timings are not mixed into the table.

The backend gap becomes clearer after reducing shared query work. At 1,000
enemies, SLJIT is about 3.5x faster than bytecode in this workload. Before the
change, the common C search dominated both. This is not a comparison against a
handwritten C ECS loop, and does not establish that the remaining compiler or
scheduler overhead is optimal.

## Rendered Arena regression

Three paired trials run the saved pre-query-cache executable and the current
executable through `--arena-crowd-review`, alternating order. This is the actual
1920x1080 Arena3D editor scene, moving without firing until the authored wave
reaches 40 enemies. Each run contains 1,800 fixed ticks, 180 warmup ticks and
834 measured frames at 40 enemies. Gameplay counts, travel and sampling windows
match across all six runs. Profiling is enabled in both executables. No builds
or other benchmarks ran concurrently.

Median of the per-run mean costs at 40 enemies:

| Metric | Before | After |
| --- | ---: | ---: |
| Behavior CPU | 0.178 ms | 0.129 ms |
| Simulation CPU | 0.321 ms | 0.268 ms |
| Enemy Tick execution | 0.056 ms | 0.009 ms |
| Enemy Tick setup | 0.006 ms | 0.005 ms |

Behavior CPU falls about 28%, and simulation CPU about 17% in this workload.
Host wall-frame p50 is essentially unchanged, 8.295 versus 8.339 ms; presentation
remains dominant. This does not demonstrate a visible frame-rate improvement.
Wall p95 is 9.162 versus 9.114 ms, with overlapping ranges. These three trials
support the CPU improvement, not a claim of better frame pacing.
[All rendered logs, distributions and hashes](arena-query-crowd-results.json)
are retained. This comparison is specifically the query-cache change; both
executables already include whole motion, nearest-query and material lowering.

## Limits that remain

The current ECS permits 10,000 total entities. The test uses 9,999 enemies plus
one target; asking for 10,000 or 20,000 enemies fails with an explicit capacity
diagnostic. Larger-count acceptance is unfinished. Increasing one constant
would not address the storage and allocation costs.

At 9,999 enemies, the current run retains 49,995 behavior attachments and reaches
about 1.9 GB peak process RSS, with about two seconds of setup. Query reuse does
not materially reduce those costs. Native time per entity also grows from about
178 ns at 1,000 enemies to 436 ns at 9,999. Compact/lazy instance storage,
capacity growth and scheduler/data locality remain priorities.

The improvement depends on a selective category. A large matching population
still requires a spatial index or another algorithm. Mutations may split the
safe groups and require additional scans. These costs should become visible in
source-linked performance diagnostics; this change adds opt-in query counters
but does not yet expose author-facing warnings.

Full scenes at 10,000 and 20,000 entities, including physics, spawning, contacts,
rendering and frame latency, remain acceptance requirements. These isolated
Tick timings must not be advertised as playable frame rates. The 40-enemy
rendered scene remains a regression check, not the scale target.

## Verification

Native `make test` and `make build` pass, including the full suite, reload checks, scene documents
and the headless app. Query tests compare the oracle, SLJIT and bytecode around
category mutations between readers and across broadcasts. Both declared host
writes and unknown host effects invalidate reuse. Existing targeting coverage
checks categories, exclusions, retirement, stale entities, finite inputs and ties.
The scaling fixture also passes under ASAN/UBSAN on native and portable execution,
with opt-in counters proving one world scan per broadcast. Sanitized timings are
excluded from performance claims.

The sanitized Arena Scene smoke passes firing and restart on SLJIT and bytecode,
with zero oracle calls. Whole-function coverage remains 21/33 catalog event
entries; this optimization improves existing whole bodies rather than expanding
that count.

The web app builds, and the Emscripten smoke executes bytecode with no SLJIT or
oracle fallback. The native no-JIT smoke rejects execution as UNAVAILABLE. The
web smoke covers compiler/backend policy; real-query cache parity is exercised
by the native-host bytecode tests. The [capacity checks](arena-query-capacity.txt)
explicitly retain the unsupported 10,000/20,000-enemy requests.
