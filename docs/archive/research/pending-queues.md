# Indexed pending behavior queues

The behavior world now indexes attachments with nonempty activation queues.
Pumping and paused-state inspection enumerate that index instead of inspecting
every attachment. Ordinary prepared Tick bodies leave no activation queue, so
they add no pump visits after Begin cleanup. Instance clock propagation still
visits every attachment; this change does not remove that cost.

The index uses ordered atomic bit words and a population count. Empty/nonempty
queue transitions update membership through an instance notification. A sparse
queue walk scans compact bit words; a completely empty index returns immediately.
Parallel pumps receive only indexed attachments. Serial pumps consult current
membership after each attachment, preserving same-pump delivery to an existing
later attachment and next-pump delivery to an earlier attachment. Attachments
created during a pump join the next pass, matching the previous bounded walk.
Reaping stays in attachment order after execution. Suspended, paused and completed
debug-held activations retain membership.

Detach, reordering, replacement and pooling rebuild or clear membership. Failed
Begin cleanup releases old queue membership before rebuilding indices. Tests
cover a Begin callback that creates another attachment before failing: rollback
must preserve that new attachment's queued work. The tests also exposed missing
host-effect metadata on Print String. It now declares HOST_SINK, preventing
parallel calls into the shared log and arbitrary sink callbacks.

## Measurements

[Five paired trials](pending-scale-results.json) per backend/count use Release
binaries, disabled sanitizers/profiling, 128 measured frames and eight warmup
frames. Before/after execution order alternates. The fixture uses actual
ArenaEnemy components and five authored attachments per enemy, with fixed
positions and no physics or rendering. All measured updates use prepared bodies;
SLJIT uses the native outer loop. The new executable asserts zero attachment
queue visits during measured frames. Both versions assert zero oracle calls and
the script compares state checksums.

| Enemies | Native Tick p50, before → after ms | Native Tick p95 ms | Native pump p50 ms |
| ---: | ---: | ---: | ---: |
| 100 | 0.005 → 0.005 | 0.007 → 0.005 | below timer resolution |
| 1,000 | 0.057 → 0.053 | 0.063 → 0.060 | 0.010 → 0.005 |
| 10,000 | 0.870 → 0.750 | 1.638 → 1.349 | 0.149 → 0.060 |
| 20,000 | 4.642 → 4.274 | 5.228 → 4.649 | 0.808 → 0.398 |

Native median time improves about 7%, 14% and 8% at 1,000, 10,000 and 20,000.
At 20,000, update p50 is 3.843 → 3.874 ms, so the gain comes from the pump.
Peak RSS rises from 198.391 to 203.672 MiB and attachment setup is
117.378 → 113.502 ms. The index, collected attachment indices and per-instance
notification field add storage; allocator size classes also affect RSS.

Bytecode median Tick times are 0.049 → 0.048, 0.486 → 0.477,
6.866 → 6.729 and 14.469 → 14.321 ms at the same counts. At 20,000,
pump p50 falls from 0.851 to 0.456 ms, while update p50 rises from
13.554 to 13.875 ms. Bytecode's larger execution cost limits the total gain.
Its 100-entity p95 rises from 0.054 to 0.058 ms in this sample; improvements are
not uniform across every metric. The JSON retains every trial and phase timing.

[Seven paired native Arena firing/restart runs](pending-scene-results.json)
show unchanged SLJIT behavior p50/p95 of 0.008/0.022 ms and simulation p50/p95
of 0.013/0.027 ms. Bytecode behavior p50/p95 changes from 0.019/0.034 to
0.018/0.032 ms and simulation from 0.024/0.042 to 0.023/0.039 ms. These
runs use the actual hidden Scene host without presentation. They do not establish
playable 20,000-enemy performance.

## Next costs

Clock propagation and scattered instance state remain measurable work. Keep
the public instance clock semantics when reducing those writes. Sleeping queues
also remain indexed and are visited on every pump; a world-level wake-time index
would need to preserve host readiness, cancellation and debugging behavior.
Dense workloads with many active queues need separate measurements before
changing their scheduling policy.

This supports using substantial typed C helpers to finish compiler coverage
while optimizing the overhead around compiled execution. Opaque helpers still
block optimization through their bodies. [The reflected-call measurements](whole-rhythm-reflection.md)
record the call/packing cost and the limits of comparing generated C that uses
the same helper.

## Verification

Native `make test` and `make build` pass with ASAN/UBSAN, including the authored
whole-function audit and gameplay/inspection checks. `--pending` runs the queue
transition, sparse parallel dispatch, waits, debug holds, cancellation, pooling,
enqueue order, attachment growth and failed Begin rollback cases through SLJIT,
bytecode and the oracle. `--tick-batch` checks ordering, component dependencies,
inspection and prepared update behavior.

`make web` builds the app and runs the same pending-queue fixture through
bytecode with SLJIT absent, alongside runtime capability smoke tests. Native
no-SLJIT core and runtime smoke tests report UNAVAILABLE and reject execution
without calling the interpreter. Formatting and `git diff --check` pass.

```sh
build/test_suite --pending
build/test_suite --tick-batch
python3 tools/bench_arena_scale.py \
  --before build-performance/test_suite-before-pending \
  --counts 100 1000 10000 20000 --frames 128 --trials 5 \
  --output /tmp/pending-scale-results.json
python3 tools/bench_arena_scene.py \
  --before build-performance/test_suite-before-pending \
  --after build-performance/test_suite --trials 7 \
  --output /tmp/pending-scene-results.json
```
