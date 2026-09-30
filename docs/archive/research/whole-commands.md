# Whole-function deferred commands

`Send Event` with scalar payloads and `Destroy Entity` now execute inside whole
compiled behavior bodies. SLJIT computes inputs, writes typed payload slots,
emits control flow and calls the known C command directly. Bytecode and generated
C consume the same lowered operations. The calls never enter generic node dispatch,
`eval_node`, or the interpreter's flow executor.

The explicit command ABI may append deferred effects. It cannot dispatch graphs,
flush structural changes, or invalidate behavior/component storage. Each command
owns retained arguments before returning. Send Event shares the existing owned,
FIFO event queue; Destroy Entity marks retirement until the existing structural
boundary applies destruction. Repeated commands inside a query snapshot leave
that snapshot valid, while subsequent queries observe retirement.

Program generations retain immutable command descriptors, references and argument
metadata. Invocation scratch holds bound services and mutable payloads. The normal
execution wrapper prepares/releases that scratch; raw native/generated-C callers
can prepare it once and reuse it. Missing services and event references are checked
before data inputs, and callback failures retain their status and source stack.

## Scalar metadata and ownership

The first payload comparison found that the old scalar compiler retained numeric
bits but lost the caller's `type_name`. Scalar values now carry separate payload
and name slots during lowering. Locals, nested calls, arithmetic, conversions,
array elements, owner references, returns and persistent writes preserve the same
metadata as the oracle. Input bindings still apply their declared names, and
float/double coercions preserve their existing width-dependent behavior.

Persistent writes intern borrowed names. Registry/builtin names have stable
lifetimes. Query snapshots explicitly produce canonical `ecs.entity` handles,
and constant-local analysis proves when name copies and ownership calls can be
removed. Arbitrary caller/array names still receive owned storage. This matters
when an activation or an old asset generation is released. Generated C escapes
control characters and quotes in literal names.

## Performance

Native arm64, Clang 23.1.1, Release `-O3`, sanitizers disabled and
`-ffp-contract=off`. Five trials rotate execution order and use 10,000 warmup plus
200,000 measured invocations. No build, test or other benchmark runs concurrently.
Generated C uses the same lowered program, host callbacks and value semantics.
Body timings reuse prepared scratch; Arena timings include invocation setup,
allocation, scheduling and host work.

The initial four-command loop measured 82.960 ns SLJIT, 77.320 ns generated C and
534.680 ns bytecode. Its 1.073 native/C ratio passes the 1.5 limit. The callback
consumes all four payloads into a checksum and performs no allocation in this
microbenchmark. This measures the compiled command boundary, not event-queue
allocation cost. [Initial trials](whole-commands-initial-results.json).

After constant-name optimization, the final command loop measures
77.560 ns SLJIT, 76.890 ns generated C and
508.750 ns bytecode (native/C 1.009).
[Final command trials](whole-commands-results.json).

Preserving names initially regressed the 16-entity query body from the earlier
87.210 ns native result to 167.960 ns: every retained handle interned the same
canonical name. Proving those names constant restored native execution to
88.850 ns, versus 83.865 ns C and 1243.545 ns bytecode (ratio 1.059). Native trials
ranged from 87.140 to 134.230 ns. The C snapshot helper alone measured 72.490 ns.
[Regression](whole-commands-query-regression.json),
[optimized query trials](whole-commands-query-results.json).

The older generated-C checks also pass after supplying ownership callbacks in
the raw scalar/integer invocation harnesses. Calls and retained outputs measure
7.750 ns native versus 6.350 ns C (1.220). Arguments, pure bodies, math,
aggregates, I32/I64 and scalar dispatch also pass their existing gates.
[Complete benchmark output](whole-commands-existing-benchmarks.txt).
Earlier timings that omitted scalar name semantics are not equivalent comparisons.

Five paired Arena trials alternate before/after order against saved commit
`dc78774b`, with seed 12345, 30 warmup and 150 measured frames, firing/restart checks
and 63 instances. Both backends assert zero oracle execution.

| Backend | Behavior p50 before/after, µs | Behavior p95, µs | Simulation p50, µs | Simulation p95, µs |
| --- | ---: | ---: | ---: | ---: |
| SLJIT | 32 / 27 | 56 / 48 | 38 / 33 | 62 / 54 |
| Bytecode | 40 / 35 | 59 / 55 | 45 / 41 | 66 / 61 |

Native behavior time improves about 16%, simulation about 13%. Native behavior
p50 ranges are 31–36 µs before and 27 µs after. These results cover the command
migration and scalar metadata work together. They do not measure presentation
frame rate or larger entity populations. [Paired trials and binary hashes](whole-commands-arena-results.json).

## Coverage and validation

The strict catalog audit now finds 13/33 whole event entries, 10/31 effectful
function bodies, 39/39 pure local functions and 3/4 libraries. The previous
checkpoint had 5 whole events and 5 effectful bodies. New entries include shard,
projectile, surface-pulse and wave-pulse Tick handlers, contact handling, damage,
death and RefreshEncounter. Wave Tick's next blocker is Spawn Prototype
(graph 2021, node 2305). [Exact catalog output](whole-commands-audit.txt).

Tests compare oracle, SLJIT and bytecode for scalar widths and metadata, nested
calls, loops, mixed-width arithmetic, array elements, variable snapshots, missing
services/references before input errors, callback errors after prior effects,
budgets, source nodes/pins and stacks. They verify owned names after activation
release, hot apply and pool reuse. Real host tests exercise 65 FIFO events across
two dispatch batches, sender/payload release, deferred destruction, and query
snapshots across retirement. The generated-C matrix independently compares
payloads, stores, returns and error/source state. The portable backend smoke
includes whole command calls in a loop. `make test`, `make build` and `make web`
pass. The web smoke reports BYTECODE with no SLJIT/oracle execution, and the native
no-JIT build reports UNAVAILABLE with no interpreter fallback. The focused ASAN
command test also passes after adding scalar generation/pool ownership coverage.

```sh
build/test_suite --whole-command
build-performance/test_suite --whole-function-audit # nonzero until full coverage
python3 tools/bench_components.py --workload command --trials 5 --iterations 200000
python3 tools/bench_components.py --workload query --trials 5 --iterations 200000
python3 tools/bench_behavior.py --build-dir build-performance --trials 5 --iterations 200000
```

Full migration remains open. Strings/managed payloads, spawning and resumable host
work, other host operations, recursive frames and whole-function debugging still
need implementation. The remaining catalog exclusions use the general compiled
backend. A reported SLJIT backend alone does not prove whole-function execution.
See [the completion requirements](behavior-completion.md).
