# Retained destruction geometry and Box2D shapes

October 1, 2026. This batch adds fracture/geometry allocation reuse and collider
updates independently of the shared spatial index. It applies to arbitrary
material masks used by terrain, destructible actors and fragments. It uses the
existing Box2D solver and authored collision settings.

## Fracture and geometry processing

Each destruction processor owns reusable connectivity labels, flood-fill queue,
component counts/edge flags, rectangle occupancy, surface tiles and a chip-cell
snapshot. Capacity follows the largest mask processed by that owner. Reset keeps
capacity; free releases it. Returned fragments own their cells and geometry and
never borrow the scratch buffers. Standalone mask operations use temporary work
with the same extraction algorithm.

Damage now changes occupancy and extracts connected fragments before rebuilding
source geometry. Source and fragment geometry are ready before fragment host
callbacks. A second source rebuild occurs only when capacity overflow restores
detached cells. Chip construction avoids building geometry before connectivity
extraction. Retry queues retain their buffers too.

Host callbacks may enqueue work. Releases appended during release callbacks are
preserved after deferred original releases for the next step. Damage-phase
releases still run in the same step. Recursive step/reset/free of the same
processor is unsupported and asserted; independent processors and standalone
mask calls in callbacks are supported.

Geometry revision remains a monotonic invalidation key. Its numeric increments
now represent published rebuilds, so old redundant rebuilds no longer increment
it. Material world hashes cover cells, distances and damage rather than the
numeric revision.

## Box2D geometry synchronization

A mask revision or incarnation change updates shapes on the existing body.
Generation, body/collider configuration and material-presence changes still
create a new body. An incarnation key survives standalone material resets, so a
new mask with a reused revision cannot leave stale collision geometry.

Old/new rectangle centers are sorted and merged. Unchanged rectangles keep their
Box2D shape and contacts. Equal-center extent changes use `b2Shape_SetPolygon`
only if the actual polygon centroid is also unchanged. Moved rectangles or
changed centroids recreate the affected shape on the retained body. The bundled
Box2D setter retains a cached CCD centroid, so that restriction avoids stale CCD
geometry without modifying upstream code.

Shape creation/removal defers mass calculation. Changed batches calculate mass
once, wake the body and invalidate its auxiliary swept-target bounds. Initial
compound masks also calculate mass once. Engine shape proxies retain the original
material rectangle ordering. Empty material masks have no shapes and never fall
back to analytic collider geometry.

Retaining bodies and unchanged contacts preserves Box2D island and warm-start
history that body recreation discarded. Numerical trajectories can therefore
differ from the previous implementation. The acceptance gate is preserved
gameplay behavior and exact native/scalar/worker agreement for this version,
rather than old-baseline bitwise trajectories.

## Validation and evidence

Focused native material and combat checks pass. Independent read-only reviews
found no defects in scratch ownership, callback geometry, shape matching,
centroid handling, mass updates or reset lifetime handling.

Regression checks cover holed/disconnected/anchored masks, an ellipse using the
same workspace, exact rectangle occupancy, callback geometry, overflow and retry
handling, and warmed allocation reuse. Physics checks cover retained contacts,
swept sensors, solid CCD through a carved opening, mass/center of mass and actual
collision response against a fresh equivalent body, sleeping-contact wakeup,
static/dynamic/kinematic motion, empty masks, configuration changes, entity reuse
and material-only reset with reused revisions.

The full ASan/UBSan native `make test`, paired `make web`, and scalar/four-worker
WebAssembly `check-material-engine` pass. The Web builds also execute the physics
regressions in both serial and padded four-worker modes.

The isolated source checkpoint `392abaabe`, with the original fluid index and
latest main as its common base, has exact complete returned `run_scenario` results
and `hash_parts` across native Release, scalar Wasm and four-worker Wasm for all
eight cases below. The older combined checkpoint `51c9cc64` remains separately
recorded in [historical results](destruction-geometry-results.json). These are final endpoint checks, not complete trajectory
traces. Fixtures and requests are copied unchanged from the frozen spatial
validation. The 20,000-enemy crowd remains intact at 20,008 live entities.
Controller profiles do not report active worker counts; four workers describes
the configured build pool. The separate padded physics smoke checks report
`workers=4`.

