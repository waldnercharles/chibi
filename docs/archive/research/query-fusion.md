# Query fusion and source diagnostics

Compatible `Count Matching Entities` nodes now share an entity traversal
automatically. Arena's four encounter counts use one loop without changing its
authored graph. The predicates remain reflected component/field conditions;
the compiler contains no Arena names or game rules.

## Compilation and safety

The reduction intrinsic records its checked prelude, cursor, predicate body and
accumulator in the shared IR. A compiler pass combines adjacent compatible
regions over the same snapshot. SLJIT, bytecode and generated C consume the same
result. Each fused iteration checks the entity generation once and evaluates the
individual predicates. No graph evaluator or per-entity C helper is involved.

The pass requires one snapshot value and one logical frame. Writes, host calls,
waits and control paths that can enter between reductions prevent fusion.
Multiple independent snapshots can each form their own group. Snapshot creation
still uses its existing host operation and reusable buffer.

Fusion speculates only reads into private compiler locals. Non-trapping column
guards check registration and layout before the fused loop. The original
compiled loops remain available when a guard fails, preserving the original
error node, pin, call source, budget and preceding effects. Their validation and
result publication sites stay in the original order. Inspection callbacks select
the original traversal order so debugger edits between observations remain live.
Neither path falls back to the interpreter.

Keeping both paths increases compiled code size. The optimization also adds
compile-time analysis: validation currently lowers relevant functions for their
diagnostics before publication lowers executable functions. This work is shared
per asset generation, rather than repeated for each instance or frame. Cold
publication cost has not been benchmarked in this change.

## Diagnostics

Node descriptors declare query cost semantics independently of their display
names. The compiler reports:

- `PERF-QUERY-FUSION`, a note identifying each count in a fused group.
- `PERF-QUERY-SCAN`, a warning for dynamic conditions, incompatible neighboring
  reductions, or a world query reachable from Tick through data dependencies and
  local/linked calls. The message describes the relevant limit or possible work
  amplification and suggests an authoring change.

These are static diagnostics, not measured time estimates. A Tick query warning
does not assert that the current scene is slow. The cost depends on instance
count, candidate count and execution frequency. Notes and warnings do not block
Play. The existing correctness diagnostics continue to do so.

Compiler Results shows the severity, explanation and a clickable graph/node
source. The pin ID is retained in the diagnostic and source tooltip. Immutable
program generations retain their reports through `behavior_program_report`.
Automation validation and inspection expose `note` severity separately from
warnings, and instance snapshots include `performance_diagnostics`. Detailed
profiling remains opt-in; these reports add no per-frame timers.

## Measurement method

The baseline is commit `a032ef71`, preserved as
`build-performance/test_suite-before-query-fusion`. Both executables use the
same unchanged assets and production step budget. Release ARM64 builds disable
sanitizers. Five trials alternate executable order and rotate bytecode, SLJIT
and generated-C body execution. Generated C is compiled with `-O3` and floating
point contraction disabled.

The isolated measurement includes snapshot production. Activation measurements
also include activation setup and release; body measurements reuse the context
and buffers. Every trial verifies the expected counts and zero oracle calls.
The generated-C driver additionally checks missing/mismatched column guards and
their original failure sources. A traversal callback edits health between two
counts; bytecode, SLJIT and generated C must all observe the edit in later counts.

The Scene comparison runs five paired 240-frame combat trials at 1,000 and 5,000
enemies with the shared four-worker physics executor and detailed profiling off.
It verifies matching position checksums and combat traces, damage/death/XP,
restart, empty event backlog and retained runtime failures. These measurements
are not directly comparable to earlier runs with detailed profiling enabled.

Reproduce from optimized binaries with no other builds or benchmarks running:

```sh
python3 tools/bench_encounter_query.py \
  --before build-performance/test_suite-before-query-fusion \
  --before-root "$PWD" --before-budget 0 --trials 5 --repeats 2000 \
  --output /tmp/query-fusion-results.json
python3 tools/bench_arena_scene_scale.py \
  --before build-performance/test_suite-before-query-fusion \
  --counts 1000 5000 --frames 240 --trials 5 --combat \
  --expect-after-workers 4 --output /tmp/query-fusion-scene-results.json
```

