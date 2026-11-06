# Reuse solved contacts during gameplay pair collection

## Scope

The common source baseline is `38118fd2cdb6d45c18d31c79c5a2094c19d42e36`.
This slice changes the shared physics contact collector and its tests. It has no
enemy prototype, collider shape, mask dimensions, or game-rule condition.
Behavior compiler work remains in the coordinating compiler chat.

In the earlier 20,000-enemy CPU profiles, gameplay pair collection averaged
5.262 ms natively and 21.180 ms in serial WASM. Those measurements precede the
common baseline and identify a candidate; they are not a before/after comparison.
See [the workload results](physics-material-benchmarks.md).

## Change and invariants

Previously, persistent solid contacts could receive an exact current-pose
distance check through the Box2D spatial index, then be appended again through
the solved-contact list. Compound colliders could also append one solved pair
per touching polygon. Sorting removed these duplicates afterward.

The collector now accepts solved solid contacts first and seeds its existing
per-worker visited scratch with the other body slot. Repeated polygon manifolds
and overlap callbacks for those accepted pairs need no new pair or distance
check. Spatial queries still check every remaining candidate.

- Only positive-delta, nonsensor, nonswept bodies reuse solved contacts.
- The lower entity slot still owns each ordinary pair. Bilateral categories,
  masks, ignored entities, and continuous-sensor exclusions are preserved.
- Speculative solid contacts with small positive separation remain active.
  Zero-delta synchronization still requires exact overlap.
- Sensors, relative-motion sweeps, nearest blockers, and pierced fragments
  keep their existing paths. Missed compound polygons do not mark a pair visited.
- Pair IDs, representative midpoint, fraction, canonical ordering, and contact
  lifetime are unchanged. This Box2D version interpolates the midpoint as
  `0.5 * a + 0.5 * b`; strict contraction keeps shape-order reversal equivalent.

The profiler's `physics_pairs` is a raw count before sorting/deduplication. It can
fall without changing the canonical contact set. Compare canonical contact/event
hashes and gameplay state separately from that diagnostic work count.

The optimization applies to circles, boxes, and compound destructible masks,
including terrain and fragments. It reduces redundant contact collection; it
does not alter Box2D stepping, geometry rebuilding, damage, or rendering.

## Validation fixtures

The existing wall, lifetime, filter, sweep, query, removal, and reset cases remain.
Two independent fixtures extend the serial and padded parallel physics suite:

- A connected notched slab, circular probe, and wide bar exercise compound
  misses followed by hits, multiple touching polygons, sensors, persistent solid
  contacts, reset/re-entry, and canonical event delivery. No combat rules are used.
- A dynamic/static circle pair with a positive gap exercises speculative contact
  lifetime. A zero-delta resynchronization removes that pair; a subsequent actual
  overlap starts a new contact.

Complete baseline/candidate replays cover the four authored stress workloads on
native, serial WASM, and worker WASM. Native initial/final inspection comparisons
cover material state and the full population counters. Replay hash parts include
canonical physics contacts and event payloads at the endpoint. These checks do
not claim a full per-frame trajectory trace. Inspection returns up to 256 entity
rows, alongside whole-world population and material counters.

The full native `make test` passes, including canonical asset validation, along
with focused ASAN/UBSAN physics/combat tests and the Release backend smoke test.
Both `make web-serial` and `make web-threads` app/backend builds pass; worker
validation runs the physics fixtures with four workers. The source and executable
fingerprints in [the retained results](contact-collection-results.json) identify
the exact candidate tested.

## Matched CPU measurements

The accepted group uses three alternating baseline/candidate pairs per workload
and platform on an Apple M4 Pro running macOS 26.7. Each fresh controller process
replays the same prepared project and input timeline: 120 warmup frames for the
crowd or 60 for the other cases, then 240 measured frames. All 82 profiles across
the initial and confirmation groups match independent replay hashes, retain all
240 measured steps, and drop zero simulation time. Project and executable
fingerprints remain unchanged.

All six pinned builds use Release `-O3 -DNDEBUG`, with performance tests disabled.
Native timing builds disable sanitizers. Three WASM caches retain the native
sanitizer option as `ON`; CMake's sanitizer block excludes Emscripten, so that
option adds no web sanitizer flags. Field SIMD stays enabled in their CMake policy;
its web gate requires threading. Serial WASM therefore means the serial runtime,
rather than a separate SIMD-off experiment. Worker WASM uses four workers.

