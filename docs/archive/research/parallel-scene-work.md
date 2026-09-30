# Parallel Tick execution and rendering preparation

September 13, 2026. Native Scene execution now automatically uses the shared
EnkiTS pool for eligible compiled Tick spans and 3D geometry preparation, in
addition to Box2D and contact collection. Small jobs stay on the caller. The
existing Auto budget selects up to eight workers including the caller. Pools
with matching budgets are shared; these phases join before the next phase.
Emscripten remains serial and executes behavior bytecode.

## What executes concurrently

The compiler examines lowered instructions, not graph names. It proves Self
ownership for component and host targets, records other-entity component reads,
and rejects overlapping reads/writes across the functions in a candidate span.
Unknown host effects, structural operations, global RNG calls, string interning
into the ECS, and unproven targets keep the existing serial execution path.
Invocation-owned container operations can execute concurrently. New operations
need a supported lowering or an explicit, correct host access contract; they
are not silently assumed safe.

Each chunk retains its entities' attachment order and owns separate bindings,
VM/host scratch and temporary arenas. A repeated sequence can begin partway
through an entity's attachments. Chunk boundaries follow entities; a short
prefix aligns each chunk with its compiled sequence kernel. A uniqueness check
rejects plans that would split one entity across chunks. Before dispatch,
liveness, service revisions, program generations, observation and active waits
are checked. Source failures are retained on the caller in row order without
repeating effects. The serial scheduler's continuation after failed prepared
entries is preserved. Structural work remains behind ordered barriers.

Nearest-entity queries read category membership prepared on the caller. Workers
do not start ECS queries or mutate the shared query cache. Compiler dependency
checks keep Transform and Collider stable throughout the span. Material
parameter writes validate Self ownership and protect the shared override table
with a short critical section. The override table is read only after workers
join. This helper is safe to call from workers; the table mutation itself is
serialized. Arbitrary host sinks are not given the same permission.

The scheduler requires at least 1,024 rows, chunks of at least 256 rows, and a
minimum estimated amount of lowered work. A simple counter at 4,096 entities
was slower when distributed despite being independent. The work threshold
keeps that loop inline. This is a conservative heuristic, not hardware-specific
autotuning. Prepared bindings are copied once per distinct binding per chunk,
not rebound for every row during schedule rebuilding.

Rendering gathers entity IDs on the caller, then prepares transforms, composed
parts, visibility, material parameters, inverse matrices and instance payloads
in chunks of 512 entities. At least two chunks are required. Each chunk owns
its output and a material-parameter cache with separate entries per material.
The caller merges chunks in entity order, resolves/compiles shader programs,
sorts draws, uploads buffers and submits through Cute Framework. ImGui stays
on the main thread. Lights, surface influences, trails and particle-emitter
updates retain their existing serial phases. This change parallelizes geometry
preparation, not every rendering operation or GPU submission.

## Measurement

Arena3D, 20,000 initial enemies, 1280x720, all authored surface influences,
four physics substeps, sleeping and solid CCD enabled, Auto eight workers.
Release without sanitizers or per-behavior profiling, three alternating paired
trials, 30 warmup plus 240 combat frames. Both executables use identical assets
and physics configuration. Medians of trial medians, in milliseconds:

| Population / phase | Before | Parallel |
| --- | ---: | ---: |
| 5,000 / behavior | 1.317 | 1.131 |
| 5,000 / rendering preparation and host work | 1.257 | 0.806 |
| 5,000 / frame | 8.327 | 8.288 |
| 20,000 / behavior | 5.708 | 5.150 |
| 20,000 / rendering preparation and host work | 3.653 | 1.848 |
| 20,000 / physics | 6.486 | 6.692 |
| 20,000 / frame | 18.100 | 15.281 |
| 20,000 / frame p95 | 28.535 | 28.478 |

The large scene's median is about 16% lower, equivalent to 55 versus 65 FPS.
Its p95 is essentially unchanged and still exceeds the 60 Hz budget. Physics
remains the largest measured phase, including its tail. The smaller scene
remains limited by presentation waiting and shows little frame-rate improvement.
Each after-run reported eight behavior workers and eight rendering workers.
Position checksums, frame-by-frame combat traces, restart checks and zero
interpreter calls matched across both executables and every trial.

