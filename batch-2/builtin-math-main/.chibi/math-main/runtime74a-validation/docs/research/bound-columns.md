# Component columns in prepared Tick updates

September 12, 2026. Eligible Tick bodies now resolve component IDs once per
prepared execution span. SLJIT, bytecode and generated C share this change.
It removes repeated hash probes from the entity loop and gives a modest native
improvement in the larger isolated ArenaEnemy workload. It does not establish
a new full-Scene performance improvement.

## Execution and lifetime

Lowering records each component ID together with its service view. An explicit
component node uses `ecs.world`; an input binding uses the component reader's
execution view. Identical IDs from different views remain separate requirements.

Prepared bindings own a small table of borrowed column descriptors. The runtime
resolves that table before each synchronous span, including after any intervening
ordinary scheduler work. Native component access loads the prepared descriptor;
ordinary activations retain the checked hash lookup. No new C helper is called
per entity to resolve these columns. The one-time binding work uses C.

Generated bodies still check world availability, entity generation, occupancy,
component presence and layout at the original operation. Binding an absent or
incompatible column does not fail an untaken branch. Descriptors contain live
data and occupancy pointers, so storage growth and migration require refreshing
the table before another span. Host operations inside a proven span may not
invalidate storage or reenter graph execution.

Service rebinding notifies the instance's owning world and invalidates the
schedule before the old service can be freed. Rebinding the same pointer also
refreshes a changed execution view. Captured failure activations clear borrowed
column pointers and retain their own inspection storage. Attachment edits,
pooling and hot apply preserve their existing generation and ordering rules.

The first implementation enlarged the arm64 bytecode instruction record from
112 to 120 bytes through padding. Moving the new field into existing padding
restored 112 bytes. The [initial three-trial results](bound-columns-wide-ir-results.json)
showed roughly 3% bytecode regressions at intermediate populations. Those were
not repeated in the final five-trial comparison. These runs do not isolate
padding as the sole cause, but retaining the larger record was unnecessary.

## Actual ArenaEnemy Tick workload

[Five paired trials](bound-columns-scale-results.json) compare the saved executable
before this change with the final Release executable. Both already use prepared
Tick scheduling, native outer iteration, indexed attachments and query reuse.
Clang 23.1.1 builds disable sanitizers and detailed profiling. Each process uses
all five authored ArenaEnemy attachments, two Tick bodies per enemy, eight warmup
frames and 128 measured frames. Velocity checksums match and compiled execution
makes zero oracle calls. Timings include update and scheduler pumping; physics,
rendering, contacts and wave spawning are excluded.

The table reports medians of per-run p50 values. The final column is the median
of paired after-minus-before p50 differences; it need not equal the difference
between the two separate medians.

| Backend | Enemies | Before p50 ms | After p50 ms | After p95 ms | Paired delta ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| SLJIT | 100 | 0.005 | 0.005 | 0.006 | 0.000 |
| SLJIT | 1,000 | 0.058 | 0.058 | 0.066 | -0.001 |
| SLJIT | 10,000 | 1.036 | 0.945 | 1.745 | -0.131 |
| SLJIT | 20,000 | 4.628 | 4.532 | 5.006 | -0.044 |
| BYTECODE | 100 | 0.051 | 0.051 | 0.058 | +0.001 |
| BYTECODE | 1,000 | 0.496 | 0.496 | 0.580 | -0.002 |
| BYTECODE | 10,000 | 6.873 | 6.888 | 7.770 | -0.128 |
| BYTECODE | 20,000 | 14.491 | 14.599 | 15.628 | +0.057 |

Native p50 decreases about 8.8% at 10,000 enemies and 2.1% at 20,000. Results
vary: native 10,000-enemy p50 ranges were 0.877–1.469 ms before and 0.860–1.040 ms
after. At 20,000 they were 4.378–4.906 and 4.385–4.739 ms. Bytecode is effectively
flat in this sample, with a small increase at 20,000. This is a modest improvement,
not a demonstrated general speedup across backends or populations.

Final native 20,000-enemy peak RSS was a median 226.56 MiB and setup 124.09 ms.
The first update plus pump cost 21.23 ms, while steady per-phase medians were
3.75 ms updating and 0.78 ms pumping. Bytecode phases were 13.75 and 0.85 ms.
Phase percentiles need not sum to total percentiles. Absolute timings differ
from the previous scheduler checkpoint even for the same saved executable;
use the paired runs here rather than comparing separate sessions.

