# Frame-time optimization roadmap

September 14, 2026. Planning baseline: `fca335eb`, after editor integration and
the input-action lookup fix. This document records future options, their likely
scope, and the measurements needed to choose between them. It implements no
runtime changes and makes no new benchmark claims.

The goal is good performance by default for hundreds through tens of thousands
of entities. Prefer compiler/runtime optimizations that preserve authored
behavior. Changes to collision response, update frequency, or visual quality
need an explicit authoring contract. Shared capabilities must work beyond Arena.

## What the current numbers establish

The latest controlled Arena comparison uses 20,000 initial enemies, 1280x720,
Release without sanitizers, Auto eight workers, 30 warmup frames and 240 combat
frames. Values are medians across three runs, with each run first calculating
its own percentile. Phase medians do not add up to the whole-frame median.

| Metric | After input lookup fix |
| --- | ---: |
| Whole-frame median | 16.262 ms |
| Whole-frame P95 | 28.959 ms |
| Physics median | 6.973 ms |
| Behavior and runtime orchestration median | 5.427 ms |
| Rendering and host work median | 1.917 ms |
| Other systems median | 1.002 ms |
| Presentation median | 0.061 ms |

The same-scene frame median improved from 23.312 ms, about 30%. All paired runs
used eight behavior/render workers, made zero interpreter calls, matched combat
traces and passed restart. These are recorded results, not a new measurement.
[Method and raw results](input-action-lookup.md).

The benchmark advances synthetic time by exactly 1/60 second and asserts one
simulation tick per measured frame. Its P95 is therefore not caused by multiple
catch-up ticks. It includes host and presentation work, so it is not a direct
measurement of GPU execution either. Real-time display pacing and catch-up need
their own acceptance run. See `arena_scene_scale_run` in
[the scene fixture](../../test/arena_ui.c) and the
[fixed-step engine](../../src/runtime/engine.c).

Earlier profiles provide leads, not current per-phase promises. A separate
1,800-frame, denser run recorded 146,830 contact pairs. Its physics phase means
were 4.915 ms for Box2D, 2.725 ms for contact collection, 1.007 ms for sweeps,
0.536 ms for body synchronization and 0.183 ms for writeback. Contact dispatch
added 1.466 ms inside the behavior bucket. Dependency checks and prepared binding
creation accounted for 36% of sampled Tick-update stacks. These overlapping
samples include descendants and waits; they are not CPU utilization or a
recoverable millisecond budget. Reprofile after the editor/content changes.
[Historical diagnostic](remaining-runtime-costs.md).

## Frame targets and evidence required

| Display rate | Time available per displayed frame |
| --- | ---: |
| 60 FPS | 16.67 ms |
| 90 FPS | 11.11 ms |
| 120 FPS | 8.33 ms |
| 144 FPS | 6.94 ms |

Moving P95 from 28.959 to 16.67 ms requires about a 42% reduction in that
percentile. This is not a claim that every phase needs the same reduction.
Nor does removing the median physics cost prove the resulting P95.

Suggested first milestone on a named reference machine: median at or below
12 ms, P95 at or below 14 ms, P99 at or below 16.67 ms. These are proposed
engineering targets with headroom, not measured capability or a universal
hardware guarantee. Report missed-budget frequency, longest consecutive miss
sequence, and worst frames too. P95 alone permits one slow frame in twenty.
At higher refresh rates, keep the existing 60 Hz simulation and interpolation
unless a scene explicitly needs a higher simulation rate; rendering at 120 Hz
does not require doubling physics frequency.

Before claiming consistent 60 FPS:

1. Capture correlated per-frame timings, not only independent percentile
   summaries. Split Tick planning/body execution, commands/lifecycle, input,
   Box2D, contact collection, sweeps, dispatch, render preparation/upload,
   GPU execution, and presentation wait. Attribute the actual slow frames.
2. Record population, awake bodies, candidate/accepted/duplicate pairs, contact
   listeners, queries/candidates, allocation bytes, buffer growth, generated
   code work, tasks, join waits, event backlog and simulation-step count.
   Keep these counters cheap; sample detailed traces only around slow frames.
3. Retain the short paired fixture for regression comparisons, then add longer
   deterministic scenarios. Cover dense contact, sustained projectile load,
   mass death/spawn, distant entities, mixed graphs, hot apply, and restart.
   The current short 20k run had a peak of one projectile. It does not establish
   bullet-heavy performance. Run several minutes for thermal and pacing checks.
4. Compare at 1k, 5k, 10k and 20k with fixed assets, seed, input and build flags.
   Use repeated alternating runs and publish distributions, machine/core budget,
   resolution, backend and executable hashes. Keep cold start and warm play
   separate. The existing runner supports up to 1,800 frames; extend it for
   longer runs rather than presenting a nonexistent command option.
5. Measure optimized standalone and editor Play separately. Detailed behavior
   inspection currently selects serial execution, so its timing is unsuitable
   for judging the ordinary parallel path. Validate native and browser targets
   separately; their worker/backend configurations differ.
