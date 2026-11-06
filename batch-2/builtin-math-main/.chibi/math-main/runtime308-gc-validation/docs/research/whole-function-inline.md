# Inlined calls, stored outputs and guarded optimization

September 11, 2026. This extends the typed argument/return work. The baseline
executables were preserved before call inlining. Both revisions use Release
`-O3`, Homebrew Clang 23.1.1, Apple M4 Pro, sanitizers disabled and
`MallocNanoZone=0`. [Raw results](whole-function-inline-results.json) include
executable hashes and five alternating before/after pairs.

## Implemented

The compiler follows acyclic synchronous effectful calls into their callee's
control blocks. Scalar arguments and returned values use typed storage. Set
Variable outputs retain the value written by that execution even if the variable
is subsequently overwritten. Separate calls to the same graph retain distinct
results. Unused output storage is removed before native stack allocation.

Logical frames preserve independent step budgets and the authored call-depth
limit. A failure records the actual graph, node, frame and step count; the public
call-stack API reconstructs the source stack without allocating child activations
during successful execution. Inspection continues through the general compiled
path, where existing watches and traversal records are collected.

A new generated-C comparison exposed excessive checks and stores in the initial
inlined program: 8.273 ns native versus 2.888 ns C, missing the 1.5× gate. The
compiler now derives a guarded variant for straight paths through multiple frames.
Before any effects, the guard proves that the root budget, all child budgets and
call-depth limits suffice. Inside that variant, variable/local reads can be
forwarded, constant arithmetic folded and temporary writes removed. The original
compiled body handles insufficient budgets and preserves partial effects and
precise failures. Branches and loops retain their checked compiled execution.

The optimizer produces shared instructions for SLJIT, bytecode and generated C.
The native entry also caches argument metadata in registers. Native word-sized
budget fields use `uintptr_t`, matching SLJIT's word width across C data models.

## Measured results

| Counter entities | Native before, ns/entity | Native after | Reduction | Bytecode before | Bytecode after |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 176.514 | 62.378 | 64.7% | 222.656 | 88.989 |
| 512 | 253.036 | 83.145 | 67.1% | 292.633 | 109.589 |
| 4,096 | 492.962 | 112.631 | 77.2% | 505.066 | 142.294 |

Each entity updates its own variable through an authored function. The benchmark
checks final values after eight warmup and 128 measured frames. The improvement
comes from removing child activation setup and per-node execution. Bytecode also
benefits from the shared lowering and optimization.

The final five-run generated-C comparison used two million invocations per trial:

| Kernel | SLJIT median | Generated C median | Ratio |
| --- | ---: | ---: | ---: |
| Inlined calls and retained outputs | 2.296 ns | 2.231 ns | 1.029 |
| Arguments, conversions and returns | 3.355 ns | 2.409 ns | 1.393 |
| Scalar arithmetic | 2.923 ns | 2.034 ns | 1.437 |

All pass the 1.5× gate. The scalar dispatch comparison passes the separate 1.1×
gate against the C host operation through the same scheduler. These small kernels
include invocation overhead and do not establish native parity for gameplay.

Arena's native median behavior time was unchanged at 87 microseconds. Bytecode
changed from 89 to 90 microseconds. Native p95 medians were 147 to 132 microseconds,
but the unchanged median does not establish a broad scene improvement. These
measurements cover simulation, not presentation timing.

## Acceptance state

The strict catalog gate remains **1/33 event entries and 1/31 effectful function
bodies**. Calls now lower far enough to identify the callee's actual blocker,
including component fields, integer comparisons and engine operations. The 39
pure local functions remain candidates for scalar inlining; full pure-function
and library coverage still needs an explicit gate.

Tests compare nested calls, repeated calls, stored snapshots, early/missing
returns, budgets, depth failures and error stacks against the oracle. Generated
C tests also compare both guarded and checked paths, with pre-consumed budgets
and word-boundary values. Existing gameplay parity and backend policy checks
remain required.

Reproduce the benchmarks with `tools/bench_behavior.py --build-dir
build-performance --trials 5 --iterations 2000000`, `bench_scheduler` and
`test_suite --compiled-arena`. The inlined-call comparison is now a required
performance gate. `test_suite --whole-function-audit` remains an intentionally
failing coverage gate until all language and engine operations lower.

The next coverage work is integer operations, vector/component field access and
explicit engine calls, followed by managed values, recursive/resumable frames
and whole-function debugger instrumentation. Complete gameplay still needs a
generated-C baseline. The full-coverage goal remains open.
