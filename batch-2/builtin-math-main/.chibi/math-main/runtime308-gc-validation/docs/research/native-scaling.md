# Native scaling after whole-function coverage

The September 13 work finishes Retriggerable Delay, then measures the storage,
scheduler and physics work around compiled Arena behaviors. Changes are in the
isolated `cwaldner/compiled-behavior-paths` worktree. Standalone Scene C exports
remain deferred by the user.

## Changes with measured gains

[Stable instance pages](instance-pages.md) reduced isolated native Tick p50 from
4.220 to 1.527 ms at 20,000 enemies in five paired trials. This groups hot
instance fields and variable headers; it does not fuse the compiled bodies.

The world now avoids copying an unchanged clock value across all attachments.
New and pooled instances already inherit the world time and delta at acquisition.
Bitwise comparisons preserve signed zero and NaN payloads. A changed simulation
time still requires one full attachment pass. The fixed delta and the repeated
time assignment from pumping no longer require additional passes.

[Five paired clock trials](clock-elision-scale-results.json) include both initial
clock setters, compiled update and pumping. They differ from the older isolated
Tick fixture, which excludes initial clock propagation.

| Backend | Enemies | Complete p50 before ms | After ms | Complete p95 before ms | After ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| SLJIT | 1,000 | 0.062 | 0.052 | 0.073 | 0.059 |
| SLJIT | 10,000 | 0.637 | 0.534 | 0.882 | 0.683 |
| SLJIT | 20,000 | 1.828 | 1.402 | 2.481 | 1.897 |
| BYTECODE | 20,000 | 9.390 | 8.645 | 9.874 | 9.231 |

Targeted event dispatch now uses the existing per-entity attachment index. Before
this change, each contact event scanned every behavior attachment in the world.
The indexed walk retains the ordered array cursor across edits and rebinds the
index when a handler adds attachments. It also retains the instance pointer
across callback-driven array growth, avoiding a stale attachment pointer.

Physics now collects each Box2D solid contact through one body and compares the
sorted current/previous contact sets with a linear merge. Event order and entity
generation checks remain unchanged. Repeated contacts do not retrigger; contact
re-entry and reused entity slots generate new events.

## Rendered Scene measurements

The new Scene fixture loads Arena3D, runs every authored enemy attachment, moves
bodies through Box2D, delivers contacts and renders at 1280x720. It prepopulates
the wave in a 22-unit grid and updates its counters. Player health is raised to
keep the sample window alive. The player remains stationary and does not fire.
Thirty warmup frames precede 120 measured fixed steps. This is a crowded Scene
workload, not acceptance of a complete large-population combat run.

Larger grids extend beyond the authored arena walls. Geometry is still submitted
for every enemy, while the existing point-light and surface-influence budgets
limit those effects. These measurements do not imply unbounded lighting capacity.

[Five paired contact trials](contact-merge-scene-results.json), profiling off:

| Enemies | Physics p50 before ms | After ms | Frame p50 before ms | After ms |
| ---: | ---: | ---: | ---: | ---: |
| 1,000 | 1.816 | 1.554 | 8.365 | 8.329 |
| 5,000 | 7.994 | 7.239 | 15.090 | 14.482 |

[Five subsequent targeted-event trials](targeted-event-index-scene-results.json),
profiling off and with the contact change already present in both executables:

| Enemies | Behavior p50 before ms | After ms | Frame p50 before ms | After ms | Frame p95 before ms | After ms |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.201 | 0.170 | 8.303 | 8.384 | 9.188 | 9.146 |
| 5,000 | 2.465 | 0.845 | 14.510 | 12.842 | 16.214 | 14.415 |

Every pair matches the final position checksum and asserts live population,
attachment/backend counts, rendered geometry and zero interpreter calls. The
1,000-enemy frame remains presentation-bound. A separate instrumented run is
required to attribute internal work; enabling per-behavior timers changes the
prepared dispatch path and must not be used as its production timing.

The optional physics API reports synchronization, solving, swept-tree updates,
contact collection, component writeback and event queueing separately. It reads
no clocks when disabled. [Instrumented contact trials](contact-merge-profile-results.json)
show contact dispatch falling from 1.207 to 0.456 ms at 5,000 enemies. Afterward,
Box2D solving is about 2.32 ms, swept-tree updates 1.98 ms and contact collection
2.17 ms. Parallelizing only the solver leaves most physics work serial.

