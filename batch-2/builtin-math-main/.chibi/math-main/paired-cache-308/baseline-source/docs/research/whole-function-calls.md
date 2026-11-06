# Whole-function calls and native performance

September 11, 2026. This continues the work at `1476d707`. Measurements use an
Apple M4 Pro, Homebrew Clang 23.1.1, Release `-O3`, sanitizers disabled and
`MallocNanoZone=0`. Raw samples and measured executable hashes are in
[whole-function-calls-results.json](whole-function-calls-results.json).

## What now executes directly

Publication attempts whole-function lowering for effectful function entries as
well as events. Eligible bodies run with one SLJIT entry or one portable VM
invocation. Their native code handles scalar arguments, missing or incompatible
argument defaults, F32/F64 coercion, Min/Max and multiple scalar return values.
Return exits immediately, including inside Sequence and loops. Existing pooled
activations still implement the call boundary, independent function budgets and
error propagation. Inspection continues through the general compiled backend.

Min/Max use the authored ordered comparisons and select the right operand when
the comparison is false. This preserves NaNs and signed zeros; substituting a
platform min/max instruction without checking its semantics would be incorrect.

The catalog audit now finds:

- 1/33 whole event entries: `PlayerContactBehavior.Tick`.
- 1/31 whole effectful function bodies: `ArenaShardBehavior.Initialize Shard`.
- 39 other local functions are pure; eligible scalar expressions can be inlined
  into a caller. This is not a claim that all 39 qualify.
- Four linked library functions remain in the catalog.

The live Arena sample reports 1/22 whole event entries over 13 assets and 63
instances. The strict catalog command still exits with failure because the
remaining entries use the general compiled runtime.

## Measurements

Five before/after pairs alternated order, using the same authored content and
profiling settings. Counter dispatch measures 128 frames after eight warmup
frames. Each entity updates its own variable through an effectful function.
Values are checked after the run. The binaries were preserved before editing;
the results include their distinct hashes.

| Counter entities | Native before, ns/entity | Native after | Reduction | Bytecode before | Bytecode after |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 253.174 | 175.537 | 30.7% | 320.190 | 221.313 |
| 512 | 308.670 | 247.513 | 19.8% | 384.659 | 281.265 |
| 4,096 | 500.296 | 430.546 | 13.9% | 577.057 | 473.595 |

These are medians across runs. Native and bytecode both benefit from eliminating
per-node execution inside the function. Their shared activation and scheduler
work still dominates these short bodies. The remaining native/bytecode gap is
therefore much smaller than the arithmetic-only gap.

Arena runs 180 fixed simulation frames with firing and restart checks, measuring
the last 150. Median behavior time was 86 to 84 microseconds for native and 90
to 89 microseconds for bytecode. Native per-run medians ranged from 84 to 94 before
and 82 to 88 after. This overlap does not establish a reliable whole-scene gain.
Behavior p95 medians were 150 to 137 microseconds for native and 145 to 133 for
bytecode. These are simulation costs, not displayed frame times.

The generated-C comparison compiles the same lowered program with
`-O3 -ffp-contract=off`. Five trials used two million invocations each:

| Whole-function kernel | SLJIT median | Generated C median | Ratio |
| --- | ---: | ---: | ---: |
| Existing scalar arithmetic | 2.857 ns | 2.232 ns | 1.280 |
| Typed arguments, conversions, Min/Max, three returns | 3.566 ns | 3.088 ns | 1.155 |

Both pass the 1.5× limit. The scalar dispatch comparison also passes its 1.1×
limit against a C host operation through the same scheduler. These small kernels
include call and loop overhead; they do not establish equivalent performance for
complete gameplay programs. There is still no complete generated-C Arena baseline.

## Verification and reproduction

```sh
cmake --build build-performance --target bench_behavior bench_scheduler test_suite -j 8
python3 tools/bench_behavior.py --build-dir build-performance --trials 5 --iterations 2000000
MallocNanoZone=0 build-performance/bench_scheduler
MallocNanoZone=0 build-performance/test_suite --compiled-arena
build-performance/test_suite --whole-function-audit
```

For a before/after comparison, preserve both baseline executables before
building the changes, run them from the same checkout and alternate their order.
Avoid running builds concurrently with timings. The last command above remains
an intentionally failing coverage gate.

Runtime tests compare native, bytecode and oracle results for nested local calls,
effectful linked-library calls, changing pooled arguments, float narrowing and
widening, defaults, incompatible arguments, booleans, NaNs, infinities, signed
zeros, early Return, missing Return, budgets and source attribution. Inspection
tests verify recorded child-return watches and traversal records. The C export
test separately compares argument/return results and budget failures against the
native and portable implementations.

The native and WASM smoke tests invoke a nested whole function and assert zero
oracle calls. Native builds without SLJIT still reject execution. Existing Scene
and prototype parity checks cover gameplay and lifecycle semantics.

## Next compiler work

Compile across effectful call boundaries so a small synchronous callee does not
need a separate runtime activation. Add live typed values across control blocks,
including Set Variable outputs, then integers and direct vector/component field
access. These are the remaining blockers on frequent Tick paths. Keep explicit
native ABI calls for engine services and measure their argument handling.

This work should precede a scheduler replacement. Once an event can execute a
complete body, an ECS batch can share component lookup and dispatch across
entities. Compare that batch with equivalent generated C and measure serial
execution before adding worker scheduling and deterministic effect commits.