6. For semantic-preserving changes, require event order, state, RNG, errors,
   waits, cancellation, hot apply and restart parity. Use race/sanitizer checks
   separately from timing. Add a second scene without Arena-specific mechanics.
   For quality changes, publish the changed contract and tolerances instead of
   claiming identical gameplay.

Existing starting points are [the paired scene runner](../../tools/bench_arena_scene_scale.py),
[Tick runner](../../tools/bench_ticks.py), [physics settings runner](../../tools/bench_physics_settings.py),
and [current raw scene results](input-action-lookup-results.json). Fixed input,
uncapped work timing, and real display pacing answer different questions.
Do not use a tighter catch-up cap or dropped simulation time to pass a speed gate.

## What is already implemented

These should not reappear as proposals to do for the first time:

- Whole-function native SLJIT and portable behavior bytecode, explicit oracle
  comparison, and native no-JIT rejection. Generated C comparisons replace
  bodies/kernels in a test executable; a full AoT Scene export is still separate.
- Prepared Tick dispatch, homogeneous native system loops, repeated interleaved
  sequence kernels, retained kernels across compatible attachment churn, and
  parallel execution of proven-safe spans. The historical 99.8% figure counted
  prepared Tick entries in that fixture, not all Tick executions or all graphs.
- Query membership reuse, constant reflected count-reduction fusion, typed
  component access, managed scratch reuse and large scalar Set indexes. Arrays
  retain ordering/duplicates. Source-linked query/container diagnostics exist.
- Shared EnkiTS execution for Box2D, contact collection, eligible Tick spans and
  geometry preparation. Auto uses up to eight workers on the measured machine;
  inexpensive/small jobs stay inline. Web currently stays serial.
- Geometry culling before expensive preparation, material/ramp caching, improved
  surface influence selection, and Cute 3D batching of compatible instances.
- Reuse of Box2D's world index for ordinary overlaps, conditional swept-tree
  maintenance, parallel contact queries, radix sorting and disabled unused
  Box2D contact/sensor event buffers.
- Indexed input listener checks across active asset generations, rather than
  every behavior instance and graph node.

Sources: [parallel work](parallel-scene-work.md), [Tick sequences](tick-sequences.md),
[query fusion](query-fusion.md), [containers](container-choice.md),
[render/contact work](render-contact-optimization.md), and
[input lookup](input-action-lookup.md). Older reports describe intermediate
states; their old capacity limits and threading descriptions are not current.

## Web build: measured changes and open work

September 30, 2026. `make profile-compare PROJECT=projects/arena2d
PROFILE_SCENARIO=Firefight` replays the same 1,800 fixed steps natively and
as WASM under Node; `make web-profile` names WASM functions and trace zones in
the browser's profiler, and `Module.chibiFrameSummary()` reports the page's
frame log. WASM Firefight per-frame times, median of three runs:

| Change | p50 | p99 | max |
| --- | ---: | ---: | ---: |
| Starting point | 5.9 ms | 96.2 ms | 113.0 ms |
| Deterministic field sine and cosine | | 30 ms | 32 ms |
| Terrain prefetch baked in 16-row slices | 7.2 ms | 10.5 ms | 28.2 ms |
| Cute without ASYNCIFY, at -O3 | 6.3 ms | 9.5 ms | 18.8 ms |

Slices trade some median time for the tail. Native is 4.2 ms p50 and 6.4 ms
p99 on the same run. Later detailed attribution identifies material-fluid
relaxation as the larger remaining WASM cost in Firefight; Box2D itself is small.

Measured and not kept: app-wide `-msimd128` and `-flto` changed no percentile
beyond run-to-run noise. `INITIAL_MEMORY` would not help: the heap grows eight
times in the first 0.6 s of loading, each under 1.3 ms, and never during play.

Checklist status, September 30. The
[implementation report](frame-time-implementation.md) records the final checks,
matched measurements, rejected experiment and remaining optimization candidates.
The older option catalog below describes future experiments rather than an
implementation checklist.

- [x] Find the remaining native/WASM divergence. Complete Firefight responses
  match after 1,800 steps; an independent HRC scene also matches. Deterministic
  math and upstream physics compilation policies remove the observed drift.
- [x] Use deterministic sine and cosine in behavior math, vector angles,
  bytecode, native JIT and generated C.
- [x] Speed up the field VM with a contiguous row kernel. The matched native
  bake is 3.95 times faster with identical output hashes.
- [x] Attribute and reduce WASM physics work. The field kernel reduces terrain
  preparation inside physics synchronization on both platforms. Detailed phase
  timings identify material relaxation as the next target. The pressure-neighbor
  cache was rejected because it had no repeatable benefit. Whole-frame tail
  improvement remains unproven under varying machine load.
- [x] Decide the catch-up cap from real browser evidence. Retain
  `ENGINE_STEPS_MAX` at eight; capture correlated intervals, fixed steps and
  dropped time, including scene resets. Startup/reset preparation is the next
  place to smooth bursts.
- [x] Add optional browser threading and initial runtime Wasm behavior codegen.
  The scoped service-worker bootstrap supports static hosts without isolation
  headers. Helper-free numeric regions compile asynchronously; wider function
  lowering and pthread module handoff remain future C8 work.
