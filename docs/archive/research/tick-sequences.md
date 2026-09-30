# Repeated interleaved Tick systems

[Parallel Tick execution and rendering preparation](parallel-scene-work.md)
now extend this work. The threading descriptions below record the earlier stage.

The scheduler now automatically compiles repeated sequences of different Tick
bodies into a single ordered native loop. A schedule such as `A B A B A B`
executes embedded bodies `A; B` in each iteration. No separate behavior-body ABI
call is needed between them. This extends the earlier
[homogeneous system loops](tick-system-loops.md) to the common case of multiple
Tick behaviors attached to each prototype.

Execution order is identical to the attachment schedule, including dependencies
between entities, random calls, component writes and failures. This makes the
optimization safe without proving that different behaviors commute. It does
not sort attachments into separate all-A/all-B passes. Shared compiler lowering
still supplies each body's arithmetic, calls, components, containers and source
maps; no scene-specific implementation or graph interpretation is introduced.

## Compilation and scheduling

The compiler lays out each lowered body, reserves one compatible native frame,
and links successful exits directly to the next body's guard. All bodies agree
on the scratch-register budget so the combined frame fits the target's register
file. A sequence containing only bodies that need no saved context keeps its
loop state in saved registers. Sequences needing those registers across helpers
share four stack slots. Each body reuses the remaining frame space.

Schedule rebuilding finds the shortest repeated prefix of up to eight bodies,
then extends it across matching rows. It stops at ineligible entries. The search
limit bounds planning work and generated code growth; longer or nonrepeating
stretches retain their prepared compiled execution. Sequence kernels are cached
across attachment churn by function, asset identity and immutable program
revision. Rebuilding frees unused kernels. This avoids compiling again when
spawning or destroying entities leaves the same sequence in the schedule.

Per-row guards still validate entity generations, instance liveness, backend,
asset, services, profiling, tracing and active activations. Guarded rows use the
existing compiled activation path. After an interruption in the middle of a
sequence, the prepared loop finishes the remaining prefix before entering the
next aligned iteration. A failed body retains its partial effects and source
error exactly once. A span may end partway through a sequence.

`game_behavior_last_update().system_loop` counts actual entries attempted in
both homogeneous and mixed system kernels. `tick_system_compiled` continues to
report per-function availability. Portable bytecode uses the existing ordered
prepared VM loop; native execution never falls back to the interpreter.

## Native and generated C comparison

`behavior_function_write_sequence_c` emits all of the same lowered bodies and
an ordered C loop with direct calls in one translation unit. This benefits C
export work too, but the complete standalone Scene AoT loading/export pipeline
is still separate unfinished work.

Five rotated trials, 256 measured iterations after eight warmups, Release/O3
without sanitizers or profiling. Times below are nanoseconds per entity for the
entire sequence. The prepared reference uses the existing native row dispatcher
and a separate native body call for each row. Compilation and schedule rebuilding
are excluded from execution time and recorded separately.

| Sequence | Entities | Prepared native | Embedded SLJIT | Generated C |
| --- | ---: | ---: | ---: | ---: |
| Two component graphs | 512 | 18.394 | 17.807 | 17.952 |
| Two component graphs | 4,096 | 18.957 | 17.702 | 17.912 |
| Two component graphs + string-array graph | 512 | 383.621 | 385.452 | 377.075 |
| Two component graphs + string-array graph | 4,096 | 390.299 | 396.882 | 382.172 |

The component sequence takes about 3–7% less time. The managed sequence has no
reliable improvement: medians increase by 0.5–1.7% with overlapping trial ranges,
and copying dominates. SLJIT is within 4% of generated C for all four cases.
All checksums match across prepared native, embedded native, generated C and
bytecode. Warmed execution allocates no VM scratch, runtime stores or arena
backing blocks, and calls neither interpreter entry point.

At 4,096 entities, cached span rebuilding takes 9.7–12.8 microseconds. Initial
planning plus native compilation takes 57–118 microseconds for these fixtures.
Those are workload-dependent measurements; compiling larger bodies can cost
more. The benchmark asserts that rebuilding retains the existing kernel.

Two measured revisions preceded these results: forcing all sequences to spill
loop state regressed the component fixture, and searching additional matching
periods repeatedly scanned Arena's enemy rows. Keeping registers for eligible
sequences and extending only the first matching period removed those costs.

Raw samples and executable/compiler/generated-source hashes:
[tick-sequence-results.json](tick-sequence-results.json).

```sh
python3 tools/bench_tick_sequences.py --entities 512 4096 --frames 256 --trials 5 \
  --output docs/research/tick-sequence-results.json
```

## Arena at 1,000–20,000 enemies

