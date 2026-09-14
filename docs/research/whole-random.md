# Whole random functions and bytecode operand metadata

September 12, 2026. Random Real, Random Range Real and Random Range Vector now
lower into the shared whole-function program. The [catalog audit](whole-random-audit.txt)
increases from 21/33 to 25/33 whole event entries. The four additions are Begin
on ArenaEnemyVisualBehavior, ArenaProjectileVisualBehavior, ArenaPickupBehavior
and ArenaShardBehavior. Effectful functions remain 24/31, pure functions 39/39
and library functions 4/4. The audit still exits nonzero while eight events and
seven effectful functions are excluded.

## What runs as native instructions

| Work | ARM64 SLJIT | Portable bytecode / other native targets |
| --- | --- | --- |
| Advance the existing random stream | C `rand()` call | Same platform RNG |
| Convert a sample to a unit interval | Native conversion and division | Typed VM instructions |
| Validate inputs, order bounds, clamp scalar results | Native comparisons and selections | Same lowered operations |
| Blend a double range or vector coordinate | Multiply and hardware fused multiply-add | VM `fma`; other native targets use the scalar `fma` ABI |
| Preserve extended scalar precision where `long double` is wider | Not needed on this ARM64 target | Explicit C range kernel retains extended precision and clamping |
| Extract vector fields and store results | Native loads/conversions/stores | Typed VM instructions |

The C RNG call preserves the existing stream and draw count. Native C uses that
same call in the comparison. It is not evidence that a C call is faster than an
inlined generator. Replacing the generator or moving to per-instance streams
would change seeded behavior and needs a separate explicit contract. Current
host-sink access metadata keeps random execution serial.

The initial parity check found a one-bit scalar difference: native C contracted
the range blend into a fused multiply-add, whereas separate lowered multiply
and add operations rounded twice. `BF_FMA` now represents that operation, with
three live operands tracked through optimization, register allocation and
suspension. ARM64 emits one FMADD instruction; its encoding was checked against
the installed assembler. Only functions containing FMA reserve the third FP
temporary. Other functions retain their previous register allocation.

The oracle now spells the blend with `fma`/`fmal`, making contraction explicit
for all backends and generated C even under `-ffp-contract=off`. This matches the
observed native contraction. Historic builds that disabled contraction can
produce a different low bit; this is not a promise of identical floating-point
results across every previous compiler configuration. The platform RNG itself
also remains platform-specific.

Repeated use of a random node shares its computed value within an evaluation.
A new evaluation consumes new draws. Selecting only X from a random vector still
consumes Y and Z draws. Invalid inputs fail before any draw, including a bad
later coordinate. Unused pure-function returns retain their random effects.
Instrumentation reads retained values rather than generating new samples.
Projection tracing also now retains the base vector node's input edges.

## Generated-C comparison

[Final five-trial measurements](whole-random-operands-c-results.json) use
Clang 23.1.1, Release O3, sanitizers disabled, 500,000 calls per fixture and
10,000 warmup calls. Each function returns the sampled value twice and another
independent scalar sample. Backend order rotates. C and SLJIT call the exported
body directly; bytecode reuses warmed activation scratch. Timed loops assert
zero additional VM allocations and zero oracle calls. Outputs, checksums and
the next RNG value match across all modes.

| Fixture | SLJIT ns/call | Generated C ns/call | Bytecode ns/call | SLJIT / C |
| --- | ---: | ---: | ---: | ---: |
| Unit scalar | 9.694 | 9.676 | 48.842 | 1.002 |
| Scalar range | 9.800 | 9.542 | 100.188 | 1.027 |
| Vector range | 21.134 | 19.014 | 229.222 | 1.111 |
| Vector X projection | 19.238 | 19.520 | 222.970 | 0.986 |

All meet the advisory 1.5x target. These are whole-function calls, not full Scene
updates, and both native implementations include RNG calls and result stores.
They do not establish that an inlined replacement RNG could not be faster.

## A VM cost exposed during the comparison

Initial scale measurements showed a small bytecode update regression, including
at 10,000 enemies in a [seven-trial repeat](whole-random-bytecode-repeat-results.json).
Inspecting the native VM assembly exposed repeated operand classification in
every iteration: the compiler generated a vectorized sequence of comparisons
just to decide whether to load each instruction's arguments. That work depends
only on the lowered opcode.

Finalization now computes the two operand-presence flags once, after all IR
transformations, including the straight-path variant. Runtime and instrumented
instruction arrays share those flags. The VM reads the flags directly. They
occupy bytes 49 and 50 of existing padding; the native instruction record remains
112 bytes and the component field remains at byte 52. This does not add another
per-entity allocation or change native body execution.

The [isolated five-pair comparison](whole-random-operands-results.json), using
256 frames, was flat at 10,000 enemies and reduced the 20,000-enemy bytecode
median from 12.261 to 11.811 ms. Its first trials were noisy. The final comparison
below checks the combined change against the executable before random lowering.
The [initial random microbenchmarks](whole-random-results.json) are retained;
bytecode medians there were 53.6–261.6 ns, versus 48.8–229.2 ns finally. These
separate microbenchmark runs are weaker evidence than paired scale runs.

## Authored ArenaEnemy setup and Tick