- [x] Fix headless collaboration checkpoint restoration by guarding app-dependent
  Cute and renderer work. The full native suite passes.
- [ ] Restore hosted CI. Local checks pass, but repository Actions are disabled
  and GitHub reports failed payments or a spending limit. An account owner must
  resolve hosted execution; no Actions or billing setting was changed.

## Recommended order

The September 19 [slow-frame attribution and swept-target experiment](swept-targets.md)
now guide the order below. In the baseline's slowest engine frames, swept-target
maintenance averaged 7.964 ms and the contact/lifecycle pump 6.671 ms, compared
with 0.252 ms for parallel Tick execution. Batched tree repair reduces the
swept-target phase P95, but repeated full-scene acceptance remains unresolved.
Prioritize contacts/lifecycle and physics costs before further isolated kernel
polish. The historical baseline and option catalog remain useful context.

The [contact lifecycle follow-up](contact-lifecycle.md) separates event dispatch
from lifecycle maintenance. Incremental attachment removal now preserves the
unchanged prefix and reduces behavior/runtime P95 in every measured scene pair.
Contact collection/sorting and the physics solve remain the larger next targets.

Implementation status: cached dependency/binding preparation and incremental
Tick schedule repair from S1/S2 are present. S3 now uses a shared world clock;
see [the shared-clock measurements and next behavior work](shared-clock.md).
Large spans now also [validate rows in parallel](parallel-validation.md), with
a join before behavior effects and serial validation for small spans.
The compiler now reuses [stable reads across branches](branch-reads.md), and
parallel plans [prepare only demanded target snapshots](query-snapshots.md).
The original measurements above predate these changes.

Impact below is a priority judgment based on current cost and scaling behavior.
It is not a predicted speedup. Medium effort means work within an existing
module; large means a new execution/storage contract or several modules.

| Priority | Work | Potential | Effort | Main acceptance |
| --- | --- | --- | --- | --- |
| First | Correlate slow frames and counters | Makes P95 work attributable | Medium | Explain the slow-frame population with low measurement overhead |
| First | Incremental Tick preparation, indexes and clock access | High when population churn repeats broad work | Medium/large | Stable-frame planning disappears; churn scales with changes |
| First | Demand-aware contact processing and swept-target maintenance | High in dense/contact-heavy scenes | Large | Preserve exact contact contract and reduce measured candidates/work |
| Next | Retained query plans, spatial queries, incremental reductions | High for query-heavy content | Medium/large | Avoid repeated world scans without stale results |
| Next | Compact hot state, bulk lifecycle work, bounded allocations | High under churn or memory pressure | Large | Lower bytes touched and growth spikes at equal workload |
| Next | Better parallel eligibility, partitioning and dependency scheduling | Medium/high when enough independent work exists | Large | Reduce critical-path time, not merely increase worker count |
| Then | Shared IR optimization, SIMD and complete AoT exports | Workload-dependent; useful across content | Large | Full-scene gains beyond isolated kernel ratios |
| As measured | Remaining render services, GPU work and editor overhead | Larger at higher resolutions or richer scenes | Medium/large | CPU/GPU attribution and pixel/visual acceptance |
| Explicit alternatives | Simulation/visual LOD, simplified or hybrid physics | Potentially very high | Large | Author-approved fidelity contract and quality tests |

## Tick scheduling, events and lifecycle

**S1. Cache immutable dependency proofs and binding templates.**
`prepare_updates` checks query preservation and recreates prepared bindings when
the attachment revision changes. Cache function-level proof results on immutable
program generations, with separate revision checks for mutable services/schema.
Keep reusable binding templates alive across schedule rebuilds. Measure proof
calls and binding allocations per generation versus per population. Adding one
enemy must not repeat the same analysis for every other enemy.

**S2. Update only changed schedule segments.**
Prepared schedules already survive unchanged frames. Extend that to insertion,
removal, replacement and changed services: retain stable rows, spans and private
worker bindings, repairing only affected boundaries. Consider stable attachment
slots plus a separate deterministic iteration order, or chunked ordered storage.
Benchmark repeated projectile spawn/death and mass retirement. Do not replace
FIFO/attachment order with unordered swap removal unless equivalence is proven.

**S3. Eliminate remaining population-wide clock writes.**
Identical clock values are already skipped. Changed simulation time still writes
every instance. Let compiled rows read a shared tick context and snapshot time
only when an activation requires it, or update just instances that consume it.
Preserve pause, waits, delta time, pooled-instance initialization and exact
authored time values. Count stores/cache misses and measure sparse-active scenes.

**S4. Hoist stable row guards to safe span boundaries.**
Program, schema, service and observation checks may be shared across a span when
no operation can invalidate them. Keep necessary entity-generation and failure
checks where their guarantees expire. The existing checked compiled route must
resume at the correct row without repeating effects. Test destruction, hot apply,
debugger activation and a failure halfway through a sequence.

**S5. Compile common paths around rare structural branches.**
A whole Tick currently loses batch eligibility for some command/wait paths even
when that branch is not taken. Emit a fast synchronous region with a precise
continuation into compiled activation storage when spawning, destruction or a
wait is reached. Preserve all earlier writes, RNG draws and source diagnostics
once. Measure actual execution share, not percentage of published graph kinds.
This could matter more for projectile-rich scenes than the current short fixture.

