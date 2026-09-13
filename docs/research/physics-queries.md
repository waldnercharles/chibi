# Box2D contact collection

Arena3D uses Box2D 3.1.1 for planar rigid bodies. The engine also uses Box2D's
distance, shape-cast and spatial-query routines to implement its gameplay
contact contract. There is no separate collision geometry implementation.

## Responsibilities

| Path | Box2D work | Engine work |
| --- | --- | --- |
| Rigid-body step | Integration, solid contacts, constraints and solid-body CCD | Synchronize ECS configuration and velocities; write solved poses back |
| Ordinary gameplay contacts | World AABB query and exact shape distance; touching solid contact data | Include current-pose overlaps and non-dynamic pairs; apply owner exclusions |
| Continuous sensors | Dynamic-tree query, shape distance and relative-motion shape cast | Track both motion endpoints, select the nearest eligible target and stop the sensor at impact |
| Contact delivery | Candidate pairs supplied by the paths above | Canonical entity ordering, deduplication, previous-frame comparison, queue `Contact Begin` for both entities |
| `Get Contact Entities` node | Exact shape distance | Scan current ECS colliders, category/owner filtering, exclude retiring entities and return owned entity handles |

The graph node is a separate, on-demand path. It deliberately observes current
ECS transforms, including spawns and teleports that have not been synchronized
into the physics world. It is a linear collider scan per invocation and should
not become a default per-entity Tick query without a current-state spatial index.

The engine's events drive authored projectile hits, player contact damage and
contact reactions. Sorting and event delivery occur after all query workers
finish. No behavior graph or structural effect executes on a physics worker.

## Why a direct event-buffer substitution changes behavior

Box2D offers `b2World_GetContactEvents`, `b2World_GetSensorEvents` and current
per-shape contact/overlap accessors. The current engine reads solid contacts with
`b2Shape_GetContactData`; it does not consume the two world event buffers.
Shape creation still enables those unused event buffers. Removing that overhead
or adopting them is a separate follow-up to the index change below.

Several parts of the engine contract extend beyond those buffered events:

- Continuous sensors must detect crossings within a step, including a moving
  target, select the first hit and clamp the projectile position. Box2D sensors
  have no continuous collision detection. Its manual recommends casts for this.
- Zero-delta physics calls synchronize teleports and detect overlap immediately.
  Box2D returns from a zero-delta world step without rebuilding contact events.
- Ordinary solid contact manifolds are computed before integration. The engine
  also tests overlaps at the resulting poses. Replacing this with buffered
  solid events alone could shift a gameplay event to a later step.
- Gameplay overlap events include static/static and kinematic/static or
  kinematic/kinematic pairs, even though Box2D does not solve those solid pairs.
- Entity generations, pending destruction, owner exclusions and deterministic
  delivery belong to the engine's contact contract.

These are reasons to preserve particular semantics, not reasons to retain every
extra query. The [Box2D simulation manual](https://box2d.org/documentation/md_simulation.html)
documents sensors, body-type filtering, contact timing and event accessors.
The pinned source's `src/world.c`, `src/sensor.c` and `src/contact.c` were also
inspected for this audit.

## Reuse the world index

Previously, every collider had a proxy in a second Box2D dynamic tree owned by
the engine. Every moving body updated its swept bounds there, even with no
continuous sensors. Ordinary overlap checks queried that duplicate tree.

Ordinary checks now call `b2World_OverlapAABB` against Box2D's existing world
index. The callback retains the same exact shape-distance test and filtering.
The swept index is populated and updated only while continuous sensors exist.
It is still needed for relative-motion casts: a target can cross the projectile
path even when its final pose lies outside the projectile's swept bounds.
When the last continuous sensor disappears, its target proxies may remain
allocated but are not updated or queried. They are refreshed before a later
continuous sensor queries them. Destroyed bodies remove their proxies.

The EnkiTS experiment can partition these read-only world queries after the
solver finishes. Each worker owns its pair and contact scratch arrays and writes
only the stop fraction of its assigned bodies. Main-thread merging establishes
the same event order afterward. The subsequent [shared task pool](shared-task-pool.md)
makes this the default native path for steps with at least 256 live bodies.

Validation includes thin walls, sliding, sustained contacts, moving targets,
rotated box casts, nearest-hit and owner filtering, initial overlap, zero-delta
teleports, removal, reset, and reused entity slots. With the optional four-worker
executor, the same cases run above the parallel collection threshold. A wider
fixture checks exact event order and re-entry across groups of contacts. Another
case disables continuous sensing, moves and replaces targets while the swept
index is dormant, then reactivates sensing and checks the first impact.

## Measurement

[Three paired native trials](world-queries-scene-results.json) compare the saved
executable before world-index reuse against the new default serial path.
Profiling is disabled. The Arena3D fixture uses 30 warmup and 120 measured
fixed steps, a stationary player, a prepopulated enemy grid, raised player health
and no firing. All enemy attachments, physics and rendered geometry execute.
It is a crowded workload, not an entire authored combat run.

| Enemies | Physics p50 before ms | After ms | Physics p95 before ms | After ms | Frame p50 before ms | After ms |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 1.642 | 1.421 | 1.837 | 1.554 | 8.355 | 8.340 |
| 5,000 | 7.258 | 5.082 | 8.296 | 6.106 | 12.444 | 10.194 |

The 5,000-enemy physics median falls by 30%, and the frame median by 18%.
Final position checksums match across executables and trials. The fixture also
asserts population, attachment/backend counts, rendered geometry and zero
interpreter calls. The 1,000-enemy wall time remains presentation-bound.
This fixture has no projectile sweeps; it measures the case where the second
index can be skipped. Continuous-sensor scenes still pay for that index.

[Three paired firing/restart trials](world-queries-combat-results.json) exercise
the existing small Arena scenario on SLJIT and BYTECODE. Native simulation p50
is unchanged at 0.013 ms and p95 changes from 0.027 to 0.026 ms. Bytecode p50
changes from 0.021 to 0.022 ms and p95 is unchanged at 0.035 ms. These are small
samples near the timer resolution, with no appreciable change. Both compiled
backends retain the scenario assertions and report zero interpreter calls.

After index reuse, a [separate paired four-worker EnkiTS experiment](enki-world-queries-scene-results.json)
reduces 5,000-enemy physics p50 from 5.089 to 2.207 ms, and frame p50 from 10.172
to 8.487 ms. Both sides of that comparison already reuse the world index.
It combines Box2D solver tasks and engine contact collection tasks. This
experiment used an optional borrowed executor. Production now owns a shared pool;
[the follow-up](shared-task-pool.md) measures its default selection and lifetime.

The later combat fixture exposed a 10,000-step budget failure in the authored
encounter scan at 5,000 enemies. These earlier physics timings remain phase
measurements, but do not establish full gameplay correctness at that population.

The full native sanitizer `make test`, four-worker physics regression run,
`make web` and native JIT-unavailable check all passed. The portable backend
smoke now runs the physics cases under BYTECODE with SLJIT absent. The follow-up
adds firing, damage, XP and restart parity at 1,000 enemies. Longer-lived projectile
swarms and corrected encounter execution at larger populations remain unfinished.