For the 20,000-body destructible crowd, each value below is the median of three
run percentiles or phase means. Units are milliseconds.

| Target | Pair collection mean, before → after | Contact dispatch mean, before → after | CPU frame p95, before → after |
| --- | ---: | ---: | ---: |
| Native | 1.917 → 1.695 (11.6% lower) | 0.591 → 0.336 (43.1% lower) | 19.894 → 15.828 (20.4% lower) |
| Serial WASM | 16.505 → 14.327 (13.2% lower) | 0.679 → 0.394 (42.0% lower) | 85.849 → 80.548 (6.2% lower) |
| Worker WASM | 4.627 → 3.927 (15.1% lower) | 0.673 → 0.383 (43.1% lower) | 54.686 → 51.946 (5.0% lower) |

All three native crowd pairs improve CPU frame p95. The third serial WASM pair
has a worse frame tail, 80.766 → 92.573 ms, despite lower mean pair cost. The third
worker WASM pair also regresses, 52.810 → 82.515 ms, with slower pair collection
and Box2D/other phases. The medians establish a crowd benefit; they do not establish
uniformly consistent browser frame tails.

Parallel `contact_collection` is zero because the collector runs on workers.
`contact_join` captures the main thread's wait, including collection: its crowd
median is 1.885 → 1.672 ms native and 4.595 → 3.905 ms worker WASM. Serial collection
is 16.504 → 14.325 ms. These are nested within the pair phase, not extra savings
to add to it.

The other workloads have mixed or higher initial CPU frame p95:

| Workload | Native, before → after | Serial WASM, before → after | Worker WASM, before → after |
| --- | ---: | ---: | ---: |
| Terrain barrage | 0.942 → 1.006 | 2.238 → 2.646 | 2.242 → 2.871 |
| Destructible enemy barrage | 0.778 → 0.910 | 1.888 → 2.254 | 2.093 → 1.699 |
| Fog/liquid | 1.237 → 1.614 | 3.395 → 3.830 | 2.236 → 4.142 |

The initial worker fog increase appears in density/relaxation; contact collection
averages less than 0.002 ms in both versions. Five additional alternating pairs
with unchanged assets/binaries do not reproduce the large tail: median p95 is
1.455104 → 1.461504 ms (0.4% higher), fluid mean 1.252605 → 1.240942 ms, and pair
means overlap from roughly 0.0006 to 0.0009 ms. Both groups remain in the evidence.
This establishes no reproducible worker-fog contact regression or fog speedup.
It does not identify the cause of the original variation or confirm the other
control cases, which have no separate follow-up group.

These controller timings exclude rendering and GPU completion. No all-scene,
rendered-frame, or 60 FPS improvement is claimed. Allocation request counts and
bytes also remain unchanged in the crowd; scratch/allocation work remains open.

The results JSON retains all per-run frame percentiles, physics/fluid/Box2D
phase means, worst-frame population/contact counts, replay hashes, build policy,
source/binary/project fingerprints, and validation log fingerprints. Full raw
profiles, prepared projects, requests, replays, logs, and the comparison drivers
remain in `/private/tmp/chibi-contact-reuse` as local temporary artifacts.

## Terrain and further work

Terrain, destructible enemies, and fragments all use this shared collector.
The benefit depends on persistent solid contacts and compound-polygon duplicates,
rather than an enemy shape. Swept projectile impacts still require their exact
checks, and this change leaves destruction body/shape rebuilds and field/material
processing untouched. The barrage measurements do not establish a terrain
speedup.

For destruction, profile body recreation after mask revisions, then test updating
only changed collision shapes while preserving velocity, mass, contact lifetime,
and deterministic event order. Validate disconnected masks, arbitrary contours,
fragments, and independent terrain/enemy fixtures. The other measured candidates
are retaining allocation/scratch capacity, skipping unneeded fluid-proxy preparation,
and improving contiguous density/relaxation data. See the
[workload priorities](physics-material-benchmarks.md#measured-next-optimizations).
