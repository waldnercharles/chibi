# Contiguous behavior instance pages

Scene behavior worlds now allocate instances and variable headers in stable pages,
normally one bank per registered behavior asset. Frequently used Tick fields are
grouped at the beginning of each instance. The prepared loop, compiled bodies,
entity order and graph semantics are unchanged.

An arm64 instance is 416 bytes. The fields needed for Tick guards and context
binding occupy the first 116 bytes. Each page contains an array of instances and
a contiguous array of their variable-header groups. Page capacities grow from
eight rows and account for the asset's variable count, targeting at most 64 KiB
per allocation. A single unusually large instance may exceed that target.
This is still scalar, per-instance execution. Numeric variables are not separate
columns, and compiled bodies are not fused or vectorized by this change.

## Ownership and lifecycle

Instance addresses remain stable while activations, delegates, debugger handles
or prepared rows refer to them. Destroyed slots can be reused within their page;
empty pages are freed. Inactive pooled instances retain their existing storage.
The bank belongs to the world's asset record and outlives every instance allocated
from it, including instances hot-applied to a compatible program generation.

Compatible hot apply reorders variable headers in place. An inactive instance
reused after a variable-count edit receives separately allocated headers of the
new size if its original page layout no longer fits. Managed payloads, strings,
plain-struct backing storage and ownership arenas retain their existing cleanup
rules. Standalone instances can still use individual allocations through the
unchanged creation API.

## Paired measurements

[Five paired trials](instance-pages-scale-results.json) use optimized executables,
disabled sanitizers/profiling, 256 measured frames after eight warmup frames,
actual ArenaEnemy components and all five behavior attachments per enemy. Both
Tick bodies execute through prepared updates. Both backends produce matching
velocity checksums and zero oracle calls; native rows use the SLJIT outer loop.
Physics, rendering, contacts and wave spawning are excluded.

| Backend | Enemies | Before p50 ms | After p50 ms | Before p95 ms | After p95 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| SLJIT | 100 | 0.005 | 0.005 | 0.005 | 0.005 |
| SLJIT | 1,000 | 0.054 | 0.054 | 0.060 | 0.060 |
| SLJIT | 10,000 | 0.707 | 0.528 | 1.491 | 0.680 |
| SLJIT | 20,000 | 4.220 | 1.527 | 4.683 | 2.080 |
| BYTECODE | 100 | 0.042 | 0.039 | 0.049 | 0.048 |
| BYTECODE | 1,000 | 0.418 | 0.419 | 0.508 | 0.468 |
| BYTECODE | 10,000 | 5.533 | 4.162 | 6.513 | 4.537 |
| BYTECODE | 20,000 | 12.624 | 8.920 | 13.413 | 9.445 |

At 10,000 and 20,000, both backends improve in every paired trial. Native median
Tick time falls about 25% and 64%, respectively. At 20,000, native update p50 is
3.834 to 1.246 ms and pump p50 is 0.397 to 0.274 ms. Setup is effectively unchanged
at 113 ms. The cold first update plus pump improves from 19.398 to 15.417 ms but
still exceeds the steady-state cost substantially.

Native peak process RSS at 20,000 is 204.438 to 206.484 MiB. The
[initial fixed-row-limit experiment](instance-pages-initial-scale-results.json)
had a larger increase, 202.125 to 214.344 MiB, with 4.179 to 1.659 ms Tick p50.
Accounting for variable size when choosing capacity reduces unused allocation
space. The final and initial experiments were run separately; use their paired
baselines rather than attributing every difference to page capacity alone.

[Five paired Scene firing/restart checks](instance-pages-scene-results.json) retain
native simulation p50 at 0.014 ms. Native behavior p50 changes from 0.009 to
0.008 ms, while simulation p95 rises from 0.029 to 0.042 ms. Bytecode simulation
p50/p95 changes from 0.023/0.039 to 0.024/0.047 ms. These small hidden-host Scene
checks pass gameplay/backend assertions but establish no whole-Scene speedup.
Their tail increase remains recorded alongside the large-population gains.

The measured executable SHA-256 is
`bc19a64742170c16375a76be1b3c957ab82c9c660e984be88c142086e148d111`.
Raw reports retain before/after hashes, content hashes and individual trials.

## Validation and remaining work

Focused sanitizer tests pass on oracle, SLJIT and bytecode. They retain suspended
activations and independent owned strings across page growth, scattered
cancellation/destruction, hole reuse, compatible variable reordering and an
inactive schema edit. Existing Tick and pending-queue tests pass, including
ordered component dependencies, profiling, inspection and failure snapshots.
The minimal backend test catalog now registers the state-node pack so the
hot-apply fixture can deserialize Retriggerable Delay.

Native `make test` passes with address and undefined-behavior sanitizers,
including the catalog audit, editor/Scene checks and application smoke.
`make web` builds the application and executes the instance-page lifecycle case
through BYTECODE with SLJIT absent. The native no-JIT core/runtime tests still
report UNAVAILABLE without interpreter fallback. Formatting and
`git diff --check` pass. Large playable-scene acceptance,
body fusion, vectorization, independent-C comparisons, parallel execution and
remaining clock/structural maintenance work are not completed by page allocation.

```sh
build/test_suite --instance-pages
build/test_suite --tick-batch
build/test_suite --pending
python3 tools/bench_arena_scale.py \
  --before build-performance/test_suite-before-instance-pages \
  --after build-performance/test_suite --counts 100 1000 10000 20000 \
  --frames 256 --trials 5 --output /tmp/instance-pages-scale.json
python3 tools/bench_arena_scene.py \
  --before build-performance/test_suite-before-instance-pages \
  --after build-performance/test_suite --trials 5 \
  --output /tmp/instance-pages-scene.json
```