## Generated C and Scene checks

[Generated-C comparisons](bound-columns-c-results.json) use the same lowered
counter and component bodies at 64, 512 and 4,096 entities, three trials and 128
frames. All checksums match, compiled modes make zero oracle calls, and prepared
bytecode reuses its warmed scratch. Native-loop SLJIT takes 0.87–1.24 times the
reference C-loop/generated-C time, meeting the advisory 1.5x target on these
fixtures. The reference outer C loop is handwritten; its bodies are generated.
This does not compare actual ArenaEnemy bodies against generated C.

[Three rendered crowd pairs](bound-columns-crowd-results.json) use the authored
40-enemy cap, movement without firing, 1,800 fixed frames and 180 warmup frames.
Both versions explicitly enable detailed behavior profiling. Behavior means
range from 0.104–0.116 ms before and 0.098–0.135 ms after; separate medians are
0.107 and 0.116 ms. Simulation medians are 0.236 and 0.262 ms, with overlapping
ranges. Wall p50 is 8.343 versus 8.392 ms. These measurements show no reliable
improvement in the rendered Scene and include a higher median CPU cost after.

To check normal uninstrumented execution separately,
[five additional Scene-host pairs](bound-columns-scene-results.json) run
`CBLUEPRINT_BENCH_NO_PROFILE=1 MallocNanoZone=0 <executable> --compiled-arena`.
Each run fires for 180 fixed simulation steps, measures the last 150, checks
restart, and asserts both backends, prepared execution and zero oracle calls.
The hidden native app does not present frames. SLJIT behavior p50/p95 remains
0.011/0.027 ms before and after; simulation remains 0.015/0.032 ms. Bytecode
behavior p50 changes from 0.020 to 0.018 ms, with p95 from 0.036 to 0.037 ms.
This shorter firing scenario also does not establish a material full-Scene gain.

## Implications and remaining work

Retain column binding for proven synchronous spans. It eliminates redundant
lookup work without changing authoring or ordering, but the measurements do not
justify replacing the ECS library. They also do not prove that storage is optimal.

The scheduler still walks all attachments to update clocks and find pending
activations. Twenty thousand ArenaEnemy entities have 100,000 attachments, with
separate instance/state allocations. The next comparisons should target those
scans and data locality, using a pending-activation index and contiguous hot
instance state. Component storage, query algorithms, physics and rendering need
their own attribution before choosing a larger architecture change.

Whole-function operation coverage and the explicit Convert to System editor
action remain unfinished. This optimization adds neither new supported graph
operations nor standalone System scheduling. See the
[completion inventory](behavior-completion.md).

## Reproduction and validation

```sh
python3 tools/bench_arena_scale.py \
  --before build-performance/test_suite-before-bound-columns \
  --after build-performance/test_suite \
  --counts 100 1000 10000 20000 --frames 128 --trials 5 \
  --output docs/research/bound-columns-scale-results.json
python3 tools/bench_ticks.py --entities 64 512 4096 --frames 128 --trials 3 \
  --output docs/research/bound-columns-c-results.json
python3 tools/bench_arena_crowd.py \
  --before build-performance/test_suite-before-bound-columns \
  --after build-performance/test_suite --trials 3 \
  --output docs/research/bound-columns-crowd-results.json
```

Run benchmarks sequentially without concurrent builds or tests. Reports retain
binary hashes, trial output and content hashes where applicable.

Final native `make test`, `make build`, sanitizer Arena firing/restart, and the
no-JIT UNAVAILABLE diagnostic pass. `make web` builds the app and passes the
Emscripten BYTECODE smoke with SLJIT absent, including the new bound-column cases.
Formatting and `git diff --check` pass. Bound-column tests cover both backends, untaken
branches, missing/late components, independent reader and world services,
descriptor/hash-table growth, 20,001 entity rows, component removal, layout
migration and service rebinding followed by freeing the old world. Existing Tick
parity tests retain event/state ordering, partial-write failure diagnostics,
waits, cancellation, inspection, pooling and hot apply coverage.

The [final catalog audit](bound-columns-audit.txt) remains at 21/33 whole event
entries, 24/31 effectful functions, 39/39 pure functions and 4/4 library functions.
Its nonzero exit is expected while full whole-function coverage remains open.