## Results

Median time per invocation over five trials on ARM64. Timings are milliseconds.

| Enemies | Previous SLJIT activation | Fused SLJIT activation | Reduction |
| ---: | ---: | ---: | ---: |
| 1,000 | 0.015084 | 0.012342 | 18.2% |
| 5,000 | 0.070432 | 0.056282 | 20.1% |
| 20,000 | 0.285822 | 0.226098 | 20.9% |

| Enemies | Bytecode body | SLJIT body | Generated C body | SLJIT / C time |
| ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.199650 | 0.010715 | 0.007490 | 1.43 |
| 5,000 | 0.989040 | 0.052155 | 0.038215 | 1.36 |
| 20,000 | 3.994740 | 0.207205 | 0.151805 | 1.36 |

The isolated reduction activation improves by 18–21%. This includes snapshot
production, which fusion does not remove. These results describe this query
workload; they are not a whole-scene speedup or a general SLJIT/C ratio.

Raw samples, compiler version and executable/asset hashes are recorded in
[query-fusion-results.json](query-fusion-results.json).

### Arena combat and restart

Each cell is the median of five runs' per-frame percentile, in milliseconds.

| Enemies | Phase | Before p50 | After p50 | Before p95 | After p95 |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1,000 | Behavior | 0.294 | 0.288 | 0.686 | 0.713 |
| 1,000 | Frame | 8.281 | 8.277 | 9.149 | 9.156 |
| 5,000 | Behavior | 1.390 | 1.376 | 2.846 | 2.849 |
| 5,000 | Frame | 10.636 | 10.416 | 13.817 | 13.511 |

Behavior p50 falls about 2% at 1,000 enemies and 1% at 5,000. The 5,000-enemy
frame p50 is 2.1% lower in this run, but the trial ranges overlap and paired
differences include regressions. Behavior p95 increases from 0.686 to 0.713 ms
at 1,000 and is essentially flat at 5,000. This supports the isolated scan
improvement, not a reliable broad frame-time gain.

At 5,000 enemies the behavior p50 ranges are 1.348–1.477 ms before and
1.354–1.386 ms after; frame p50 ranges are 10.367–11.084 ms before and
10.294–10.695 ms after. All paired position checksums and combat traces match,
with zero interpreter calls, no retained behavior failures, empty event backlogs
and successful restart.

Raw scene samples and executable hashes are in
[query-fusion-scene-results.json](query-fusion-scene-results.json). The phase
percentiles are not additive. Container representation and Tick body execution are measured separately in
[container-choice.md](container-choice.md) and
[tick-system-loops.md](tick-system-loops.md); this query pass changes neither.

## Scope

This pass fuses constant reflected count reductions. It does not recognize
arbitrary authored ForEach/filter loops, remove snapshot materialization,
maintain incremental counts, change container representations or fuse Tick
bodies into system loops. Those are separate optimizations. Complete gameplay
at 20,000 enemies is also still unproven; only the isolated query is measured at
that population.

## Verification

- `make build` and the complete `make test` suite pass, including MCP validation
  and inspection of note severity and graph/node/pin identities.
- `make web` builds the application and executes the bytecode backend smoke
  cases with SLJIT unavailable. These include fused reductions, independent
  snapshots, intervening writes, empty inputs and original error sources.
- `build-nojit`'s `check-behavior-backend` rejects native execution with
  `UNAVAILABLE` and asserts zero interpreter fallback.
- Native `--query-diagnostics-ui` renders the compiler notes, clicks a source
  row and verifies the correct graph and node selection. Both captures were
  inspected for readable layout.
- Query tests compare oracle, bytecode and SLJIT values, duplicate/stale handles,
  reflected predicates and ownership. Tick diagnostics follow function calls and
  exclude unused queries and disconnected Tick paths.
- The generated-C comparison checks layout/service failures and live traversal
  observations across all three compiled backends before timing them.
- `git diff --check` passes.
