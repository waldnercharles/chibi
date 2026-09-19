# Preserve attachment indexes during entity removal

September 19, 2026. Baseline `f315d46c` already includes shared clocks,
incremental Tick preparation, bound component columns, parallel execution and
batched swept-target updates. This work follows the
[slow-frame investigation](swept-targets.md).

## Where contact time goes

The contact-event phase includes both event dispatch and lifecycle flushes after
each batch. A 20,000-enemy, 1,800-frame Tracy capture separates those operations.
The slowest 5% of contact-event frames average 6.116 ms in that phase, including
5.593 ms in lifecycle processing and 0.515 ms dispatching events. These frames
were selected by contact duration, not whole-frame duration.

Across complete engine frames, the contact pump averages 0.746 ms, including
0.541 ms lifecycle and 0.205 ms dispatch. Physics pair sorting averages 1.339 ms;
the following pair comparison and event queueing averages 0.228 ms. Event payload
allocation and faster compiled event calls are smaller targets in this workload
than attachment maintenance and contact enumeration.

## Change

Removing an entity used to copy every surviving behavior attachment, reset all
pending-work bits and rebuild every entity/asset attachment index. Deleting a
recently spawned projectile repeated this work across the whole crowd.

Removal now preserves the prefix before the first deleted attachment. It removes
the old suffix from entity indexes and asset counts, compacts that suffix in
order, truncates only its pending membership, then indexes the surviving suffix.
Unchanged prefix instances, pending work and indexes remain intact. The existing
attachment revision still invalidates the affected portion of the prepared Tick
schedule. This applies to individual, asset-specific and bulk removal.

This is not constant-time removal in every case. Deleting an early attachment
still moves the later ones to preserve broadcast order, and the selection bitset
still scales with attachment capacity. A different ordered-storage design would
be a separate change.

The follow-up Tracy capture has the same combat trace and checksum. Contact-event
P95 falls from 3.366 to 0.912 ms, about 73%. Across complete engine frames, contact
lifecycle work falls from 0.541 to 0.142 ms on average; dispatch changes from
0.205 to 0.186 ms. In each capture's separately selected slowest contact frames,
lifecycle time is 5.593 versus 1.881 ms. The new detach scope averages 1.029 ms
within the latter set. These are instrumented CPU wall times, not a frame-rate
guarantee. [Capture metadata, scope statistics and logs](contact-profile.json).

Optional Tracy zones distinguish contact-batch dispatch/lifecycle, physics pair
sorting/comparison, and behavior detachment. The scene benchmark now accepts
`--physics-profile` to retain coarse physics timing while keeping ordinary
parallel behavior execution enabled.

## Scene measurements

Three alternating Release pairs use 20,000 initial enemies, 1,800 combat frames,
1280x720 rendering and eight workers. Both executables have sanitizers and
detailed behavior inspection disabled; coarse physics timers are enabled.
Snapshots, full combat traces, worker execution and restart checks match within
all pairs. These fixtures retain the normal simulation workload.

| Pair | Behavior/runtime P50 before / after | Behavior/runtime P95 before / after | Frame P50 before / after | Frame P95 before / after |
| --- | ---: | ---: | ---: | ---: |
| 1 | 1.994 / 1.245 ms | 9.590 / 4.180 ms | 22.455 / 15.839 ms | 37.877 / 23.154 ms |
| 2, after first | 1.545 / 1.392 ms | 6.579 / 4.359 ms | 16.441 / 17.914 ms | 23.363 / 24.591 ms |
| 3 | 1.509 / 1.224 ms | 6.389 / 3.997 ms | 15.290 / 14.766 ms | 21.492 / 21.168 ms |

Behavior/runtime P50 and P95 improve in every pair. Medians across the trials
are 1.545 to 1.245 ms P50 and 6.579 to 4.180 ms P95, about 19% and 36% lower.
This bucket includes contact lifecycle work, not just authored Tick bodies.

Whole-frame medians across trials are 16.441 to 15.839 ms P50 and 23.363 to
23.154 ms P95. The second pair regresses overall despite lower behavior cost.
Physics timing varies substantially, including the unchanged Box2D solve.
The phase improvement is consistent; a reliable overall frame-time reduction
remains unproven. [All scene samples and hashes](contact-scene-results.json).

The existing full-population destruction fixture is a separate regression check.
Removing 20,000 entities with 100,000 idle attachments measures 7.044 to 7.232 ms
in the three-trial medians. Paired differences are -0.143, -0.072 and +0.188 ms,
so this case is effectively flat in the sample. This fixture has no physics or
rendering. [All destruction samples](contact-destroy-results.json).

Reproduce with saved optimized executables:

```sh
python3 tools/bench_arena_scene_scale.py --before build-release/test_suite-before-contact-work --after build-release/test_suite --counts 20000 --frames 1800 --trials 3 --combat --physics-profile --expect-parallel --expect-after-workers 8 --output /tmp/contact-scene.json
python3 tools/bench_behavior_destroy.py --before build-release/test_suite-before-contact-work --after build-release/test_suite --counts 20000 --trials 3 --output /tmp/contact-destroy.json
```

## Component prebinding and the C comparison

The [prepared column binding](bound-columns.md) is already shared by SLJIT,
bytecode and generated C. It resolves each required component type outside the
entity loop. Compiled code still validates entity generation, component presence
and layout before computing `column.data + slot * stride`.

The independent C fixture retains final component pointers across the whole run
and assumes inactive feedback. Its last repeated 20k comparison was 0.218 ms
versus 0.344 ms for our native update, with median per-trial ratio 1.557. At 1k,
the recorded ratio was 3.182. The native path uses workers while the independent
C loop is serial. The 0.126 ms difference at 20k is an absolute latency budget,
not evidence that the relative or per-core gap has disappeared. These are the
[previous comparison's results](query-snapshots.md), not new measurements of
this lifecycle change.

Caching final owner pointers is feasible, but it needs entity-generation and
component-presence guards plus invalidation for storage growth, schema migration,
service replacement and hot apply. Within proven synchronous spans, storage
already stays stable. Hoisting redundant owner/layout checks to those boundaries
is a narrower next compiler experiment. Adding a per-row pointer table also costs
memory and preparation work, so it needs its own comparison rather than an assumed
speedup. This change adds no owner-pointer cache or content-specific fast path.

## Validation

The pending-work fixture covers removal before and on 64-bit word boundaries,
moving pending survivors, deleting the final attachment, and retaining earlier
waits. It uses shared behavior assets without Arena mechanics. Existing cases
cover mixed assets, ordered broadcasts, duplicate/stale destruction requests,
pooled reuse, callbacks and Tick repair across native, bytecode and oracle modes.

The full Debug `make test` run passed with ASan and UBSan. Release pending-work
checks passed for SLJIT, bytecode and oracle, as did the native runtime smoke
test and WebAssembly `check-behavior-backend` target. The Tracy build, formatting
and diff checks also passed.