**S6. Batch ordered events and structural commits.**
Use typed contiguous event payloads, compiled event handles, recipient indexes
and bulk lifecycle operations where profiling shows overhead. Amortize component,
attachment, query and physics-index maintenance at existing semantic barriers.
Commands may be prepared privately in parallel only when their visible commit
order remains unchanged. Immediate reads after writes and Begin-before-Tick
prevent arbitrary deferral. Avoid extra one-frame contact or damage latency.

**S7. Schedule waits by deadlines or readiness.**
The pending-activation index already avoids scanning every idle instance, and
its pump barely appeared in the historical Arena profile. A deadline heap or
timing wheel and host completion queues are future options for thousands of
waiting actors. Keep deterministic same-time ordering, cancellation and paused
host waits. Prioritize only after a wait-heavy scene demonstrates this cost.

Code: [behavior scheduling/indexes](../../src/runtime/behaviors/behavior.c),
[parallel planning](../../src/runtime/behaviors/parallel.c),
[update runtime](../../src/graph/runtime/update_jit.c).

## Contact work while retaining Box2D

**P1. Collect only contacts that have a consumer.**
Separate solid collision response from gameplay-contact demand. Maintain compiled
listener/query demand by category and generation; skip engine pair storage and
event creation for proven-unobserved pairs. Box2D must still solve required solid
contacts. Define listener hot-add/replacement semantics first: a newly attached
listener may need the preexisting overlap state, so an empty cache cannot silently
create a new Begin. Include native services and diagnostic consumers in the proof.
Measure candidate, accepted, retained and delivered pairs separately.

**P2. Maintain contact state incrementally where semantics allow.**
Evaluate buffered Box2D events or active pair caches for eligible solid pairs,
with explicit supplemental handling for current-pose overlap, zero-delta sync,
non-dynamic pairs and continuous sensors. Invalidate by motion, filter changes,
teleports, shape/body replacement and generational identity. A direct substitution
of Box2D event buffers changes our current contract. Compare each contact's tick
and recipient order, re-entry, wall resting contact and moving-target hits.

**P3. Make continuous-sensor support proportional to relevant targets.**
The private swept tree is already dormant without continuous sensors. While one
exists, investigate aggregate category masks, active target lists, dirty proxy
updates, separated static targets and batched casts. Parallelize endpoint/AABB
preparation, keeping unsupported tree mutations serialized. Preserve relative
motion, nearest hit and deterministic ties; indexing only final positions misses
targets that crossed the projectile path. Test a single short-lived projectile
among 20k bodies as well as thousands of simultaneous projectiles.

**P4. Reduce duplicate collection and pair-buffer traffic.**
Stable radix sorting already replaces large comparison sorts. Next measure overlap
versus solver-contact duplicates, private-buffer growth and merge bandwidth.
Consider pair ownership before narrow-phase tests, cached exact shape data,
local deduplication or incremental canonical pair tables. Avoid a contended global
hash table that costs more than the existing linear passes. Preserve unsigned
generation-aware ordering and benchmark sparse and dense contact separately.

**P5. Synchronize only changed bodies and transforms.**
Use component/field change tracking and active body lists to avoid reading and
comparing unchanged configuration every tick. Consider Box2D body-move events for
writeback where they cover the contract, with explicit teleports and swept-sensor
clamping handled separately. Avoid routine destroy/recreate and transform writes
that act as teleports. This complements S6 and data-layout work; it cannot skip
velocity updates that behavior changed. Measure sync and writeback individually.

**P6. Review unnecessary collision relationships and shape complexity.**
Compile author-declared collision matrices into the earliest available filters.
Offer diagnostics for expensive category combinations, excessive compound
shapes and dense overlap at spawn. Creating bodies at their intended positions
already matters; do not briefly stack them at the origin. Disabling enemy/enemy
blocking, shrinking colliders or changing spawn layout is a gameplay decision,
not a silent engine optimization. Measure pair density, not just entity count.

**P7. Retest solver settings only against a stated quality target.**
Editor controls already expose workers, substeps, sleeping, solid CCD, force
drive and contact tuning. Earlier tests favored eight workers over four, ten or
fourteen. One substep improved the median but left P95 near 29 ms and failed
crowd-separation tolerance. Sleep/solid-CCD disabling and force drive did not
justify new defaults. Body damping/friction are already zero and rotation fixed;
contact damping does not reduce the number of solver passes. Selective CCD and
sensor sweeps remain quality choices with thin-wall and moving-target tests.
[Recorded tuning and limitations](physics-settings.md).

**P8. Reevaluate upstream Box2D changes on our workloads.**
Compare a newer pinned version only with a migration/parity plan, phase timings,
worker scaling and dense-contact tests. Do not predict gains from release names.
Split independent physics worlds only if the authoring contract rules out
cross-world interactions or defines their handling; splitting a connected crowd
does not automatically create independent simulations.

