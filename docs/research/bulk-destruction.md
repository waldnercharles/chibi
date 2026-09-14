# Bulk entity destruction

Deferred host destruction and scene reset now remove behavior attachments in one
pass. The change is shared runtime infrastructure; it contains no Arena-specific
rules and changes no graph lowering or backend selection.

Previously, each entity removal scanned the complete attachment array once per
registered asset. Each removed attachment shifted the array, and each affected
asset rebuilt the entity index and pending-work membership. Repeating this for a
crowd made removal quadratic in the population.

The new path selects attachment indices through the existing entity index, checks
entity generations, and deduplicates selection with a temporary bitset. It releases
selected instances and compacts survivors in attachment order, then rebuilds the
index once. This also invalidates prepared Tick bindings through the existing
attachment revision. Individual entity and asset detach operations share this code.

`game_behavior_destroy_entities` is a structural boundary outside graph dispatch
and ECS queries. Selected activations and delegate bindings are released before
entity destruction; survivors retain their instances, variables and order. Stale
handles cannot select a replacement generation. Duplicate destruction requests and
requests already pending in the ECS command buffer are consumed once.

ECS destruction still uses the existing synchronization points in request order.
Combining commands with equal sort keys reordered destruction by entity slot; the
ordering regression test caught this. Batching attachment maintenance captures the
large gain while preserving ECS command ordering and destroy-observer boundaries.
An observer sees selected behaviors already detached and the current entity still
alive, as documented by the bulk API.

## Measurements

The [isolated measurements](bulk-destruction-results.json) time
`game_entities_flush` with five idle behavior attachments per entity. Creation and
final pool teardown are outside the interval. There are no components, physics or
rendering in this fixture, so it isolates attachment maintenance and entity
destruction rather than representing a playable scene.

| Entities | Attachments | Before ms | After ms |
| ---: | ---: | ---: | ---: |
| 1,000 | 5,000 | 58.491 | 0.265 |
| 5,000 | 25,000 | 1,700.898 | 2.269 |
| 20,000 | 100,000 | 68,024.145 | 12.318 |

The [Arena combat measurements](bulk-destruction-combat-results.json) use the
existing 1,000-enemy fixture: 240 measured frames after 30 warmup frames, native
1280x720 rendering, all authored enemy attachments, physics, held fire, damage,
experience and restart. The player has raised health and enemies use the existing
22-unit grid. Both executables use the default four-worker pool. These are
Release/O3 builds with sanitizers and behavior profiling disabled; execution order
alternates across three paired trials. Binary hashes and raw samples are retained.

| Arena, 1,000 initial enemies | Before ms | After ms |
| --- | ---: | ---: |
| Restart median | 89.617 | 2.634 |
| Frame p50, median across trials | 8.276 | 8.311 |
| Frame p95, median across trials | 9.182 | 9.176 |

Restart is about 34 times faster. Steady frame time remains effectively unchanged.
All six runs agree on positions and the per-frame state trace: 991 surviving
enemies, 9 kills, 8 experience and 239 firing frames, with no queued events left
after restart. Both executables report SLJIT and zero interpreter calls. This
scenario exercises immediate projectile hits; it is not a long-lived projectile
swarm benchmark.

## Validation and remaining work

`make build`, `make test`, and `make web` passed. The shared runtime fixture covers
removal across pending-bitset words, suspended waits, queued event delivery,
survivor attachment order, generation reuse, duplicate requests, ECS requests
already pending, pool reuse and Tick schedule rebuilding. It runs on SLJIT,
BYTECODE and the explicit ORACLE; compiled execution asserts zero interpreter
calls. The Emscripten runtime smoke executes the same fixture with BYTECODE and
SLJIT unavailable.

The 1,000-enemy native combat/restart fixture also passed under ASan and UBSan.
Its different build produces a different combat trace; no cross-build numerical
parity or sanitizer performance comparison is claimed.

This does not eliminate every structural cost. Dense component indices still
remove sorted entries individually, and spawning/attaching many entities is a
separate workload. The isolated benchmark has idle activations and no components;
it does not establish the cost of cancelling 100,000 complex active waits.

The 5,000-enemy encounter scan still exceeds its graph execution budget. Full
combat acceptance at that count remains blocked on a reusable compiled query
reduction and persistent failure diagnostics. Neither the execution budget nor
the authored encounter graph was changed by this optimization.

Reproduce with optimized before/after executables:

```sh
python3 tools/bench_behavior_destroy.py \
  --before build-performance/test_suite-before-bulk-destroy \
  --counts 1000 5000 20000 --trials 3 --output /tmp/destroy.json
python3 tools/bench_arena_scene_scale.py \
  --before build-performance/test_suite-before-bulk-destroy \
  --combat --counts 1000 --frames 240 --trials 3 \
  --expect-after-workers 4 --output /tmp/destroy-combat.json
```

The baseline is commit `18682c2` plus the measurement-only
`--bench-behavior-destroy` fixture. Build with `CMAKE_BUILD_TYPE=Release` and
`CHIBI_ENABLE_SANITIZERS=OFF`; both drivers reject sanitizer builds and executable
changes during measurement.
