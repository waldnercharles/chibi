# Shared native task pool

Native physics now uses a shared EnkiTS pool by default. All live Scene and
prototype worlds on the controlling thread retain the same pool. Acquiring it
starts no workers. The first sufficiently large step starts at most four threads,
including the caller, capped by available hardware threads. The last world release
joins and destroys it. Restart retains the pool and reuses its task storage.

Steps with fewer than 256 live collider bodies execute inline. The decision uses
live bodies, not the highest entity slot. Larger steps parallelize Box2D solver
callbacks and contact collection. Each worker owns contact scratch; sorting,
component writeback and event delivery remain ordered on the caller. Task waits
wait for completion before callback arguments or scratch can be reused. More than
64 outstanding tasks use inline completion instead of allocating overflow slots.

The borrowed executor API remains available. WASM builds the same interface with
one worker and inline execution; it neither fetches nor links EnkiTS. Behavior
execution remains SLJIT on native and BYTECODE on WASM. Behavior graph batches
are still serial; this integration does not establish their independence.

EnkiTS is pinned to `4cba61a045d8e759bd35e5d85691bf9f7271f822`. Four workers and
256 bodies are an initial policy supported by these measurements, not a universal
optimum for every machine or mix of static and dynamic bodies.

## Measurements

Release/O3, sanitizers and per-behavior timers off, fixed 1/60 simulation steps,
1280x720 native rendering, 30 warmup frames, alternating execution order and three
paired trials. The player remains stationary with raised health. Enemies use a
22-unit grid that extends beyond the authored walls at larger populations. The
existing light and surface-effect limits remain active.

[Initial comparison against the serial checkpoint](shared-pool-scene-results.json),
120 measured frames without firing:

| Initial enemies | Serial physics p50 ms | Default pool ms | Serial frame p50 ms | Default pool ms |
| --- | ---: | ---: | ---: | ---: |
| 40 | 0.155 | 0.142 | 8.297 | 8.153 |
| 1,000 | 1.231 | 0.887 | 8.455 | 8.332 |
| 5,000 | 5.085 | 2.307 | 10.074 | 8.629 |

The 40-enemy run reports one worker, the larger runs four. Position checksums
match. The 5,000-enemy measurement predates the explicit encounter-completion
check below: its physics timing is useful, but it is not full gameplay acceptance.
Its logged cold physics value is zero because the first presentation frame only
anchors the simulation clock. The current fixture records the first actual step.

[Combat comparison](shared-pool-combat-results.json) uses the same executable
with serial physics forced for the baseline, 1,000 enemies and 240 measured frames.
It spawns enemies through a compiled Spawn Prototype initializer so restart owns
their lifetime. Nearest-target aiming runs outside the frame timer; held firing
uses the normal presentation input path. Authored damage, death, drops and XP
collection execute during measured frames.

| Measure | Serial | Default pool |
| --- | ---: | ---: |
| Physics p50 ms | 1.319 | 0.825 |
| Physics p95 ms | 1.733 | 1.317 |
| Frame p50 ms | 8.337 | 8.301 |
| Frame p95 ms | 9.087 | 9.137 |
| Restart reset median ms | 95.019 | 94.730 |

Physics improves about 37%. Presentation still limits the frame, whose tail does
not improve. All six runs match the final position checksum and the per-frame
trace of encounter state, player health, XP, live projectile count and event
backlog. They finish with 991 enemies, 9 kills, 8 XP and no event backlog. All
authored enemies report SLJIT and interpreter counters remain unchanged.

Projectiles hit and are destroyed within a single simulation step in this tightly
packed workload, so the post-frame live projectile peak is zero. Firing input,
damage, kills and collection are checked separately. This is crowded firing
coverage, not a long-lived projectile swarm. Restart invalidates every initial
enemy handle, clears projectiles/XP/events and resumes the authored 40-enemy wave
on the serial small-workload path. The roughly 95 ms reset warrants separate
profiling of entity removal, instance release and scene reconstruction.

## Newly exposed scaling limit

The strengthened 5,000-enemy combat run fails its encounter-completion assertion:

```text
Scene scale encounter failed: flow step budget exceeded, code=-104 node=2329 pin=-1 steps=10000
```

This is the Branch after Has Health in ArenaWaveBehavior's Count Living Members
function. The default activation budget is 10,000 graph steps. Normal behavior
dispatch reaps failed activations and continues, so the authored encounter count
can remain stale while physics, firing and XP continue. Checking backend status
and position checksums alone did not detect this failure.

The benchmark now explicitly completes RefreshEncounter outside the timed window
and rejects a failed activation before accepting the run. The 5,000-enemy case
must not be reported as passing combat parity until this limit is addressed.
Increasing the global runaway budget alone would hide the graph's scaling cost.
The next compiler work should reduce these entity scans into compiled queries or
reductions and preserve source-linked runtime failures even when inspection is off.
The successful 1,000-enemy results do not establish tens-of-thousands acceptance.

## Validation

The task-pool fixture submits 96 independent jobs into 64 retained slots, joins in
reverse order and checks all 393,504 outputs before freeing callback storage. It
also checks shared references and serial execution before and after worker use.
Physics semantic cases run both below and above the default parallel threshold,
including contact order/re-entry, moving-target sweeps, removal and reset. The
portable backend runs the same cases with one worker.

The full native sanitizer `make test`, `make web`, native JIT-unavailable
`check-behavior-backend`, and both controller/editor `make dev-smoke` reload cases
passed. The 1,000-enemy combat and restart fixture also passed under ASan/UBSan;
its sanitizer timings are not performance results. Web link commands contain
neither SLJIT nor EnkiTS, and its runtime physics cases report one worker. The
development smoke target now requires SLJIT, matching the native execution policy.

Commands for the measured combat case and the explicit remaining limit:

```sh
python3 tools/bench_arena_scene_scale.py --before build-performance/test_suite \
  --serial-before --combat --counts 1000 --frames 240 --trials 3 \
  --expect-after-workers 4 --output /tmp/shared-pool-combat.json
CBLUEPRINT_BENCH_COMBAT=1 build-performance/test_suite \
  --bench-arena-scene-scale 5000 240 0
```
