# Box2D tuning and worker selection

[Parallel Tick execution and rendering preparation](parallel-scene-work.md)
now extend this work. The threading descriptions below record the earlier stage.

September 13, 2026. Arena3D, 20,000 initial enemies, 1280x720, all surface
influences enabled, Release without sanitizers. Each configuration runs three
times in alternating order, with 30 warmup and 240 measured combat frames.
Physics phase profiling is enabled; behavior profiling is disabled. Values
below are medians of trial medians. This machine reports 10 performance cores
and 4 efficiency cores, without SMT. Worker budgets include the calling thread.

| Configuration | Physics ms | Frame ms | Same gameplay trace as baseline |
| --- | ---: | ---: | --- |
| 4 workers, 4 substeps, sleeping and CCD enabled | 9.246 | 20.161 | Yes |
| 8 workers | 6.805 | 17.875 | Yes |
| 10 workers | 7.263 | 18.236 | Yes |
| 14 workers | 7.083 | 18.133 | Yes |
| 2 substeps | 8.411 | 19.272 | No |
| 1 substep | 7.942 | 18.815 | No |
| Sleeping disabled | 9.493 | 20.331 | Yes |
| Solid CCD disabled | 9.528 | 20.605 | No |
| Contact frequency 15 Hz instead of 30 | 8.819 | 19.776 | No |
| Contact damping ratio 1 instead of 10 | 9.670 | 20.689 | No |
| Dynamic velocity driven with force | 9.259 | 20.218 | No |

All repetitions are deterministic within their configuration. Each run checks
finite state, combat outcomes, restart and zero interpreter calls. A different
trace means different positions and/or combat results, not a backend parity
claim. Tuning changes the subsequent workload as contacts and outcomes change.
Small timing differences here should not be treated as universal speedups.
Raw logs and executable hashes: [individual settings](physics-settings-results.json).

The clearest gain is eight workers, reducing physics time by about 26%. The
separate phase means show both the Box2D step improving, 4.71 to 3.69 ms, and
engine contact collection improving, 2.83 to 1.55 ms. Swept-sensor preparation
remains about 1.43 ms; it is not parallelized by the worker-count setting.

A second comparison combines workers and substeps:

| Configuration | Physics ms | Frame ms | Equivalent FPS | Frame p95 ms |
| --- | ---: | ---: | ---: | ---: |
| 4 workers / 4 substeps | 9.201 | 20.144 | 49.6 | 32.061 |
| 8 workers / 4 substeps | 6.853 | 17.738 | 56.4 | 28.755 |
| 8 workers / 2 substeps | 5.998 | 17.130 | 58.4 | 28.396 |
| 8 workers / 1 substep | 5.282 | 16.178 | 61.8 | 28.951 |

These tails still exceed a 60 Hz budget. One substep reaching 62 FPS on the
median does not establish a smooth 60 FPS scene. Raw data:
[combined settings](physics-settings-combined-results.json).

## Accuracy and motion

The wall-slide regression measures penetration of an eight-unit-radius body
held against a thin wall at 600 units/second. Four substeps penetrate 0.225
units, two penetrate 0.886 units, and one penetrates 2.758 units. All retain the
same two Contact Begin deliveries and slide along the wall. One substep also
fails the existing crowd-separation tolerance. Four retains its original
tolerances; lower settings are explicit quality choices, not equivalent modes.

The CCD regression deliberately fires a solid body through a thin wall at
6,000 units/second. Disabling solid CCD permits tunneling. Existing authored
continuous sensor sweeps remain independent of the Box2D world flag. This is
why the setting is labeled **Solid continuous collision** in the editor.
The sampled Arena workload did not benefit from turning it off.

A final comparison also disabled the existing authored Body `continuous` flag
on sensor prototypes, separately from Box2D's solid CCD. At four workers this
reduced median physics time from 9.559 to 8.670 ms and frame time from 20.502 to
19.562 ms. Sweep preparation fell from 1.441 to 0.098 ms. The sampled gameplay
trace matched, but this is not proof that discrete detection covers fast,
grazing or moving-target contacts. The override is confined to the benchmark;
production sensor prototypes retain their continuous setting.

That same final run verified Auto selecting eight workers: 6.962 ms physics and
18.176 ms per frame, with the baseline gameplay trace. These are another set of
paired trials, not replacements mixed into the earlier tables. See
[Auto and sensor-sweep results](physics-settings-auto-sweeps-results.json).

The existing movement path sets velocity; it does not apply impulses. Body
linear/angular damping and shape friction/restitution are already zero, and
rotation is fixed. Reducing body damping cannot save work here. Contact damping
is a separate solver coefficient; reducing it does not reduce the number of
solver passes.

