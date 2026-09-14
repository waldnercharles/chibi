# Remaining runtime costs

For the post-editor baseline, prioritized future options and P95/P99 acceptance,
see the [frame-time optimization roadmap](frame-time-roadmap.md). This report
retains the earlier measurements and sampling evidence.

September 13, 2026. The final paired 240-frame combat comparison at 20,000
initial enemies reports these medians. Each value is the median of three trial
medians, so the phases should not be added to reconstruct the frame median.

| Phase | Milliseconds |
| --- | ---: |
| Physics | 6.692 |
| Behavior and runtime orchestration | 5.150 |
| Rendering preparation and host work | 1.848 |
| Other systems | 0.998 |
| Presentation | 0.060 |
| Whole frame | 15.281 |

Behavior fell from 5.708 to 5.150 ms, about 10%. The label includes contact
reactions, clock propagation, event delivery, lifecycle maintenance and schedule
construction. It is not a timer around only the compiled Tick bodies.
[Paired measurements](parallel-scene-final-results.json).

## Longer diagnostic run

A separate 1,800-frame combat run retained native parallel Tick and rendering
execution and enabled coarse physics phase timers. The process was sampled for
10 seconds, starting six seconds after launch. Per-behavior instrumentation
stayed off because it selects the serial inspection path. The run ended with
19,921 enemies and still reported eight workers for each parallel path and zero
interpreter calls.

The crowd becomes denser over this longer run. These averages describe that
workload, not the short comparison above, and sampling can perturb timings.

| Physics service phase | Mean ms/frame |
| --- | ---: |
| Box2D world step, including collision detection and solving | 4.915 |
| Contact pair collection and merging | 2.725 |
| Projectile sweeps | 1.007 |
| ECS/body synchronization | 0.536 |
| Transform writeback | 0.183 |
| Contact dispatch, charged to the behavior bucket | 1.466 |

The final collected pair count was 146,830. The event backlog peaked at 62 on
16 frames and drained to zero. The run's whole-frame median was 18.179 ms and
p95 was 29.937 ms. Physics contact density and its tail remain a scaling limit.

## What the behavior scheduler is spending time on

The sample contains 7,845 main-thread observations. Inclusive counts include
descendants and wait time; they are not CPU utilization or precise milliseconds.
Rows nested under Tick update must not be added to the Tick update total.

| Sampled call | Observations | Interpretation |
| --- | ---: | --- |
| Tick update, inclusive | 1,474 | Schedule preparation, execution and guarded dispatch |
| Parallel span execution, within Tick update | 466 | Row validation, query preparation, execution/join and result handling |
| Query-preservation proof checks, within Tick update | 322 | Repeated dependency checks while rebuilding prepared rows |
| Prepared binding creation, within Tick update | 206 | Service and execution binding setup |
| Parallel plan construction, within Tick update | 54 | Private chunk rows/bindings and entity boundaries |
| System span discovery, within Tick update | 23 | Finding repeated eligible sequences |
| Attachment destruction/index maintenance | 379 | Ordered attachment compaction and indexing |
| Per-instance clock propagation | 237 | Writing simulation time across the attachment population |
| Pending activation pump | 1 | Not a dominant cost in this workload |

The two preparation helpers alone account for 36% of Tick-update samples.
Parallel span execution accounts for another 32%, including its serial checks
and join. This explains why faster worker bodies produce a modest improvement
in the entire behavior bucket. Compilation already removed interpreter work;
the remaining opportunities include reducing repeated population-wide work.

Recommended next work, in order:

1. Keep prepared Tick schedules and attachment indexes incremental across
   spawn/death. Cache immutable function dependency decisions per generation.
   Preserve the existing ordering and hot-apply guards.
2. Reduce full-population clock writes and validate stable row groups at a
   coarser boundary where generation/liveness guarantees permit it.
3. Reduce redundant contact enumeration and dispatch. Measure candidate density
   and event demand before changing collection semantics. Evaluate the existing
   simplified physics option for scenes that do not need rigid-body solving.
4. Continue with serial rendering services only after the larger runtime costs.
   Surface influence collection and visual-motion component lookups appear in
   the sample; particles and the pending activation pump barely appear.

No new optimization was applied for this diagnosis. The evidence is in
[the diagnostic log and sample counts](remaining-runtime-profile.json).

Reproduce the workload with `CBLUEPRINT_BENCH_COMBAT=1`,
`CBLUEPRINT_BENCH_PHYSICS_PROFILE=1`, and
`CBLUEPRINT_BENCH_REQUIRE_PARALLEL=1`, running
`build-performance/test_suite --bench-arena-scene-scale 20000 1800 0`.
On macOS, attach `/usr/bin/sample PID 10 1 -file sample.txt` during steady play.
