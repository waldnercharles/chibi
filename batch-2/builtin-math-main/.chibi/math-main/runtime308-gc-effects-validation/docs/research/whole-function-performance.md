# Whole-function compilation and scheduler measurements

September 11, 2026. Apple M4 Pro, Homebrew Clang 23.1.1, Release `-O3`,
sanitizers disabled. Floating-point contraction is disabled for the handwritten
and generated C references. All benchmark assertions remain active.

## Implemented

Eligible synchronous scalar event entries now execute as one SLJIT function.
The lowerer emits typed values, explicit branches/jumps, variable loads/stores,
step-budget checks and completion. It handles Event, Set Variable, Branch,
Sequence and WhileLoop, F32/F64 arithmetic, float comparisons, Boolean values,
Delta Time and nested pure calls in linked local/library graphs. Calls inline
into the caller. Repeated constants are shared within an expression. Four
scratch floating-point registers hold live values; excess values spill.
Functions without spills need no local stack storage. Fall-through jumps are
removed. Generated native instructions call no runtime helpers in this path.

The same lowered instructions have a portable bytecode executor and a C emitter.
The C emitter exports the same context ABI and reproduces variable updates,
budget failures and source node IDs. C compilation happens only in the benchmark
workflow, never during engine execution. Program generations own native code and
its embedded constants. Failure to emit an eligible native function is a hard
publication diagnostic.

This is an optimization with explicit eligibility, not universal whole-function
coverage. Event payload reads, integer/vector/managed values, casts, component
and entity operations, general effectful calls, host operations, waits, ForLoop,
ForEach and Break retain the general compiled runtime. No oracle fallback is
introduced. Pure-call depth/recursion and control expansion limits can also make
an entry ineligible. `behavior_program_function` exposes eligibility for an entry.

Instrumentation or stepping uses the existing instrumented compiled path.
Entries with breakpoints do not receive this optimization. Uninstrumented whole
functions do not materialize historical node values: `flow_run_input_value`
returns NULL, and watches remain unavailable until recording is enabled for a
new execution. Inspection does not replay effects to reconstruct old values.

## Reproducible C comparison and performance gates

Configure native dependencies as for the regular build, then run:

```sh
cmake -S . -B build-performance -DCMAKE_BUILD_TYPE=Release \
  -DCHIBI_ENABLE_SANITIZERS=OFF
cmake --build build-performance --target bench_behavior bench_scheduler --parallel 8
python3 tools/bench_behavior.py
build-performance/bench_scheduler
```

The Python runner uses the configured C compiler, emits a temporary C translation
unit, compiles it at `-O3 -ffp-contract=off`, loads it into the benchmark, and runs
five trials of two million measured iterations after warmup. It rotates backend
and direct-function order, validates checksums, reports medians/ranges and fails
when either of these initial scalar-fixture gates is missed:

- SLJIT whole-function median exceeds 1.5 times the generated-C median.
- SLJIT pooled-dispatch median exceeds 1.1 times the C-host-node dispatch median.

These gates apply to this fixture on an otherwise idle optimized native build.
They do not establish a scene-wide performance bound. Noise in nanosecond-scale
measurements is why these are an explicit benchmark command, not a sanitizer CI
unit-test assertion. The direct-function gate can be tightened with
`--max-native-ratio`.

To inspect the generated C directly:

```sh
build-performance/bench_behavior --emit-c /tmp/behavior-generated.c
```

The artifact contains the eight-operation numeric event and a Sequence/While/
Branch event. The latter compares SLJIT, bytecode and C for every budget from 0
through 16, including partially applied state and exact failing source nodes.
The unit suite separately compares the authored control graph against the oracle.

Five-trial results from the acceptance runner:

| Path | Median ns | Range ns |
| --- | ---: | ---: |
| Direct whole-function SLJIT | 2.763 | 2.672–3.913 |
| Generated C, same context and budget checks | 2.127 | 2.060–2.939 |
| Handwritten C arithmetic only | 0.787 | 0.746–0.979 |
| SLJIT pooled dispatch | 59.287 | 58.436–60.169 |
| Bytecode pooled dispatch | 123.935 | 118.952–131.799 |
| Oracle pooled dispatch | 471.565 | 467.819–487.057 |
| C host node through scheduler | 92.317 | 91.079–92.689 |

SLJIT was 1.299 times generated C. Its faster dispatch than the C-host reference
comes from bypassing node bookkeeping; it does not imply better arithmetic code
than Clang. The handwritten arithmetic function excludes context, budget and
variable-update costs and remains a lower-bound reference.

A separate five-trial comparison alternated before/after executables, with one
million iterations per trial and rotated backend order. The baseline used
`flow.c` and `instance.c` from `fdfe3f5c`; the benchmark and the published whole
program were otherwise available in both versions. This isolates runtime
selection and scheduler changes, rather than comparing compiler publication
cost. Both source files were restored before the final build. Executables were
explicitly relinked; an earlier stale-link timing set was discarded.