| Case | Replay steps | Final world hash | Old frozen endpoint |
| --- | ---: | --- | --- |
| Terrain barrage | 300 | `cb5fd805c5f5d987` | Equal |
| Enemy barrage | 300 | `c2eee7d85b452b4c` | Changed |
| Enemy collisions, 20k | 360 | `269a8b8dae59ab30` | Equal |
| Fog/liquid | 300 | `339350bde84b6ff3` | Equal |
| Gas only | 300 | `1aac6f7d520c19f3` | Equal |
| Liquid only | 300 | `59c9c255688390ce` | Equal |
| MaterialStudy | 5 | `9a44ae1f43f39495` | No reference |
| HRCVolume | 5 | `14252b267128ec69` | Equal |

Enemy barrage differs in ECS, material masks and physics/contact state, consistent
with retained contact history. The physical-contract regressions above test the
required behavior independently. Future timing pairs must record population,
damage, occupied cells, shapes and contact density when outcomes differ.

The `physics_work` diagnostics report body creations, shape creations/updates/removals
and mass updates beside timing objects, with per-frame samples and totals. The
previous frozen candidate did not include these counters, so it cannot establish
comparative work-reduction percentages.

The isolated native comparison uses baseline `47ee7f93` and candidate
`392abaabe`, with the original fluid index and matched Release/compiler flags on
both sides. Six scenes each run three alternating pairs, with 60 warmup and 240
measured steps (120 warmup for the 20k collision control). All 36 profiles and
36 quiet, unexpired boundary checks pass independent audit. The fixed readiness
deadline was retained through three diagnostic harness/readiness attempts; the
fourth qualified at 10:14:12 and 10:15:14 Denver and completed its own run window.

| Matched terrain barrage | Baseline | Retained geometry |
| --- | ---: | ---: |
| Frame p50 | 0.440 ms | 0.431 ms |
| Frame p95 | 0.724 ms | 0.622 ms |
| Average synchronization per frame | 0.10565 ms | 0.08129 ms |
| Average requested allocation bytes per frame | 955,832 | 754,961 |

Terrain synchronization improves 23.06%, frame p95 improves 14.09%, and requested
allocation bytes fall 21.02%. All three terrain pairs improve p50, p95 and average
frame time. Five scenes have matched sampled workload and endpoints. Enemy
barrage remains an outcome/workload diagnostic because retained contact history
changes its final state. Controls vary: liquid p95 rises 2.24%, average frame rises
0.64%, and one pair's p50 rises 8.26%; its median p50 falls 0.64%. These runs support
the terrain improvement and do not establish a general whole-engine speedup.

[Current audit and runs](destruction-geometry-ready-results.json) retain executable,
source, raw input/output and readiness hashes. The independent short-profile audit
also reconstructs 24 three-frame counter sums; long-profile worst-frame samples
cannot reconstruct every frame's counter sum. Baseline has no `physics_work`, so
those counters establish candidate work rather than comparative work savings.
Native and Web builds reused dependency caches. Timing covers headless CPU work;
rendered frames, GPU execution and display pacing were not measured.

## Further options

- Retain owned fragment/chip allocation capacity only if churn measurements show
  these allocations dominate. Pool masks/storage rather than disabled Box2D
  bodies; keep the largest-mask high-water cost visible.
- Measure rectangle matching, shape changes and contact reconstruction per slow
  frame. Shape reuse depends on how extraction changes the mask, not on a
  particular enemy outline.
- Consider a stable incremental rectangle decomposition only after proving exact
  occupancy, contact ordering and collision behavior; a different decomposition
  can change Box2D's numerical response even with the same occupied area.
- Profile unchanged body synchronization and material revision changes before
  adding dirty memberships. Include authoring hot edits, terrain streaming,
  standalone material resets and teleports in the invalidation contract.

Shared algorithms and retained storage benefit native and WebAssembly. Rendered
frame tails, GPU execution, display pacing and long thermal runs remain separate
acceptance needs.