[Five final paired trials](whole-random-final-scale-results.json) use the actual
ArenaEnemy component set and all five behavior attachments, two Tick bodies per
enemy, eight warmup frames and 128 measured frames. Profiling is disabled.
Physics, rendering, contacts and wave spawning are excluded. Setup includes
component creation, attachment setup and their Begin execution. Timed Tick
includes both the prepared update and the scheduler pump. All checksums match,
selected backends are asserted, and compiled execution makes zero oracle calls.

Values below are medians of each run's p50. Paired deltas are medians of
individual after-minus-before differences and need not equal the difference
between the two separate medians.

| Backend | Enemies | Setup before / after ms | Tick before / after ms | Paired Tick delta ms | Peak RSS before / after MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| SLJIT | 100 | 0.696 / 0.589 | 0.005 / 0.005 | 0.000 | 15.73 / 15.66 |
| SLJIT | 1,000 | 5.902 / 5.205 | 0.058 / 0.057 | 0.000 | 24.70 / 23.31 |
| SLJIT | 10,000 | 57.889 / 53.703 | 0.673 / 0.638 | -0.011 | 119.05 / 106.92 |
| SLJIT | 20,000 | 120.125 / 105.755 | 2.966 / 3.012 | -0.060 | 227.78 / 197.44 |
| BYTECODE | 100 | 0.695 / 0.670 | 0.053 / 0.047 | -0.006 | 15.73 / 15.56 |
| BYTECODE | 1,000 | 6.046 / 5.799 | 0.486 / 0.464 | -0.022 | 24.77 / 23.28 |
| BYTECODE | 10,000 | 58.685 / 57.734 | 5.150 / 4.759 | -0.228 | 119.22 / 104.81 |
| BYTECODE | 20,000 | 118.055 / 117.590 | 12.477 / 12.519 | +0.075 | 224.23 / 194.06 |

Native setup at 20,000 enemies is about 12% lower. Peak memory is about 30 MiB
lower on both backends because the newly whole Begin path avoids its general
per-node activation storage. Steady native Tick has no substantial demonstrated
gain. Bytecode improves through 10,000 enemies and is effectively flat at
20,000 in this sample; its small positive paired delta is retained in the table.
Final 20,000-enemy Tick p95 is 4.004 ms native and 14.444 ms bytecode.
These results do not prove playable full scenes at these populations.

[Five final Scene-host pairs](whole-random-scene-results.json) run the authored
Arena3D firing/restart check with profiling disabled: 180 fixed steps, 30 warmup
steps and no frame presentation. Native behavior p50/p95 remains 0.011/0.026 ms;
simulation p50 remains 0.015 ms. Bytecode behavior p50/p95 changes from
0.018/0.033 to 0.019/0.035 ms, and simulation p50 from 0.023 to 0.024 ms. This
short scenario establishes no material whole-Scene speedup. Earlier
[scale](whole-random-scale-results.json) and
[Scene](whole-random-before-operands-scene-results.json) measurements before the
operand optimization remain available with their executable hashes.

## Validation and next work

`make test` and `make build` passed. Sanitized native Arena firing/restart checks
passed on SLJIT and bytecode, with 18/22 live event entries executing whole bodies
and zero oracle calls from compiled execution. The no-JIT native build reported
`UNAVAILABLE` and rejected execution without an interpreter fallback. `make web`
built the application and passed the BYTECODE backend smoke with SLJIT absent.

Random parity covers generated C, native SLJIT, bytecode and the explicit oracle:
draw order, repeated values, projections, reversed/extreme/equal ranges,
non-finite and missing inputs, nested calls, loops, budgets, errors, waits,
cancellation and inspection. The shared native/WASM smoke includes extreme
scalar and vector ranges. The final native backend and random checks also passed
after the portability-only rename of a file-local helper and smoke expansion.
The benchmark binaries precede those two non-semantic changes; their hashes
remain in the result files. Formatting and `git diff --check` passed.

The catalog coverage audit intentionally still fails: eight event bodies and
seven effectful functions retain whole-function exclusions. Passing runtime
tests does not close this coverage requirement.

The next scale targets remain the full attachment scans used for clocks and
pending activations, plus separate heap allocations for behavior instance state.
Twenty thousand enemies have 100,000 attachments. A pending-activation index
and contiguous hot state are concrete alternatives to measure. Component storage
and query algorithms still deserve attribution, but these measurements do not
justify choosing a replacement ECS library. The native Tick bodies already use
a prepared native loop automatically where semantics permit.

Whole coverage still requires managed struct fields, additional host operations
and container operations. The explicit Convert to System editor action and
standalone System scheduling also remain unfinished. See the
[completion inventory](behavior-completion.md).

Reproduce the body and scale measurements sequentially, without concurrent
builds or benchmarks:

```sh
python3 tools/bench_random.py --trials 5 \
  --output docs/research/whole-random-operands-c-results.json
python3 tools/bench_arena_scale.py \
  --before build-performance/test_suite-before-random \
  --after build-performance/test_suite \
  --counts 100 1000 10000 20000 --frames 128 --trials 5 \
  --output docs/research/whole-random-final-scale-results.json
CBLUEPRINT_BENCH_NO_PROFILE=1 MallocNanoZone=0 \
  build-performance/test_suite --compiled-arena
build-performance/test_suite --whole-function-audit
```
