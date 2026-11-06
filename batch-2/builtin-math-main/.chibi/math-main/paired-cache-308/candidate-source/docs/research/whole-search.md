# Typed array searches

Contains and Find now lower to typed loops in the shared behavior IR. Supported
values are booleans, integer widths, floats, enums, handles, strings and names.
Numeric comparisons, element addressing, tag checks and branches are native
instructions. String and nominal-name comparisons use a scalar text helper.
Aggregate and nested-container search remain excluded from whole bodies.

The work also improves loop-invariant register allocation, local aliases and
memory-field branches. Indexed element addressing is shared with ForEach.
Searches inside repeated pure calls and outer flow loops have distinct temporaries.

Typed equality fixes an existing bug: numeric array equality compared padding and
unused union bytes. It now compares values, including equal signed zeros and
unequal NaNs. Nominal handle/enum names compare by text. This is a semantic
correction, not a claim that old bytewise behavior was preserved. String-array
default ownership and nominal array metadata were also corrected. Nested-loop
inspection tests found and fixed shared pure-call traversal state and a skipped
breakpoint pause after stepping.

## Measurements

[Five rotated trials](whole-search-results.json) use 200,000 warmed invocations,
with 3,125 calls for 4,096-element searches, Clang O3 and contraction disabled.
Every run first checks oracle/bytecode/SLJIT/generated-C parity. The measured body
allocates no runtime data or VM scratch and makes zero oracle calls.

| Find miss | SLJIT ns | Generated C ns | Bytecode ns | SLJIT/C |
| --- | ---: | ---: | ---: | ---: |
| I32, 128 elements | 68.740 | 65.565 | 5,810.085 | 1.048 |
| I32, 4,096 | 1,974.080 | 1,941.440 | 183,395.200 | 1.017 |
| F64, 128 | 71.615 | 56.375 | 5,605.500 | 1.270 |
| Handle, 128 | 72.705 | 54.585 | 5,747.290 | 1.332 |
| String, 128 | 281.265 | 188.475 | 6,635.500 | 1.492 |
| String, 4,096 | 8,859.840 | 5,869.120 | 205,000.320 | 1.510 |

Fifteen of sixteen workloads meet the advisory 1.5x C target. The longest string
case misses by about 0.7%. These are raw body measurements, not Scene gains.
The initial native I32 4,096-element body took 10,453 ns before the register and
branch optimizations; [initial](whole-search-initial-results.json),
[register](whole-search-register-results.json) and
[index](whole-search-index-results.json) measurements retain those stages.

Two independent sets of seven paired Arena runs used profiling disabled, 180
fixed steps and 30 warmup frames. Native median behavior stayed 0.008 ms and
median simulation stayed 0.013 ms. Native behavior p95 rose 0.021 to 0.022 ms in
[the first set](whole-search-scene-results.json), and 0.020 to 0.021 ms in
[the repeat](whole-search-scene-repeat-results.json). Simulation p95 likewise rose
1 microsecond. Bytecode changes varied in direction. There is no measured median
Scene gain from this slice. The repeated native tail increase is unresolved;
single instrumented profiles show small differences across several waiting and
Tick bodies but do not establish a cause.

## Validation and coverage

Native make test/build, generated-C parity, compiled Arena firing/restart,
Emscripten app and bytecode smoke, and native no-JIT rejection checks pass for
this slice. Tests cover all supported types, first/last/missing matches, null and
empty arrays, nominal metadata differences, malformed element tags, caller
mutation across waits, cancellation, nested calls/loops, budgets, stepping,
breakpoints, watches, source stacks and traversals.

The [search-stage audit](whole-search-audit.txt) reports 31/33 events,
28/31 effectful locals, 39/39 pure locals and four libraries. It adds
ArenaPickup.Collect Along Spiral. Subsequent rhythm and reflected-call work
addresses the remaining authored blockers; see the [completion record](behavior-completion.md).

```sh
build/test_suite --whole-search
python3 tools/bench_search.py --count 200000 --output /tmp/search-results.json
```