| Pooled dispatch | Before median ns | After median ns | After range ns |
| --- | ---: | ---: | ---: |
| SLJIT | 115.615 | 59.048 | 58.223–63.162 |
| Bytecode | 424.570 | 119.697 | 117.247–125.246 |
| Oracle | 476.141 | 468.125 | 463.806–470.383 |

The scalar dispatch improvement is 1.96 times for SLJIT and 3.55 times for
bytecode. Publication time and generated code size have no acceptance bound in
this change; programs still also own their instrumentable general implementations.

## Scheduler

The old pump restarted its search at activation zero after every runnable
activation. Retaining completed activations until callers release them made
waking a large queue quadratic. The new pump carries a cursor through unchanged
queues. It restarts when callbacks modify the queue, complete a host wait, resume
an activation explicitly, or change scheduler time. Thus a callback can wake an
older activation without letting a later event overtake it. The execution model
remains serialized. There is no new worker pool or concurrency contract.

`bench_scheduler` enqueues 64, 512 and 4,096 Delay activations, suspends them,
measures 1,000 idle ticks, then wakes all of them and verifies completion. Five
before/after trials alternated executable order. Results are total wake-batch
times; they include resuming/completing the activations.

| Queue | Before wake median | After wake median | Before idle tick | After idle tick |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 4 us | 1 us | 92 ns | 105 ns |
| 512 | 146 us | 6 us | 741 ns | 932 ns |
| 4,096 | 8.981 ms | 0.167 ms | 7.112 us | 7.155 us |

For 4,096 waits the wake median improved 53.8 times; ranges were 8.671–9.949 ms
before and 0.149–0.307 ms after. Idle scans remain linear and show no improvement;
the smaller queues regressed slightly. Some 64-item wake samples rounded to zero
at this timer's effective resolution, so that ratio is not reliable evidence.
This benchmark does not measure worker scaling, enqueue/release throughput or
whole-frame latency.

A regression test completes an older host wait from a newer activation and
asserts that its continuation executes before the next queued event. Existing
FIFO, cancellation, host-wait, pooled activation and reentrant dispatch contracts
also remain covered by the native suite.

## EnkiTS and Flecs assessment

[EnkiTS](https://github.com/dougbinks/enkiTS) provides task scheduling, dependencies
and pinned tasks, with a [C interface](https://github.com/dougbinks/enkiTS/blob/master/src/TaskScheduler_c.h).
It is a possible implementation of the engine's outer parallel-work interface.
It does not directly provide graph activation ownership, language waits,
cancellation semantics, source inspection or deterministic event delivery.

[Flecs systems](https://www.flecs.dev/flecs/Systems.html) provide useful examples
of table-oriented iteration, staged mutations and synchronization boundaries.
Its [pipeline API](https://www.flecs.dev/flecs/group__c__addons__pipeline.html)
also supports task-thread integration. Adopting Flecs would involve the ECS and
system execution model, beyond replacing this activation pump.

The evidence supports fixing the queue algorithm now and retaining the existing
scheduler. Neither library was benchmarked against this engine, so there is no
claim that the custom scheduler beats them. The applicable design direction is
coarse batches of independent entity/behavior work, explicit access dependencies,
and deterministic commit of staged structural effects. A node or a 59 ns event
is not yet a demonstrated useful task size. Any worker experiment must measure
submission/synchronization cost, prove deterministic snapshots, and reduce real
simulation time before becoming the default.

## Scene coverage and validation

The Arena firing/restart smoke reports 63 live behavior instances, 13 unique live
assets and **0 of 22 event entries eligible for whole-function execution**. Those
entries use the general SLJIT/bytecode path, with zero oracle calls. This audit
counts entries belonging to the live assets at the sampled frame, not every
asset in the content catalog, and does not count eligible numeric subregions.
The current whole-function optimization therefore does not establish the user's
broader native-performance goal for Arena.

One final optimized smoke measured SLJIT behavior p50/p95 at 0.102/0.132 ms and
simulation p50/p95 at 0.109/0.139 ms; bytecode measured 0.108/0.138 ms and
0.114/0.142 ms respectively. These are simulation samples from a hidden native
scene fixture, not rendered frame pacing, and are not a paired Arena speedup.

Validation includes nested pure library calls, scalar/NaN comparisons, branches,
while loops, sequences, exact budget failures, bytecode/C/native control parity,
instrumented watches/traversals, host FIFO wakeups, and the existing runtime and
gameplay parity checks. Native `make test`, the web app/backend smoke and native
JIT-unavailable smoke are the final migration checks. The web smoke now includes
the whole-function loop and asserts bytecode execution with SLJIT unavailable.