The next change [reuses Box2D's existing world index](physics-queries.md) for
ordinary overlaps. The engine only populates and updates its swept-position
index while continuous sensors exist. In [three paired serial trials](world-queries-scene-results.json)
at 5,000 enemies, physics p50 falls from 7.258 to 5.082 ms and frame p50 from
12.444 to 10.194 ms. This change is in the default runtime path. The crowded
fixture does not fire projectiles, so it measures the case where the extra index
can be skipped. Moving-target projectile casts still require swept bounds.

## Independent C comparison

[Five trials](arena-c-reference-results.json) compare the actual Enemy Tick and
inactive CombatFeedback path with an independently written C loop. The reference
prebinds component pointers, builds matching targets once per frame and performs
one nearest-target search per entity. Compiled Enemy Tick currently performs two.
Execution order alternates each frame, and every velocity is compared outside
the timed sections. The measurements predate clock elision.

At 20,000 enemies, native update p50 is 1.395 ms and the C reference is 0.244 ms,
about 5.7 times apart. At 10,000 the corresponding values are 0.484 and 0.100 ms.
This is an optimization target for the restricted idle-feedback workload, not a
general generated-C export or proof that a compiler transformation is safe.
Active feedback, waits, structural edits, physics and rendering are outside this
reference. Very small samples approach the timer resolution.

## Executor and ECS assessment

The ECS already exposes stable execution views and component columns during
structurally stable execution. Flecs is not required to partition owner-local
updates. Parallel graph execution still needs explicit proofs about cross-entity
reads/writes, shared host state, failures and deterministic effect order. Existing
serial batch eligibility alone is insufficient.

The bundled Cute thread pool is not wired into behavior or physics execution.
Its `cute_try_pop_task_internal` decrements the queued count before executing a
task, while `cute_threadpool_kick_and_wait` waits only for that count to reach zero.
It can return with workers still running. Any use here needs explicit completion
tracking or a different executor. The shared framework checkout was not modified.

[EnkiTS](https://github.com/dougbinks/enkiTS) provides a C task interface and waits
for task completion. The experiment uses source commit
`4cba61a045d8e759bd35e5d85691bf9f7271f822`, fetched into a separate temporary
checkout. The runtime physics task interface borrows an executor; the benchmark
adapter initially kept EnkiTS outside production. The subsequent
[shared task pool](shared-task-pool.md) now makes it the default native executor
for sufficiently large physics steps. Executor measurements
now include both Box2D's solver callbacks and read-only contact collection. The
latter uses per-worker scratch and serial sorting/event delivery after joining.

[Three paired solver-only trials](enki-solver-scene-results.json), before world
index reuse, reduce 5,000-enemy physics p50 from 7.177 to 5.939 ms with four
workers. [Parallel contact collection](enki-contacts-scene-results.json) in a
separate paired experiment reduces 7.513 to 4.685 ms. These are separate baseline
windows and should not be added together.

With world-index reuse present in both runs,
[three paired executor trials](enki-world-queries-scene-results.json) show:

| Enemies | Serial physics p50 ms | Four workers ms | Serial frame p50 ms | Four workers ms | Serial frame p95 ms | Four workers ms |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 1.417 | 0.871 | 8.339 | 8.287 | 9.293 | 9.375 |
| 5,000 | 5.089 | 2.207 | 10.172 | 8.487 | 11.552 | 9.556 |

Position checksums match and the driver verifies that the requested executor
was actually active. The 1,000-enemy frame remains presentation-bound with a
slightly worse wall-time tail. These results support further executor integration,
but do not establish a universal worker count or measure projectile-heavy play.
Production pool ownership and automatic selection are now implemented. The
[follow-up measurements](shared-task-pool.md) include small-scene checks and
1,000-enemy firing, damage, XP and restart parity. They also expose a separate
5,000-enemy encounter step-budget failure. Earlier large Scene timings measure
physics and rendering work but do not establish complete gameplay correctness.

## Validation

The native sanitizer `make test` run after world-index reuse and parallel contact
collection passed, including editor/Scene checks and the application smoke.
Focused tests cover clock inheritance on pooled reuse, signed-zero delta changes,
waits, contact-set changes, reused entity generations and attachment growth
during targeted delivery. The four-worker EnkiTS run repeats physics cases above
the parallel collection threshold and checks exact contact delivery order,
moving-target sweeps, dormant swept-index reuse and destruction/removal cases.
Dash and encounter checks also passed on the normal runtime configuration.

`make web` passed. The portable backend smoke now includes the physics regression
cases and executes them using BYTECODE with SLJIT unavailable. The native build
with SLJIT disabled still reports UNAVAILABLE and rejects behavior execution
without interpreter fallback. `git diff --check` passed. The existing small Arena
firing/restart scenario passed on both SLJIT and BYTECODE in three paired runs;
its timing remained effectively unchanged. The large rendered fixture still
does not cover projectile-heavy combat or first-sweep creation cost at scale.

The [bulk destruction follow-up](bulk-destruction.md) removes repeated attachment
scans and index rebuilds during deferred removal and reset. Bulk spawn, component
index maintenance, body fusion, safe parallel behavior batches and sleeping-queue
scheduling remain unfinished. Combat/restart now passes at 1,000 enemies;
5,000-enemy acceptance requires fixing the encounter query's execution-budget
limit and preserving failed-activation diagnostics. The measurements above identify
useful next changes without claiming that SLJIT status alone means all runtime
overhead has disappeared.
