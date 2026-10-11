# Box2D v3 performance and a specialized crowd solver

September 30, 2026. This compares the Box2D v3.1.1 guidance with this
runtime's physics path, a fresh 20,000-entity Arena profile, and the referenced
Neon-Swarm solver at commit
[`2969cc02`](https://github.com/waldnercharles/Neon-Swarm/blob/2969cc02f07a33d604f2057414835a24e8928b5b/src/physics.c).

## Recommendation

Do not pool disabled Box2D bodies or replace Box2D globally. Stable ECS
entities already keep their body between ticks. Box2D says that enabling a
disabled body costs almost as much as creating it and advises against disabling
bodies for performance; its world allocator and id pools already handle body
storage and identifier reuse. [The v3.1.1 simulation manual](https://github.com/erincatto/box2d/blob/v3.1.1/docs/simulation.md#disabling)
and [body implementation](https://github.com/erincatto/box2d/blob/v3.1.1/src/body.c)
are explicit on this point.

The 20,000-entity Arena workload is a real physics bottleneck. It is a good
candidate for a measured, opt-in crowd-separation path for uniform circles that
only need soft separation. Keep Box2D for general rigid-body behavior, including
walls, fast projectiles, rotation, friction, restitution, and contacts that need
Box2D's response. The existing Neon-Swarm solver is a useful algorithmic seed,
but its semantics are too narrow to replace the shared physics service.

Before implementing that path, compare it with a less invasive optimization to
the current pair collection. A Box2D step averages 7.10 ms in the fresh Arena
profile, while the runtime's separate post-step pair collection averages
3.27 ms and contact dispatch 1.50 ms. A solver-only replacement leaves those
query and event costs in place. The prototype must either preserve that contract
through a shared broadphase, or show that the remaining Box2D queries are still
worth paying for.

## Workload evidence

The new [Arena attribution results](physics-arena-attribution-results.json)
use LLVM 23 Release on an Apple M4 Pro under other machine load: 30 warmup
frames followed by 600 combat frames, at 1280 by 720, with 20,000 ECS entities,
19,981 Box2D bodies, and eight workers. Three runs had identical combat traces
and checksums. Their median-of-run-medians was:

| Measurement | Median |
| --- | ---: |
| Whole frame p50 / p95 | 19.66 / 28.45 ms |
| Physics p50 / p95 | 13.26 / 21.10 ms |
| Box2D step | 7.10 ms |
| Runtime pair collection | 3.27 ms |
| Contact dispatch | 1.50 ms |
| Synchronize / swept tree / writeback | 0.89 / 0.44 / 0.24 ms |
| Pair candidates per frame | 120,292 |

Individual run medians varied: physics p50 was 10.70–14.36 ms and frame p50
was 16.30–20.26 ms. Physics spikes reached 30.96–47.35 ms. The common combat
trace shows the variation is timing noise rather than a different workload, but
the loaded-machine results should not be treated as an optimization baseline
without matched follow-up runs. Frame p95 also exceeds a 60 Hz budget in every
run. This is a meaningful test case for a crowd specialization.

This result does not generalize to every scene. Firefight's current profile puts
Box2D solving at only 0.014 ms per native step and 0.029 ms per serial-WASM
step, while material-fluid relaxation averages 0.487 and 1.623 ms. See the
[frame-time implementation report](frame-time-implementation.md#matched-measurements).
The older 20,000-enemy tuning run also found that eight workers reduced median
physics time from about 9.25 to 6.81 ms; disabling sleep was slower, and lowering
substeps changed the gameplay trace and failed a crowd-separation tolerance.
See [Box2D tuning results](physics-settings.md#box2d-tuning-and-worker-selection).

## Pooling and Box2D settings

`physics` creates a Box2D body when an ECS entity first enters the physics
query, retains it while that entity and its collider configuration remain
stable, and destroys it when the entity retires or its physics configuration
changes. Body creation already supplies the final position and rotation in the
definition, as Box2D recommends. This means an application-side pool would
target entity churn, not per-frame body creation in the stable simulation.

Box2D's body-id pool reuses freed identifiers. Its body data lives in the world
arrays, and destroying a body also removes its contacts, shapes, joints, and
broadphase proxies. A disabled-body pool retains those bodies in world memory;
re-enabling rebuilds simulation participation at close to create cost. Pool ECS
entities only if a separate spawn/despawn benchmark shows that entity or
prototype construction causes a hitch. Such a pool does not reduce steady-state
solver cost and must reset all game and contact state before reuse.

Other upstream guidance that matches the current integration:

- Reuse `b2BodyDef` storage if convenient; Box2D copies it. The current code's
  stack definitions already have this property.
- Set body type and initial transform at creation. Changing type later is
  expensive; teleporting an existing body can hurt performance.
- Use shape category and mask filters for ordinary collision exclusion. The
  manual calls these faster than a custom filter callback. The runtime needs a
  callback for per-entity `ignore_entity`, but its callback also repeats the
  category, mask, and same-entity checks after Box2D's broadphase has already
  checked the native shape filters. A callback-specific check limited to the
  remaining per-entity exclusion is a small native/WASM experiment; retain the
  full predicate for sweep and gameplay-query paths, then compare hashes and
  paired 20,000-entity timings.
- Keep CCD selective. Box2D recommends using bullets sparingly. The current
  authored path turns on Box2D bullets only for dynamic, continuous, solid
  bodies; fast sensor sweeps use the runtime's separate swept tree.
- Keep sleep enabled for scenes with resting bodies. The Arena tuning test found
  no gain from disabling it; actively moving enemies have little opportunity to
  sleep. Sensor-event generation is already disabled in the runtime because
  gameplay uses its own swept/current-pose contact contract.
- Treat substep count as a quality choice. The Arena tests show fewer substeps
  reduce median time but change the trace and increase wall penetration.

Box2D v3.1.1 already uses SIMD in its contact solver. The worktree's current
native compile command targets arm64 and preserves `-ffp-contract=off`, so
Box2D selects its NEON path. The web compile command includes
`-msimd128 -msse2 -ffp-contract=off`, matching Box2D's SSE2-emulation path for
WASM SIMD. Cute compiles Box2D as an object library and bypasses upstream
CMakeLists, so these flags are configured in the consuming build. There is no
missing SIMD flag to add for Box2D; further SIMD work should target measured
engine kernels such as field rows or custom crowd math. See the upstream
[v3.1.1 build notes](https://github.com/erincatto/box2d/blob/v3.1.1/README.md#compatibility)
and [SIMD implementation](https://github.com/erincatto/box2d/blob/v3.1.1/src/core.h).

## What the Neon-Swarm solver fits

The referenced `physics.c` uses structure-of-arrays storage for positions,
velocities, forces, radii, inverse masses, masks, and identifiers. Each step
builds a uniform grid, colors cells in a 2 by 2 pattern, tests same/neighbor
cells, applies a spring-and-damping repulsion for overlapping circles, then
integrates positions and velocities. It also tracks begin/end contact pairs and
has circle, AABB, cone, nearest, and ray queries.

That is a reasonable basis for a large crowd whose intended rule is “move apart
when circles overlap.” It does not model angular motion or torque, box shapes,
friction, restitution, rigid constraint solving, or continuous collision. Its
single `dt` update does not implement Box2D's four substeps or speculative
contacts. Its output will change crowd spacing and impact behavior unless the
gameplay contract explicitly treats the crowd as soft separation.

The parallel path gives each task a dense force array, clears it for each cell
color, and reduces it over all active bodies. This adds work proportional to
workers times bodies even when the contact grid is sparse. The fixed task order
and reduction make a run repeatable for a given partition, but changing worker
count changes floating-point grouping. A production adaptation should measure
this memory traffic in WASM and define whether determinism must hold across
worker counts. The grid can also become inefficient with mixed body radii,
because cell width is based on the largest radius.

## A safe experiment boundary

Build the alternative behind an explicit body or scene capability and apply it
only to circles whose gameplay accepts soft overlap separation. Keep authored
static walls and high-speed projectiles in Box2D. For the first prototype,
choose an Arena crowd fixture with fixed radii, no rotation, and no dependence on
precise enemy-enemy impact response. Preserve player, wall, projectile, and
sensor interactions. Do not silently route all dynamic bodies to the simpler
solver.

Before keeping the prototype, run the same scenario in native Release, serial
WASM, and threaded WASM. Record whole-frame p50/p95/p99 and maxima, allocation
counts, broadphase/pair/solve/dispatch phases, body counts, and candidate counts.
Compare combat outcomes, transforms, velocity bounds, and contact Begin/End
delivery. Repeat on sparse, crowded, and mixed-radius cases. Require a repeatable
tail improvement on the targets that matter, and document the intentional
behavior difference if the simplified path cannot match Box2D exactly.

The existing 20,000-entity benchmark currently has only native evidence. It
justifies prototyping this optional path; it does not yet establish that the
custom grid will beat Box2D in WASM or threaded web builds.