Implementation: [physics service](../../src/runtime/scenes/physics.c).
Contract: [contact queries](physics-queries.md). Upstream reference:
[Box2D simulation manual](https://box2d.org/documentation/md_simulation.html) and
[the pinned 3.1.1 definitions](https://github.com/erincatto/box2d/blob/v3.1.1/include/box2d/types.h).
The local tests and pinned source remain the authority when online docs advance.

## Queries and container algorithms

**Q1. Retain query plans and maintain selective membership.**
Extend existing reuse across stable frames using component-membership revisions.
Choose between dense candidate lists and occupancy bitsets based on measured
density. Maintain counts incrementally for predicates whose inputs have complete
change tracking, including field writes and destruction. Reading a cached count
can replace an entire traversal, but observer/update costs must be included.

**Q2. Expand fusion and avoid unnecessary snapshots.**
Current fusion handles compatible constant reflected counts. Recognize safe
ForEach/filter/map/reduction patterns and stream directly over a borrowed stable
view when results do not escape, suspend or observe intervening writes. Share
membership or pure results across entities only while their dependencies remain
unchanged. Retain original error, iteration, budget and debugger behavior.

**Q3. Add current-state spatial queries for large candidate sets.**
Selective targeting is already improved; many eligible targets still need a
spatial algorithm. Evaluate sparse grids, dynamic trees or specialized nearest
queries by radius distribution and motion. `Get Contact Entities` currently
observes ECS transforms before physics sync, so reusing stale Box2D state is not
equivalent. Maintain a current index or merge a dirty/spawned overlay, and keep
full-generation handles and stable equal-distance ties. Test uniform, clustered
and distant outlier populations to avoid a new grid-size landmine.

**Q4. Stop copying growing persistent containers when ownership permits.**
Large scalar Set membership is indexed, but repeated Set Add still copies values
and can become quadratic. Use escape/liveness analysis for in-place mutation of
uniquely owned values, reserve capacity, provide compiler-created builders and
move values at their final use. Values retained by another variable, host call or
wait must keep snapshot semantics. Measure bytes copied and total construction
time, not only warmed lookup speed.

**Q5. Select additional representations from declared semantics and workload.**
Candidates include small inline containers, sparse/dense sets for bounded integer
domains, bitsets, aggregate-key indexes and retained lookup sidecars for repeated
Array Contains. Ordinary Arrays must still preserve duplicates and order. Never
index solely on entity slot when generations affect equality. Charge index build,
invalidation and memory against expected reuse; hashing a one-off tiny array can
be a regression. Arena currently has no Set nodes, so Set improvements primarily
protect future content rather than explain its present frame cost.

Sources: [query fusion](query-fusion.md), [container choices](container-choice.md),
[entity queries](../../src/runtime/behaviors/entity_query.c),
[query lowering](../../src/runtime/behaviors/entity_query_lower.c).

## ECS storage, memory and bulk work

**D1. Separate hot execution data from cold instance metadata.**
Extend existing lazy/paged storage and scratch reuse. Keep owner, clock context,
program/binding and frequently accessed variables close together; keep debugging,
labels and dormant activation metadata out of the streaming path. Measure RSS,
bytes per entity, cache misses and bandwidth at equal behavior complexity.

**D2. Use compact iteration storage where it pays for its maintenance.**
Our ECS already stores separate component columns indexed by entity slot, with
occupancy and optional ordered dense membership. This is not an object-per-entity
design, but sparse slot ranges and full component structs can still waste cache
traffic. Experiment with packed hot component rows, pages/chunks, field-level
structure-of-arrays, or small AoSoA blocks for common numeric loops. Account for
handle-to-row indirection, pointer invalidation, component churn and scatter-back
cost. Stable compiler bindings must be invalidated by storage revisions.

**D3. Make structural maintenance proportional to changed entities.**
Ordered dense membership currently inserts/removes with shifting or scanning;
attachment maintenance can rebuild across the world. Batch changes at existing
barriers, reuse freed slots and consider bitset iteration or chunk-local holes.
Keep observable query/command order stable. Churn-heavy cases, not a static
population scan, decide whether the new representation wins.

**D4. Bound allocation spikes and memory retention.**
Reuse worker scratch, event/contact buffers, result stores and generated-code
caches with measured high-water capacities. Prewarm expected prototype, shader
and program needs before Play. Track arena growth, large copies, page faults and
retained generations separately. Reclaim retired generations after their last
activation; do not let preallocation consume memory proportional to every
possible entity/component combination. Cold-load latency and peak RSS are gates.

**D5. Prototype Flecs only against a specific storage or maintenance bottleneck.**
Cached archetype queries can iterate prematched tables, and staged writes provide
defined synchronization boundaries. Those are useful ideas for our ECS too.
Flecs adds cache/migration costs and its staging can change when writes become
visible. A fair comparison must include our mutation rate, ordering, reflection,
compiled column access and hot apply. No current evidence proves a wholesale
replacement would beat targeted fixes. See the primary
[query/caching documentation](https://www.flecs.dev/flecs/Queries.html) and
[staging semantics](https://www.flecs.dev/flecs/Systems.html#staging).

Code: [ECS implementation](../../src/ecs/ecs.c) and
[execution view](../../src/ecs/execution.h). S1-S4 and Q1-Q3 can proceed with the
current ECS; an archetype migration is not a prerequisite.

## Compiler, native code and exports

**C1. Optimize repeated loads and values across larger safe regions.**
Extend shared-IR constant propagation, common-expression elimination, dead
stores, loop-invariant hoisting, scalar replacement and component load/store
coalescing where current lowering misses them. Specialize on immutable program
data or guarded layout facts. Host effects, aliasing, errors and debugger safe
points constrain these transformations. Measure executed operations and spills;
do not trade a small body gain for excessive code size or compilation pauses.

**C2. Fuse independent work across graph boundaries.**
Sequence kernels already embed different bodies under one loop/prologue.
Further optimization could keep values in registers across bodies, share checks
and pure expressions, or use phase-based loops for proven-independent entities.
Changing entity-major execution to graph-major execution can alter cross-entity
reads, RNG and failures. It requires an effect/dependency proof and a checked
compiled path for cases that fail the proof.

**C3. Vectorize proven-independent rows.**
Add vector IR or bulk kernels for movement, numeric transforms, predicates,
culling and simple reductions after arranging suitable data. Keep scalar tails
and guards for aliasing, missing components and source errors. Floating-point
reassociation, reduction order, NaNs and fused operations need explicit numeric
semantics. Compare native SIMD to generated-C vectorizer reports and portable
bytecode results; SIMD is not an automatic consequence of SLJIT or SoA.
[LLVM vectorizer guidance](https://llvm.org/docs/Vectorizers.html).

**C4. Choose helper boundaries by actual work and optimizer visibility.**
Tiny, frequent numeric/component operations are candidates for direct lowering
or inlining. Complex collision algorithms, allocation and indexed lookup may be
efficient native helpers. Batch calls when ABI setup, repeated lookups or hidden
alias/effect information costs more than the operation. Give helpers precise
contracts so the compiler can optimize around them. Re-emitting Box2D in SLJIT
would not remove collision work; eliminating redundant calls or candidates can.
Use measured whole-frame benefit, not the percentage of instructions written in
JIT source, as the decision rule.

**C5. Finish a real AoT Scene export and compare full applications.**
Emit/link every reachable behavior and system kernel, asset layout, host binding,
managed value and continuation needed by the scene, including waits and errors.
Provide an explicit AoT program-loading/backend contract; do not hide an
interpreter fallback or label generated C as SLJIT. Preserve native editor
SLJIT requirements and the portable bytecode route. Compare the same standalone
scene under SLJIT and AoT, including startup, memory and P95. Removing the editor
does not by itself remove scheduler, ECS, physics or host-service costs.

**C6. Evaluate LTO, profile-guided optimization and CPU variants.**
For generated C and native helpers, test optimized release, link-time optimization
and profiles representative of multiple scenes. Check optimization remarks and
code size. A compiler can then see more call sites and specialize hot paths, but
cannot remove required physics or event work. Use portable baseline binaries or
runtime-selected CPU variants for exports, not development-machine-only flags.
Avoid global fast-math as an implicit gameplay change. See
[Clang's PGO guidance](https://clang.llvm.org/docs/UsersManual.html#profile-guided-optimization).

**C7. Optimize the portable VM without changing backend policy.**
Evaluate superinstructions, specialized typed operations, region execution and
reused frames after profiling browser opcode dispatch. Shared algorithmic and
storage optimizations help bytecode immediately; direct machine-code scheduling
does not. Any fused bytecode operation must retain budgets, waits, exceptions,
source maps and debugger behavior. AOT-to-Wasm exports are a separate extension
to the export contract; ordinary browser-authored behavior remains bytecode.

**C8. Generate WebAssembly for behavior graphs at runtime in the browser.**
September 30 status: an initial helper-free numeric-region tier is implemented
with asynchronous compilation and bytecode fallback. Full function/helper
lowering, waits and pthread module handoff remain extensions. See the
[implementation and input-kernel measurements](frame-time-implementation.md).

The browser cannot execute SLJIT output, so web behavior runs as bytecode. The
MovementSystem benchmark measures native SLJIT at 18 ns per entity-frame, typed
bytecode in WASM at 70 ns and generic bytecode at about 750 ns. The browser's
equivalent of SLJIT is to emit a small Wasm module per published program from the
shared IR. Each module would import the linear memory, function table and host
helpers, and be instantiated with `WebAssembly.Module`/`Instance`. The browser
then compiles it with its own optimizing tiers.

Open questions:
- Compilation latency. Chrome limits synchronous compilation on the main thread
  to small modules, so larger programs need asynchronous compilation, with
  bytecode running until the module is ready.
- Calls across the JS boundary for host helpers.
- How waits and continuations resume in generated code.
- How to preserve budgets, errors, source maps and debugger safe points.

Keep bytecode as the reference backend and require parity with it, as SLJIT
does. Emit through a small in-tree encoder, not an external toolchain. See
[WebAssembly JS API](https://webassembly.github.io/spec/js-api/).

Sources: [shared optimizer](../../src/graph/compiler/behavior_function_opt.c),
[whole-function measurements](whole-function-performance.md),
[system/sequence C comparison](tick-sequences.md),
[latest parallel kernel comparisons](parallel-scene-work.md).
Several existing kernels already run close to generated C; do not assume a
large AoT frame-rate gain from an isolated C/SLJIT ratio.

## Parallel execution and frame scheduling

**T1. Tune granularity using actual work and join cost.**
Existing thresholds deliberately leave cheap loops inline. Improve cost estimates
with sampled execution cost, chunk imbalance and cache behavior. Avoid tiny jobs,
false sharing, hot shared counters and oversubscription across simultaneous
worlds. Explore physical/performance-core-aware budgets on each platform; more
logical cores are not always faster. Keep reproducible manual settings.

**T2. Expand safe parallel eligibility.**
Use reflected read/write/effect contracts to prove more host operations and
independent event handlers safe. Replace contended shared output mutation with
private staging and deterministic merge where the operation's visibility permits
it. Keep global RNG order or explicitly declared per-entity streams, with parity
tests. Do not move an unknown helper onto workers merely because it is native C.

**T3. Schedule by dependencies instead of joining every broad phase.**
The current phases join before the next phase. A persistent dependency graph
could overlap truly independent preparation, then join only required producers.
Start with measured idle gaps. Physics cannot race with behavior writing its
inputs; renderer extraction cannot read partially updated state. EnkiTS already
has task dependencies, so replacing the pool is not required for this experiment.
[EnkiTS dependency support](https://github.com/dougbinks/enkiTS#dependencies).

**T4. Pipeline rendering from immutable snapshots.**
Keep render extraction separate from submission and investigate double-buffered
snapshots or a render thread if the critical path warrants it. Preserve buffer
lifetime, interpolation, picking, editor edits and graphics-context requirements.
Using an older simulation snapshot may add latency and memory; disclose that
tradeoff. Asynchronous GPU execution and extra CPU threads are different things.

**T5. Add a separate browser threading deployment option.**
September 30 status: the optional prestarted-pthread bundle and static-host
isolation bootstrap are implemented and validated locally. Live GitHub Pages
deployment remains untested. The serial build is the default.

Default browser behavior/physics run serially. Evaluate pthread workers and
prestarted pools, with the required SharedArrayBuffer isolation headers and a
single-thread deployment alternative. Check whether our hosting can supply the
headers before planning performance around them. Keep browser UI/main-thread
restrictions and supported graphics APIs in the design. This requires deployment
work, not just increasing the native worker setting.
[Emscripten pthread requirements](https://emscripten.org/docs/porting/pthreads.html).
Wasm SIMD is another independent option for compiled host kernels; enabling it
does not automatically vectorize behavior bytecode.
[Emscripten SIMD guidance](https://emscripten.org/docs/porting/simd.html).

## Rendering, GPU and editor overhead

**R1. Cache immutable render preparation and process changes in bulk.**
Retain prototype geometry/parts, material layouts, static transforms and static
draw groups by revision. Upload changed instance ranges; avoid repeated parameter
serialization/parsing. The renderer already culls and caches several inputs, so
measure misses and upload bytes before adding more caches. Preserve hot edits,
invalid-material diagnostics, per-entity overrides and transparent draw order.

**R2. Optimize the serial render services still scanning entities.**
Profile surface influences, lights, trails, visual-motion lookups and emitters.
Use selective memberships, change tracking and private worker outputs when they
are expensive. Prior surface influence fixes already removed a major cost.
Particles barely appeared in the old CPU sample; future particle-rich content
needs its own profile. Do not remove offscreen entities' simulation or lighting
effects under the label of geometry culling.

**R3. Reduce submission work only where batching evidence supports it.**
Cute 3D already coalesces compatible instances. A prior capture averaged roughly
1,468 instances in seven commands, so a draw-list rewrite alone is not a credible
large win for that workload. Investigate instanced bulk submission, retained
static lists, fewer mesh/material splits or indirect draws if new captures show
CPU submission or command count dominating. Dynamic actors still need current
transforms. [Local batching evidence](render-contact-optimization.md).

**R4. Measure and optimize GPU passes separately.**
Add GPU timestamp scopes where supported. Inspect fill rate, overdraw, shader
cost, render-target traffic, lighting and postprocess resolution. Options include
tiled/clustered light selection, cheaper material variants, particle batching,
occlusion/LOD, half-resolution effects and explicit dynamic resolution. Compare
fixed-resolution and camera-close worst cases. Visual approximations require a
quality setting and image review, not a silent compiler optimization.

**R5. Keep editing and inspection work off normal gameplay paths.**
Measure graph evaluation previews, traversal history, watches, layout, asset
scans and duplicate scene views. Reuse generation-based results and avoid
rendering or inspecting unused views. Preserve responsive authoring and hot apply.
Current detailed behavior inspection deliberately runs serially; measure its
cost separately and consider lighter sampling for runtime summaries.

**R6. Separate GPU work from presentation pacing.**
Time swapchain acquisition, submission and display waits independently. Check
present mode, refresh rate, compositor behavior and queued-frame depth before
calling a large Present scope a rendering bottleneck. Reducing excessive buffered
frames can improve latency without increasing throughput. The latest benchmark
had little presentation wait; this is a separate real-display validation need.

Code: [geometry preparation/submission](../../src/render/arena3d.c),
[scene renderer](../../src/render/scene_renderer.c),
[frame timing](../../src/editor/workspace/frame_profile.c).

## Explicit fidelity choices and larger experiments

**A1. Update expensive decisions less often.**
Author a cadence or maximum staleness for target selection, path planning and
other AI decisions, while movement, contacts and input retain the needed rate.
Stagger phases across entities so all decisions do not run on the same frame.
Automatic event-driven recomputation is safe only with complete dependency
tracking. A Tick that reads time/random or writes effects cannot silently skip
frames. Measure decision latency and worst-case reaction as well as frame time.

**A2. Simulate distant or unimportant entities at a declared lower detail.**
Use relevance zones, sleeping gameplay groups or aggregate crowd state. Define
promotion/demotion, offscreen combat, timers, persistence and replay explicitly.
Visibility alone does not prove gameplay irrelevance. This can change scaling
from all entities to active entities, but it is an authoring policy.

**A3. Offer simplified crowd physics and a supported hybrid.**
Keep Box2D for rigid interactions; add circle separation/spatial-grid motion for
crowds that accept soft overlap. A hybrid needs clear ownership of integration,
collision filtering, walls, projectiles and cross-model contact delivery. Do not
solve/integrate a body twice or maintain two full broad phases without measuring
their cost. The Neon-Swarm experiment measured 1.118 ms median versus 9.971 ms
for a serial Box2D circle workload. It is not a production Arena comparison and
does not prove the frame target. A production simplified backend remains
unimplemented. [Experiment and limitations](simplified-physics.md).

**A4. Use shared fields or aggregate algorithms for very large crowds.**
Shared flow fields, cached navigation, density-aware steering and group-level
decisions may replace per-agent searches. This is promising when agents share a
goal or environment. It adds update/invalidation cost and can alter individual
movement. Benchmark changing targets, dynamic obstacles and crowd compression,
not only an open field with one destination.

**A5. Put suitable cosmetic or isolated simulation on the GPU.**
Particles, culling, simple crowd motion and collision candidates are possible
compute workloads. Avoid per-frame CPU readback that cancels the gain. Gameplay
physics needs decisions on determinism, sorting, interactions with CPU entities,
debugging and supported browser backends. Start with visual-only work or an
explicitly separate simulation model; do not promise a generic GPU behavior VM.

## Prevent future performance landmines

**U1. Make node/component metadata describe effects as well as types.**
Extend the shared reflected contract with reads/writes, ownership, allocation,
suspension, structural effects, ordering and cost shape. Generate repetitive
bindings, validation and diagnostic data from it. Reflection can discover field
layout; it cannot infer whether a helper preserves queries, mutates global state
or changes event timing. New operations need a correct semantic declaration and
relevant parity checks, not a hand-tuned implementation for every whole graph.

**U2. Explain expensive multiplicative work in the editor.**
Build on existing source-linked query/container warnings. Report observed
instances times executions times candidates, copied bytes, contacts, and actual
Tick route. Explain why a graph could not batch/parallelize, which dependency
blocked fusion, and which node caused a large allocation. Keep static estimates
distinct from measured costs. A C helper call alone is not a warning condition.

**U3. Show capacity and fidelity decisions before they become surprises.**
Surface collision-matrix fan-out, fast sensor cost, huge persistent copies,
dynamic material variation, nested world queries and unbounded loops/recursion.
Offer explicit cadence, spatial-query, container or quality choices with clear
semantics. No automatic enemy cap, dropped contacts, or skipped gameplay Tick
should masquerade as optimization.

**U4. Keep algorithmic and full-scene regression coverage.**
Maintain paired kernel/C comparisons as diagnostics, plus deterministic scale,
churn, sparse-world and mixed-content fixtures. Test warmed allocation counts
where allocation freedom is a contract; avoid tautological implementation tests.
Use dedicated reference machines for timing gates and CI for correctness,
backend selection and parallel-route checks. Periodically refresh the P95/P99
evidence; an old 20k benchmark should not certify every future node or scene.

## Decisions to defer until measurement supports them

- Replacing EnkiTS or the entire ECS without identifying a remaining bottleneck
  that their alternatives actually address.
- Moving every native helper into SLJIT, chasing the last prepared singleton
  Tick, or claiming full compiled coverage implies a smooth frame rate.
- Assuming more workers, disabled sleeping/CCD, lower damping or force-driven
  motion must be faster despite our existing results.
- Treating a serial simplified-physics ratio, a kernel C comparison, or a low
  draw-command count as a prediction of full-scene P95.
- Spending heavily on GPU simulation before measuring GPU/CPU critical paths,
  synchronization and readback requirements.

The first implementation sequence should be correlated tail measurements,
cached dependency/binding preparation, incremental attachment/schedule updates
and shared clock reads, followed by contact demand and swept-target work.
Rebaseline after each group. If those preserve semantics and reach the target,
keep the larger storage, AoT and fidelity experiments as independently measured
options for higher populations, richer content and higher refresh rates.
