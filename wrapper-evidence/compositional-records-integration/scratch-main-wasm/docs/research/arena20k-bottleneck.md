# Arena3D at 20,000 enemies: bottleneck diagnosis

Measured September 13, 2026. The largest identified avoidable cost is CPU
surface-influence selection. It repeatedly scans 64 retained samples for each
candidate, even when that candidate will be discarded. Every Arena enemy owns
a surface influence. A bounded output budget currently does not bound the
collection cost.

## Controlled comparison

Three alternating-order pairs, Release/O3 without sanitizers, 1280x720,
20,000 initial enemies, combat and restart, 240 measured frames after 30 warmups.
Behavior profiling is off. Six existing physics phase timers are enabled
independently in both variants. Medians of the three trial medians:

| Phase | Normal, ms | Surface influences disabled, ms |
| --- | ---: | ---: |
| Frame | 39.081 | 27.612 |
| Behaviors | 5.819 | 5.981 |
| Physics | 10.370 | 11.123 |
| Other systems | 0.993 | 1.018 |
| Rendering host work | 22.100 | 9.145 |
| Present | 0.063 | 0.065 |

The reciprocal frame rates are 25.6 and 36.2 FPS. Removing the effect cuts
median frame time by 29.3%. The rendering phase falls by 13.0 ms; independent
phase medians do not add exactly to the frame median. The normal frame medians
range from 38.55 to 39.41 ms; disabled medians range from 26.97 to 27.87 ms.

The temporary executable skips `render_influences_collect` and binds zero
influences. This removes both collection and the visual effect, so this is an
ablation, not a semantics-preserving optimization or a promised speedup for a
future fix. The original source and benchmark executable were restored.

All six runs have identical position checksums, per-frame combat trace hashes,
health, experience, kills, and prepared/system Tick counts. Each checks SLJIT
execution, zero interpreter calls, and successful restart. These checks establish
simulation parity, not visual parity. Peak submitted geometry is 20,102
instances, not 20,102 separate GPU draw calls.

## CPU attribution

An eight-second native sample of the unmodified release executable collected
6,576 main-thread samples during a separate 1,200-frame combat run:

- `render_influences_collect`: 28.4% of main-thread samples.
- `render_shader_parameters`: 5.2%.
- `render_programs_prepare`: 3.0%, including ramp metadata scans at 2.7%.
- `render_arena_visuals`: 3.7%.

These are inclusive samples; the ramp percentage is already included in program
preparation. The longer run reaches a different contact density, and the sampler
affects timing. Use the controlled comparison for elapsed frame measurements.
The sample establishes substantial CPU work, while Present is negligible in the
comparison. It does not measure GPU duration or establish the absence of every
GPU stall elsewhere in the frame.

In `src/render/arena_influence.c`, `retain` first searches the retained samples
for a matching entity, then searches them again for the weakest score. The
second loop recalculates color and fade scores for both sides of each comparison.
At a full 64-sample budget and 20,000 candidates, the worst case is about 1.28
million identity comparisons and 2.56 million score evaluations per frame.
The selection has no camera visibility input.

The other render costs also contain repeated work. `render_shader_parameters`
parses the component's JSON on each call, including the enemy's unchanged `{}`,
and scans parameter overrides. `submit` calls `render_programs_prepare` for each
instance, which scans shader graph ramp metadata before checking the program
revision. `render_arena_draw` collects every matching geometry entity without
camera-frustum rejection before material preparation and submission.

## Physics attribution

The median of each normal trial's mean phase time is:

| Physics phase | ms |
| --- | ---: |
| ECS synchronization | 0.537 |
| Box2D world step | 4.630 |
| Pose and swept-tree updates | 1.425 |
| Gameplay contact collection | 2.840 |
| ECS writeback | 0.162 |
| Pair sorting, comparison and event delivery | 1.457 |

These are phase means, not the 10.37 ms physics median above. The default pool
already uses four workers for Box2D and contact collection. Time under EnkiTS
`WaitforTask` can include the calling thread doing useful task work.

`collect_body` performs current-pose Box2D AABB queries and exact distance checks
in addition to consuming solid contact data. Continuous sensors require another
tree covering both motion endpoints. The last measured frame has 19,998 bodies
and 84,290 candidate pair records before sorting/deduplication. That pair count
is neither unique contacts nor newly delivered events.

These paths preserve current-pose, zero-delta, non-dynamic overlap and swept
sensor semantics. They cannot all be replaced blindly with Box2D's buffered
events. See [the contact contract](physics-queries.md). The profile supports
reducing redundant queries and sorting, and narrowing swept-index work where
the collision masks and motion contract permit it.

## Next work

1. Preserve surface-influence selection and trail behavior while caching scores,
   maintaining the weakest retained sample, and avoiding full retained-set
   searches for rejected candidates. Test stable ties, lingering samples and
   entity reuse. Measure the original combat scene with the effect enabled.
2. Cull geometry before material preparation and cache parsed material values
   and shader metadata by their existing change/revision boundaries. Keep visual
   bounds conservative for animated and shader-driven shapes.
3. Reduce contact bookkeeping while retaining its tested event timing and
   continuous-sensor behavior. Re-measure the physics phases separately.

The dominant hotspots above already execute as native C. They require fewer
repeated operations, independent of behavior JIT or AoT selection. Even removing
the entire roughly 5.8 ms behavior phase would leave about 33 ms of the current
frame budget. This is an approximate budget calculation, not a measured variant.

Raw trial output, hashes, diagnostic changes and compact CPU attribution:
[arena20k-bottleneck-results.json](arena20k-bottleneck-results.json).
The earlier [Tick audit](tick-sequences.md) explains generated-C comparisons
and the precise denominator of generated-loop coverage.