Three paired runs per population, 240 measured frames after 30 warmups, rendered
at 1280×720 with profiling off. Each run fires at enemies, checks the combat
trace and final component state, and restarts the Scene. The baseline is the
previous homogeneous-loop implementation, including query and container work.
Median phase times in milliseconds:

| Enemies | Behavior before | Behavior after | Frame before | Frame after |
| --- | ---: | ---: | ---: | ---: |
| 1,000 | 0.300 | 0.302 | 8.275 | 8.279 |
| 5,000 | 1.356 | 1.380 | 10.034 | 9.945 |
| 20,000 | 5.775 | 5.847 | 38.907 | 39.262 |

There is no reliable Arena speedup from this change. Behavior medians increase
by approximately 0.7–1.8%; trial ranges overlap. An earlier three-trial run
suggested 2–6% lower behavior time at 1,000/5,000, which the final run did not
confirm. The isolated component-kernel gain should not be presented as a
whole-scene gain.

Actual system-loop entries over each measured run:

| Enemies | Prepared Tick entries | System entries before | System entries after |
| --- | ---: | ---: | ---: |
| 1,000 | 478,686 | 480 | 477,966 |
| 5,000 | 2,398,716 | 480 | 2,397,996 |
| 20,000 | 9,598,836 | 480 | 9,598,116 |

Over 99.8% of prepared Tick entries now use generated loops. The remaining
720 prepared entries per run average three per frame. Covered entries no longer
make separate behavior-body calls, but the net saving is small relative to the
measured behavior phase.

At 20,000 enemies, the after run's median rendering-host phase is 21.87 ms,
physics is 10.30 ms, and behaviors are 5.85 ms. These measurements point toward
rendering and physics as the larger remaining frame costs. This scenario does
not meet a 60 Hz frame budget. Both executables briefly queue up to 62 events
on the same 16 frames, then fully drain the queue. All position checksums and
per-frame combat traces match, no behavior failures are retained, no interpreter
calls occur, and restart succeeds at every population.

Raw phase distributions, combat checks, backlog observations and executable
hashes: [tick-sequence-scene-results.json](tick-sequence-scene-results.json).

```sh
python3 tools/bench_arena_scene_scale.py \
  --before build-performance/test_suite-before-interleaved \
  --counts 1000 5000 20000 --frames 240 --trials 3 --combat \
  --output docs/research/tick-sequence-scene-results.json
```

## Which Ticks remain outside system loops

A read-only debugger inspection confirms the three prepared singleton entries
are `TwinStickInputBehavior`, `PlayerContactBehavior`, and
`ArenaShipMotionBehavior`. Each has a native system kernel, but their position
in the attachment schedule does not form a repeated sequence. Their ordinary
prepared native dispatch still calls the compiled body directly.

The 99.8% figure uses **prepared Tick executions** as its denominator. It is
not the percentage of all Tick graphs or all scheduled Tick executions. The
published Arena catalog also contains these Tick bodies that require compiled
activations:

- `ArenaWaveBehavior`, `TwinStickWeaponBehavior`, and `SpawnTelegraphBehavior`
  contain spawning/host-command suspension paths.
- `ProjectileBehavior`, `ArenaShardBehavior`, `SurfacePulseBehavior`, and
  `WavePulseBehavior` contain entity-destruction commands.
- `ArenaPickupBehavior` contains entity queries and commands, including
  destruction.

Those whole Tick bodies are excluded by the current batch-operation rules even
on frames where a structural branch is not taken. They still execute compiled
SLJIT code on native platforms. At the inspected combat schedule, 1,999 rows
were assigned to system spans, three were prepared singletons, and 17 needed
compiled activations (wave, weapon, one pickup, 12 shards and two surface
pulses). This snapshot is not a complete-run execution percentage.

The schedule and published program flags are recorded in
[tick-coverage-audit.json](tick-coverage-audit.json). The debugger run supplies
route identification only; all timing tables above use optimized executables
without the debugger.

## Verification

`make build`, the complete `make test` suite, and `make web` pass. The web build
executes its portable bytecode smoke tests with SLJIT absent. The separate
native no-JIT `check-behavior-backend` target reports `UNAVAILABLE` and verifies
that both behavior publication/execution and runtime selection reject
interpreter fallback. Native regression checks run with ASan/UBSan; the
performance executables use a separate Release build without sanitizers.

New regressions check three distinct interleaved bodies with cross-entity
component dependencies and managed string arrays across SLJIT, bytecode and the
explicit oracle. They exercise mid-sequence debugger holds, a live wait and
cancellation, and hot apply that changes a body's result after freeing the old
generation. Direct native sequence tests verify a failure in the second body,
partial effects exactly once, source failure retention, rejection of a wrong
starting body, a span ending partway through its cycle, and budget failure.
The existing homogeneous, scheduler, managed-value and debugger tests still
pass. Formatting checks, Python compilation and `git diff --check` pass.