Optional force drive applies `mass * (target_velocity - current_velocity) / dt`
to dynamic bodies over a fixed tick. It changes acceleration and contact
response. Kinematic bodies still set velocity directly. Zero-delta sync sets
velocity without dividing by time or accumulating force. In an unobstructed
four-substep test, a target of 60 units/second moves 0.625 units on its first
force-driven tick versus 1 unit with direct velocity, ending at the same speed.
The measured force-drive option supplies no performance reason to change the
default. Sustained forces are nevertheless appropriate when acceleration is
part of the intended movement model. See Box2D's
[migration guide](https://box2d.org/documentation/md_migration.html) and
[world settings](https://box2d.org/documentation/group__world.html).
Drive forces are committed only after every body validates, so fixing an
invalid component and retrying a tick cannot accumulate the aborted force.
After that error-handling fix, a fresh three-trial comparison measured 9.092 ms
physics / 20.089 ms frame for direct velocity and 9.705 / 20.526 ms for force
drive, both at four workers. Each mode retained its original gameplay trace.
This still gives no performance reason to replace direct velocity; see the
[final force-drive comparison](physics-settings-final-force-results.json).

## Authoring and runtime

The Scene Inspector now has a **Scene Physics** section. Saved fields are:
`workers`, `substeps`, `sleep`, `continuous`, `force_drive`, `contact_hertz`, and
`contact_damping`. Changes are available while Scene Play and prototype preview
are stopped. Each committed control edit is undoable. Settings survive save,
load, tab changes, editor checkpoints, Play restart and native module reload.
Prototype previews inherit their open Scene's settings. The Scene JSON parser
also validates the settings for automation and asset tools.

The only changed automatic default is the worker budget. Auto uses up to eight
workers, capped by available hardware. On macOS it prefers the detected
performance-core count, falling back to the physical-core count on older Macs.
Other platforms use the task scheduler's hardware-thread count.
This selects eight on the measured machine. It does not pin threads to cores.
Manual budgets can use up to 32 available workers. Web execution remains serial
and reports that effective budget while retaining the authored choice.
Worlds with the same effective budget share a lazy EnkiTS pool. Worlds with
different budgets have separate pools. Fewer than 256 bodies run inline.

Four substeps, sleeping, solid CCD, direct velocity, 30 Hz contact frequency,
and contact damping ratio 10 remain the defaults. No scene automatically trades
collision quality for frame time. Existing authored scenes now save these fields
explicitly.

Changing workers requires an empty/reset physics world, because Box2D fixes its
task context at world creation. Runtime settings reject invalid values without
altering configuration. Editor changes reset the stopped physics world; normal
simulation resets retain its settings. Borrowed custom executors remain explicit
and cannot silently be replaced by a worker-budget edit.

## Threading outside physics

Production Arena uses worker threads for the Box2D solver and parallel contact
queries. ECS synchronization, swept-tree preparation, writeback and ordered
contact dispatch remain on the caller. A larger budget does not parallelize
these serial phases.

Compiled Tick batches and prepared system loops currently execute on the main
thread. The behavior activation pump supports a supplied parallel callback and
serial-effect checks, but production Arena does not install that callback.
Its current callers are tests and the separate seeded-scene benchmark.

Renderer preparation, Cute draw submission and ImGui also execute on the main
thread. GPU work is asynchronous; that is distinct from distributing renderer
CPU preparation over this worker pool. File watching and framework/driver
threads are not general behavior workers.

The next substantial scheduler change is to partition eligible compiled Tick
loops into chunks, using compiler read/write effects to reject conflicting
work, preserving deterministic random streams and merging structural effects
in order. Rendering could similarly prepare immutable instance data in worker
chunks while retaining Cute/ImGui submission on the main thread. Increasing the
physics worker setting alone does neither.

## Reproduce

```sh
python3 tools/bench_physics_settings.py \
  --cases default substeps_2 substeps_1 workers_8 workers_10 workers_14 \
          sleep_off solid_ccd_off contact_15hz damping_1 force_drive \
  --output docs/research/physics-settings-results.json
python3 tools/bench_physics_settings.py \
  --cases default workers_8 workers_8_substeps_2 workers_8_substeps_1 \
  --output docs/research/physics-settings-combined-results.json
```

`default` explicitly requests four workers to retain the pre-tuning baseline;
`auto` measures current automatic selection. Benchmark-only environment input
passes through the same validated settings API as editor-authored data.

## Validation

Native physics checks cover default and two-substep cases, one-substep wall
blocking and swept sensors, force-driven and kinematic motion, the solid CCD
tradeoff, invalid changes, pool sharing and reset retention. The default
four-substep contact quality assertions remain unchanged. Scene tests cover
JSON roundtrip, malformed fields, checkpoint restore and settings undo/redo.
The native UI test edits sleeping through the actual Inspector control and
undoes the change. `make build`, the full `make test` sanitizer suite with
`UBSAN_OPTIONS=halt_on_error=1`, and the full native `--ui-native` suite passed.
The final Inspector layout was captured and checked with the focused UI case.
`make web` passed its BYTECODE backend checks and reported one physics worker.

The first sanitizer run found a null iteration when the optional settings
object was absent. The parser now handles absence before entering yyjson's
iteration macro, and a regression covers that path. The native UI check also
caught an unsupported ImGui numeric-input flag; the final controls use the
supported input API. No such errors remain in the acceptance logs.
