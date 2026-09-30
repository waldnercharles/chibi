# Whole-function Switch compilation

Switch Int, Enum, String and Name now lower into the shared behavior IR. Integer
cases become typed comparisons and branches. Text cases resolve their `Case:`
labels during publication, preserving first-match ordering, duplicate labels,
empty/null equivalence and the final default output. Malformed text labels cannot
match. Selection is evaluated once for each execution of the node.

SLJIT expands equality against a constant text label of at most eight bytes into
null checks and byte comparisons. Each mismatch branches before another byte is
read, so a shorter string cannot cause an overread. Longer and dynamic string
comparisons call the existing native string-equality helper. Integer Switch uses
a comparison chain; jump tables and large text dispatch tables remain possible
optimizations. Selection does not call a node evaluator or the flow scheduler.

Synchronous Switch bodies now also get instrumented variants. The normal native
entry omits source observation calls, while the inspection entry preserves safe
points, argument watches, source traversal and breakpoints through loops and
inlined function calls.

## Semantic checks

The matrix covers all four Switch families, direct and nested calls, three loop
iterations, delays, null/empty text, duplicate and malformed cases, unmatched
values, case-sensitive text and integer extremes. Five control modes cover
ordinary execution, tracing, stepping, paused waits and breakpoints. Additional
small step budgets compare failure state and source mapping. Oracle, bytecode,
SLJIT and generated C must match complete runtime inspection snapshots, and
compiled executions must report whole-function dispatch with zero oracle calls.

Native guard-page checks place every truncated prefix, an exact match and an
overlong input immediately before inaccessible memory. They cover one-byte,
five-byte, eight-byte, nine-byte and UTF-8 labels. This tests the inline boundary
and the longer-label helper against independent expected selections, including
overreads that native-code sanitizers cannot instrument inside SLJIT output.

Repeated loop stepping exposed a stale breakpoint marker in the oracle. It kept
the last executed breakpoint node and could skip a later visit to that node.
Clearing the marker when execution advances makes each loop iteration stop at
the breakpoint. Tests assert those stops independently of backend comparison.

The recursive call matrix also now includes loops that remain active across
recursive calls and host waits, doubling the authored combinations to 64. This
is function recursion with private loop state per invocation. Re-entering the
same active loop node within one invocation still needs dynamic continuations.

## Measurement method

`tools/bench_switch.py` compares SLJIT, portable bytecode and generated C for the
four synchronous Switch families, both plain and inside a three-iteration loop.
Each timed call cycles eight inputs and resets persistent variables and execution
state. Activation storage and host binding are prepared once per backend; each
measurement has 1,024 warmup calls. The default is one million measured calls,
five trials, with backend order rotated. Checksums have independent expected
values. The report records individual samples and source/binary hashes.

Publication and runtime activation setup are excluded. Generated C comes from
the same lowered program and uses the same ABI; it is not an independent optimal
C implementation or a standalone Scene export. The 1.5x native/C ratio is an
advisory target. Microbenchmark gains do not establish an Arena frame-time gain.

[The initial million-call comparison](whole-switch-initial-results.json) measured
the text-loop bodies at 1.681x and 1.812x generated C. The explicit equality helper
was called for both the empty label and `alpha` on each iteration. After bounded
literal expansion, [five new trials](whole-switch-results.json) give:

| Body | Initial SLJIT ns | Final SLJIT ns | Generated C ns | Bytecode ns |
| --- | ---: | ---: | ---: | ---: |
| Int | 2.932 | 2.865 | 3.074 | 69.984 |
| String | 5.745 | 3.800 | 4.601 | 65.538 |
| Name | 5.746 | 3.775 | 4.517 | 64.603 |
| Enum | 2.887 | 2.858 | 3.165 | 69.085 |
| Int, three iterations | 6.337 | 5.931 | 6.073 | 266.957 |
| String, three iterations | 16.551 | 7.206 | 9.273 | 245.273 |
| Name, three iterations | 17.128 | 7.326 | 9.324 | 247.318 |
| Enum, three iterations | 6.035 | 6.301 | 5.951 | 273.822 |

Times are trial medians per complete body invocation. The native text bodies
improve about 34% without a loop and 56–57% with three iterations. Final native/C
ratios range from 0.777 to 1.059; all eight meet the advisory target. The generated
C source hash is identical in both reports. The integer/enum bodies did not gain
an optimization in this follow-up; their small changes, including the 4.4% enum
loop increase, are not evidence of a systematic improvement. The label set is
small and does not measure long strings or large Switch tables.

[Five paired Arena runs](whole-switch-scene-results.json) compare the saved
pre-Switch executable with the final release binary, with detailed profiling
disabled and presentation excluded. Native behavior p50 stays at 0.008 ms and
p95 moves from 0.023 to 0.022 ms; simulation p50/p95 moves from 0.014/0.031 to
0.013/0.028 ms. Bytecode behavior p50 stays at 0.018 ms and p95 moves from 0.031
to 0.032 ms; simulation changes from 0.024/0.037 to 0.024/0.038 ms. The unchanged
behavior medians do not establish an Arena improvement. Both backends pass
firing/restart smoke assertions with zero oracle calls. This small Scene check
does not resolve the earlier 10,000-entity native tail regression signal.

## Verification

The final native `make test` passes with address/undefined-behavior sanitizers,
including the Switch snapshot matrix, expanded recursive-loop matrix, authored
catalog gate, Scene/editor checks and application smoke. The generated-C Switch
and recursive-loop matrices also pass; recursive tests replace every definition
entry so native callees cannot mask a generated-C omission.

`make web` builds the application and executes Switch and recursive-loop smoke
tests as BYTECODE with `CHIBI_ENABLE_SJLIT=OFF` and zero oracle calls. The separate
native no-JIT build reports UNAVAILABLE for both core behavior execution and
runtime capabilities, without interpreter fallback. Targeted formatting and
`git diff --check` pass. Full language, active-generation hot-apply, standalone
Scene AOT and large playable Scene acceptance remain unfinished.

```sh
build/test_suite --whole-switch
build/test_suite --emit-switch-c /tmp/switch.c
clang -std=c23 -O3 -ffp-contract=off -shared -fPIC -I src \
  /tmp/switch.c -o /tmp/switch.so
build/test_suite --whole-switch /tmp/switch.so
python3 tools/bench_switch.py --build-dir build-performance \
  --output docs/research/whole-switch-results.json
```
