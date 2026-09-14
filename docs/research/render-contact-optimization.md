# Rendering and contact costs at scale

The September 13, 2026 changes preserve surface influences and Box2D gameplay.
Three alternating-order pairs at each population use Release/O3, 1280x720,
30 warmups and 240 measured combat frames. Behavior and physics profiling are
disabled in the paired comparison.

| Initial enemies | Frame before, ms | After, ms | Equivalent FPS before / after | Render before / after, ms | Physics before / after, ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 8.345 | 8.405 | 120 / 119 | 1.548 / 0.683 | 0.744 / 0.716 |
| 5,000 | 10.363 | 8.361 | 96 / 120 | 5.668 / 1.264 | 2.840 / 2.471 |
| 20,000 | 39.665 | 20.582 | 25 / 49 | 22.383 / 3.605 | 10.173 / 9.423 |

These are medians of three trial medians; FPS is their reciprocal. The small
population remains presentation limited. At 20,000, frame time falls 48.1%,
rendering host work 83.9%, and physics 7.4%. Frame p95 falls from 52.24 to
31.87 ms. This remains above a 60 Hz budget, particularly in crowded contacts.
Behavior time is 5.70 ms after the change; this slice changes no behavior code.

All 18 runs match their population's final position checksum and per-frame
combat trace. They verify compiled execution without interpreter calls,
successful kills/experience, valid behavior instances and restart. Surface
influences remain enabled. Raw output and executable hashes are in
[render-contact-scene-results.json](render-contact-scene-results.json).

## Changes

- Surface influence selection caches remaining-strength scores and the weakest
  retained entry. Rejected candidates no longer recalculate every retained
  score. Updating a retained sample invalidates the weakest-entry cache;
  beginning a frame recalculates decay. Existing stable tie order and anchored
  trail behavior remain intact.
- Geometry uses conservative homogeneous clip-plane tests before material
  parameter preparation and submission. Bounds include part transforms and
  scale, and remain conservative across the eye plane. The built-in vertex
  shader does not displace vertices. Offscreen entities still simulate, emit
  particles and contribute lights and influences. Invalid material references
  are still diagnosed before culling.
- Each shader asset caches its last parsed parameter JSON by owned string
  content. Per-entity runtime overrides are applied afterward, so the cache
  cannot leak overrides across entities. Asset replacement destroys the cache.
  Different parameter strings invalidate it; this bounded cache does not claim
  to eliminate parsing in a sequence of entirely distinct material instances.
- Program ramp capacity checks cache results by shader and ramp revisions.
  Hot edits and capacity errors retain their existing diagnostics.
- Contact pairs use stable radix sorting for larger arrays, skipping constant
  handle bytes. Small arrays retain comparison sorting. The final unsigned
  entity-pair order, deduplication and event delivery remain unchanged.
- The continuous-sensor tree retains conservative motion slack before moving
  a proxy again. Exact casts still determine collisions. Box2D's unused buffered
  sensor/contact event streams are disabled; engine gameplay contacts still use
  the existing current-pose and relative-motion paths.

A separate phase-timed run measured contact sorting/comparison/delivery at
0.58 ms versus the earlier 1.46 ms attribution. The swept-tree phase remained
about 1.42 ms in this short-lived-projectile workload, so no measured gain is
claimed there. Box2D's step averaged 4.53 ms and contact collection 2.79 ms.
Those are phase means from a separate run, not additive median frame values.
See [render-contact-phase-results.json](render-contact-phase-results.json).

## Cute Framework batching

The production scene renderer uses `cf_draw3d_mesh` and built-in 3D submissions,
which automatically coalesce compatible instances. It does not record a
`CF_DrawList`. The dependency actually linked here is `/tmp/cblueprints-tick-cute`,
whose `cute_draw3d.cpp::s_submit` supplies the batching path.

The phase-timed run averaged 1,467.8 submitted instances in 7.26 Cute 3D commands
per frame. Command reasons averaged 1 first command, 5.74 mesh changes and 0.52
uniform changes. Culling reduced peak instances from 20,102 to 1,707. The old
benchmark field named `draws` counts instances; the new `scene_scale_render`
record explicitly reports both instances and commands, followed by split reasons.

Batching is already effective at reducing GPU commands. It does not remove CPU
work performed before submission. A baked draw list can help a static level,
but moving enemies require fresh instance transforms. Replaying a separate
baked list per enemy would not automatically combine all their instance buffers.

## Validation

Added CPU cases cover influence capacity ties, refreshing/replacing the weakest
sample, decay, and clip bounds crossing edges or the eye. Material GPU coverage
now changes JSON in place and switches entities to check cache invalidation and
override isolation. Existing GPU tests cover surface effects, different material
paths, instance batching, live image/parameter/ramp edits and failure retention.
The existing ordered contact regression exceeds the radix threshold, including
parallel collection and contact re-entry. Thin walls, sliding, moving-target
sensor casts, nearest hits, owner/category filters, zero-delta teleports,
destruction, reset and entity reuse remain covered.

`make build`, the full native sanitizer `make test` suite, the GPU
`--render-checks` suite, and `make web` passed. The web checks execute behavior
bytecode without SLJIT or interpreter calls. The release `--simulation` suite
also passed in an isolated run.

An earlier release `--simulation` run reported a timing assertion failure in
the existing nested profiler attribution test while an Arena benchmark was
running concurrently. Physics semantic cases passed in that run. No profiler
changes were made; the isolated rerun and full sanitizer suite passed.

Reproduce the scene measurements:

```sh
python3 tools/bench_arena_scene_scale.py \
  --before build-performance/test_suite-before-render-physics \
  --counts 1000 5000 20000 --frames 240 --trials 3 --combat \
  --expect-after-workers 4 --output docs/research/render-contact-scene-results.json
```

The [Neon-Swarm comparison](simplified-physics.md) evaluates the next physics
choice separately from these behavior-preserving changes.
