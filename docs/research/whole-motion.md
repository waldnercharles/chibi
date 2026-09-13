# Compiled motion and Tick performance

Advance Damped Spring, Quantize Angle and Smooth Step now lower into typed
arithmetic, conversions and explicit validation checks. Square Root, Floor,
Ceiling and Round also lower. Arm64 emits FSQRT, FRINTM, FRINTP and FRINTA through
SLJIT's custom instruction API; other native targets use scalar libm calls.
The portable VM and generated C use the same lowered program and math semantics.

Spring preserves the oracle's operation order, float-to-double intermediates,
clamping, float result rounding and nonfinite-input/output errors. Both spring
outputs share one evaluation. No motion operation calls a node evaluator or flow
executor. Native register allocation treats hardware operations as instructions,
so values need not spill merely to cross a rounding operation. Comparisons used
only by the following branch or validation check compile directly to branches.

The [catalog audit](whole-motion-audit.txt) now reports 17/33 whole events,
20/31 effectful local functions, 39/39 pure local functions and 4/4 library
functions across 19 assets. The added event is ArenaShipMotionBehavior.Tick,
including its four effectful local functions. Its immutable compiled program
also qualifies for existing ordered Tick batching, without authored changes.
Full coverage remains unfinished; the audit correctly exits nonzero.

## Arena before and after

Fifteen paired trials for each profiling setting alternate the saved executable
from immediately before motion lowering with the final optimized executable.
Both use the same assets, deterministic input and random seed, with batching
enabled. Each run warms up for 30 frames and records 150 frames. Both compiled
backends assert firing, restart, backend selection and zero oracle calls.
[Raw logs, executable hashes and paired results](whole-motion-arena-results.json)
are retained. No builds or other benchmarks ran concurrently.

Median across trials, in microseconds per frame, with profiling disabled:

| Metric | SLJIT before | SLJIT after | Bytecode before | Bytecode after |
| --- | ---: | ---: | ---: | ---: |
| Behavior p50 | 20 | 15 | 28 | 23 |
| Behavior p95 | 46 | 37 | 55 | 45 |
| Simulation p50 | 26 | 21 | 35 | 29 |
| Simulation p95 | 55 | 46 | 64 | 50 |

Native median behavior time falls 25%; bytecode falls 17.9%. The median paired
native p50 difference is -5 microseconds and the p95 difference is -7
microseconds. Tails vary across runs; the table reports medians of each run's
percentiles, not percentiles of one combined sample. These are simulation costs,
not rendered frame rates or a guarantee for other scenes or entity counts.

Separate profiled trials attribute the improvement to ship Tick. Its recorded
native execution cost falls from 4.673 to 0.127 microseconds per measured frame;
bytecode falls from 6.033 to 1.420. Native setup falls from 0.353 to 0.027.
These very small individual timings include profiler resolution effects.
Batched broadcasts rise from 358 to 537 across 180 frames. Live Arena whole
coverage rises from 12/22 to 13/22 entries. The program still uses the ordinary
compiled activation path where batching is ineligible.

## Equivalent generated-C comparison

```sh
python3 tools/bench_motion.py --build-dir build-performance \
  --output docs/research/whole-motion-results.json
```

The script uses Clang 23, Release optimization, sanitizers disabled and
`-ffp-contract=off` for emitted C. Five trials rotate SLJIT, generated C and
bytecode. Every body warms up for 10,000 calls and measures 500,000 calls over
1,024 deterministic inputs. Results, metadata, validation, budgets and checksum
must agree. Timing includes argument preparation and the shared function ABI;
it excludes activation setup, scheduler work and compilation. No oracle calls
are allowed in timed execution. This is not a handwritten ECS loop comparison.

Median nanoseconds per call from [the final results](whole-motion-results.json):

| Operation | SLJIT | Generated C | Bytecode | Native/C |
| --- | ---: | ---: | ---: | ---: |
| Advance Damped Spring | 18.636 | 7.496 | 311.488 | 2.486 |
| Quantize Angle | 3.974 | 2.992 | 103.936 | 1.328 |
| Smooth Step | 3.476 | 2.480 | 93.888 | 1.402 |
| Square Root | 2.474 | 2.250 | 38.074 | 1.100 |
| Floor | 2.474 | 2.300 | 38.512 | 1.076 |
| Ceiling | 2.554 | 2.248 | 38.686 | 1.136 |
| Round | 2.482 | 2.352 | 38.642 | 1.055 |

Spring still misses the advisory 1.5x target. It remains an optimization task;
its roughly 11 ns gap per body does not block the measured Arena improvement.
The script reports this miss and succeeds unless `--enforce-native-ratio` is
requested. Missing measurements, incorrect values and nonfinite timings remain
hard failures. [Initial measurements](whole-motion-initial-results.json) precede
comparison/validation fusion and retain a 2.592x spring ratio and 1.556x angle
ratio. The small difference between separate kernel runs is weaker evidence
than the paired gameplay comparison.

## Verification and remaining work

Native `make test` and `make build` passed, including existing prototype and
host-wait coverage. New tests compare the explicit oracle, native and portable
execution on NaNs, infinities, signed zero, subnormals, halfway rounding,
float-output overflow, 1,000 random spring input bit patterns, source diagnostics
and step budgets. Additional tests compare nested waits, stepping, watches and
traversals, and preserve values across hardware operations, libm calls and stack
spills. Generated-C parity passes under ASAN/UBSAN. Arena firing and restart also
pass under those sanitizers.

`make web` passed and the Emscripten smoke reports BYTECODE whole motion
execution with zero oracle calls. The native no-SLJIT build reports UNAVAILABLE
and rejects execution without fallback. Changed C/H formatting and
`git diff --check` pass.

Find Nearest Entity is the next whole-function blocker in EnemyBehavior.Tick.
Other remaining exclusions include managed values, randomness, visual/physics
commands and queries. Full debugger and gameplay parity coverage remains tracked
in [the completion criteria](behavior-completion.md). Safe compiler optimization
and batching can expand as each body becomes eligible; authors do not need to
wait for full language coverage to benefit.
