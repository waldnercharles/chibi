# Immutable bytecode loop bounds

The portable behavior runner now reads its instruction count once when entering
the selected program, before executing instructions. Program generations are
immutable, so a host call, debugger callback or nested invocation cannot change
this loop bound. Resumed execution selects its program and count again.

Previously, the loop evaluated `acount(instructions)` every iteration. The
optimized ARM64 executable checked the instruction pointer and loaded the array
header on each pass. The updated executable keeps the count outside the loop;
calls may still require reloading the saved count from the stack. Register
allocation and block layout also changed, so timings measure the complete
compiler output rather than an isolated memory-load cost.

This changes no IR, helper ABI, context layout or SLJIT emission. Complex C
operations remain available. It is an example of moving immutable work outside
a hot loop without requiring cross-function optimization through each helper.

## Paired ArenaEnemy measurements

[Five alternating baseline pairs](vm-bounds-scale-results.json), with 512
sampled frames after eight warmup frames, compare the saved final MultiGate
executable with this change. Both are Release builds without sanitizers;
profiling is disabled. These fixtures retain the authored enemy components and
five attachments, but omit physics and rendering. All state checksums match and
all samples report zero oracle calls.

| Bytecode entities | Before p50/p95 ms | After p50/p95 ms | Paired p50 delta ms |
| ---: | ---: | ---: | ---: |
| 100 | 0.051 / 0.053 | 0.042 / 0.043 | -0.009 |
| 1,000 | 0.485 / 0.508 | 0.417 / 0.441 | -0.064 |
| 10,000 | 5.078 / 6.657 | 4.304 / 5.710 | -0.798 |
| 20,000 | 13.358 / 15.164 | 11.623 / 13.103 | -1.527 |

Bytecode is faster in all five pairs at every count. Median p50 reductions are
13.0–17.6%. SLJIT p50 medians are unchanged at 100 and 1,000 entities. Its larger
counts vary in both directions across pairs; the generated Tick code is
unchanged, and this experiment does not establish a native optimization.

The context remains 456 bytes on this host. This experiment does not identify
the cause of the earlier MultiGate integration regression, establish a web
timing gain, or close native tail-latency and large playable Scene acceptance.

[Five paired Arena firing/restart runs](vm-bounds-scene-results.json) pass both
backend checks with zero oracle calls. Bytecode behavior p50/p95 moves from
0.018/0.031 ms to 0.017/0.030 ms and simulation from 0.023/0.038 ms to
0.022/0.036 ms. Native behavior p50 stays at 0.008 ms and simulation p50 at
0.013 ms; their p95 values move from 0.021 to 0.022 ms and from 0.027 to
0.028 ms. These small Scene timings do not demonstrate a native speedup.

## Verification

The optimized MultiGate semantic matrix and full native `make test` pass,
including debugger/lifecycle checks, catalog audit, editor checks and application
smoke. `make web` builds the application and executes its BYTECODE smoke tests,
including MultiGate, recursive calls, waits and prepared updates. No SLJIT is
linked into that build. The paired native Scene runs pass firing/restart and
zero-oracle assertions. Formatting and `git diff --check` pass. Both measurement
reports match release executable SHA-256
`f6d4cf4cf22f0fe9943229584c89837bdfeab76206a8a9ce6e7d9b93836517a7`.

```sh
python3 tools/bench_arena_scale.py \
    --before build-performance/test_suite-before-vm-bounds \
    --counts 100 1000 10000 20000 --frames 512 --trials 5 \
    --output docs/research/vm-bounds-scale-results.json
```