[Paired scene samples and executable hashes](parallel-scene-final-results.json).
An [initial comparison](parallel-scene-results.json) exposed extra binding work
in schedule rebuilding; the table above uses the corrected setup and the final
cheap-loop threshold.

The [final Tick comparison](parallel-tick-final-results.json) measures 4,096
entities across three paired trials, with 64 measured frames after eight warmup
frames. Values below are nanoseconds per entity per update:

| Tick workload | Before SLJIT | Current SLJIT | Current generated C system |
| --- | ---: | ---: | ---: |
| Counter | 6.088 | 6.153 | 5.981 |
| Component | 10.857 | 10.990 | 10.727 |
| Integer array | 236.988 | 46.432 | 51.647 |
| String array | 349.758 | 61.375 | 62.565 |
| Name array | 346.100 | 62.916 | 65.792 |
| Struct array | 2044.960 | 267.509 | 281.189 |

The cheap loops remain inline and within about 1% of their previous timings.
The heavier container loops improve by 5.1 to 7.6 times. SLJIT takes 0.90 to
1.03 times the generated C time in these cases. Both use the same scheduler;
the C comparison replaces compiled bodies and system kernels in the test
executable. This does not measure a complete standalone AoT scene export.
All compared backends preserve checksums and pass the allocation checks.
The [initial Tick samples](parallel-tick-results.json) retain the counter
regression that motivated keeping small amounts of work on the caller.

The behavior gain is smaller than the rendering gain because parallel body
execution is only part of the behavior phase. Schedule maintenance, instance
clock updates, Begin/event delivery and structural effects still consume caller
time. Parallelizing those blindly would change ordering or create races.

## Inspection and validation

`game_behavior_last_update` reports actual parallel entries, submitted tasks
and the maximum workers observed in a span. `render_arena_last_work` and the
Scene renderer wrapper report geometry entities, tasks and observed workers.
MCP attachment inspection exposes `tick_parallel_safe`. This is a compiler
proof, not a promise that the current population, ordering or debugger state
will dispatch work. Explicit serial switches are available for comparisons.
Detailed per-behavior profiling and debugger observation retain the serial,
instrumented execution path; ordinary aggregate frame timing does not require
those observers.

Runtime regressions exercise native and bytecode results, private managed
scratch, cross-entity writes that must remain serial, observation barriers,
hot apply and retained errors without repeated effects. A 1,537-entity GPU
readback fixture compares serial and parallel opaque/transparent geometry,
composed parts and culling, requiring identical pixels. Render checks also
exercise materials and hot replacement. The Arena benchmark requires both
paths to actually report multiple workers.

Final validation passed: `make build`, `make test` with ASan/UBSan,
`make web` including the BYTECODE and single-worker physics smoke tests,
native `--render-checks`, and `git diff --check`. The new render fixture used
four tasks across three observed workers and produced identical pixels.
The paired Arena runs used eight observed workers in each new path. Race
sanitizer coverage was not run.

## Executor choice

EnkiTS supplies range partitioning, per-task completion, worker indices and
optional dependencies. The current adapter uses the first three. Its executor
interface remains separate from the behavior and physics implementations.
[EnkiTS source](https://github.com/dougbinks/enkiTS/blob/master/src/TaskScheduler.h).

`brutal_tpool.h` is compact and supports independent jobs, but its global wait
cannot be called from a running job without waiting on that same job. Its
64-byte-aligned structures also need an allocation API that guarantees that
alignment. This was a code inspection, not a pool performance comparison.
[Pool source](https://github.com/waldnercharles/brutal/blob/main/include/brutal_tpool.h).

## Reproduce

```sh
python3 tools/bench_arena_scene_scale.py \
  --before build-performance/test_suite-before-parallel \
  --counts 5000 20000 --frames 240 --trials 3 --combat --expect-parallel \
  --output docs/research/parallel-scene-final-results.json
python3 tools/bench_ticks.py \
  --before build-performance/test_suite-before-parallel \
  --entities 512 4096 --frames 64 --trials 3 \
  --output docs/research/parallel-tick-final-results.json
```
