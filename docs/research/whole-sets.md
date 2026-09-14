# Compiled Set operations

All four registered Set nodes now lower into whole behavior functions. Set
Length emits a nullable count load. Set Contains reuses the typed native/portable
search loop for scalar, string and name elements; managed struct comparisons use
an explicit C value call. Make Set and Set Add use explicit owned-result C calls.
None of these helpers receives a graph, node or evaluator context.

Array-backed lowered values now retain their actual container kind. Event and
function arguments check that kind, and local frames, persistent stores, direct
function results and debugger watches preserve SET instead of producing ARRAY.
The native SLJIT, portable bytecode and generated-C emitters use the same lowered
container metadata. Map remains a declared enum with no key/value model or
registered operations in this snapshot; this change does not claim Map support.

Independent expected-value tests exposed defects in the earlier native Set
implementations. Make Set retained duplicates. Set Add cloned to capacity equal
to count, then inserted only if there was spare capacity, so missing elements
were never added. Construction now removes equal duplicates while preserving
first-seen order. Add returns an owned snapshot with a missing item appended, or
an unchanged-value snapshot if already present. Adding to a null or empty Set
produces a singleton. Source values remain unchanged.

Both operations deeply own strings, names, managed struct fields and nominal
type names. Deduplication tests use independently allocated equal values, and
ownership checks read results only after releasing input storage. Direct compiled
function returns also survive destruction of the instance and asset.

The existing container test matrix now covers twenty operations across integers,
strings, names and managed structs. Set fixtures include nested calls, retained
event inputs, variable stores, waits, cancellation, low budgets, breakpoints,
stepping, watches, stacks, traversals, empty/null values and missing/existing
members. Native and bytecode snapshots compare against the oracle, with
independent value assertions for construction and insertion. Generated C adds
the same entry comparisons and sixteen direct Set-function return comparisons.

Shared native/web smoke tests also run Set Add through synchronous Tick batches
and prepared updates for all four element kinds. After warmup, these executions
create no arena backing blocks, graph runtime stores or oracle calls. Persistent
Set defaults own their data after the fixture's input arena is freed.

## Performance scope

Sets still use flat ordered storage. Membership and insertion scan linearly;
construction removes duplicates with a quadratic comparison loop. Owned stores
and returned snapshots copy their elements. This completes compilation of the
existing operations, but does not establish efficient large-Set algorithms.
Hash indexing, copy elimination and source-linked cost diagnostics remain
performance work. The current authored catalog has no Set node references, so
this extension is not expected to improve Arena's behavior cost directly.

The generated-C comparison includes the same helpers and ownership semantics.
A good native/C ratio alone does not prove an optimal Set representation or
handwritten-C performance with a different algorithm.

## Measurements

[Five rotated trials](whole-set-results.json) run 10,000 warmed invocations per
backend and workload with Clang 23.1.1 Release/O3 and sanitizers disabled. The
benchmark covers all eighty element-kind/operation pairs. Generated C first
passes the full container parity matrix, including the sixteen direct Set
returns. All eighty native/C ratios meet the advisory 1.5x target; the sixteen
Set ratios range from 1.000 to 1.090.

Median ns/invocation for integer and managed-struct Sets:

| Operation | Element kind | SLJIT | Generated C | Bytecode |
| --- | --- | ---: | ---: | ---: |
| Make Set | Integer | 126.3 | 120.8 | 367.4 |
| Set Add | Integer | 158.9 | 155.1 | 399.3 |
| Set Contains | Integer | 65.1 | 61.6 | 387.6 |
| Set Length | Integer | 62.9 | 62.2 | 242.4 |
| Make Set | Managed struct | 869.6 | 869.8 | 1285.1 |
| Set Add | Managed struct | 1268.7 | 1235.0 | 1588.3 |
| Set Contains | Managed struct | 578.0 | 566.9 | 868.0 |
| Set Length | Managed struct | 479.0 | 464.9 | 707.7 |

These are whole-function fixture costs, including a persistent source snapshot,
a nested call and result stores. They are not isolated costs of the Set Length
load or membership comparison. Inputs contain three members; Make Set receives
three equal operands and Add benchmarks an already-present member. Missing-item
insertion is exercised by correctness and prepared Tick tests. These samples do
not measure large-Set scaling or prove an algorithmic improvement over the old
Set implementation, which had incorrect construction/insertion semantics.

[Five paired Arena Scene runs](whole-set-scene-results.json) keep native behavior
p50/p95 at 0.008/0.022 ms and simulation p50 at 0.013 ms, with simulation p95
changing from 0.027 to 0.028 ms. Bytecode simulation p50 stays at 0.024 ms. These
firing/restart runs show no appreciable Scene gain.

[Three paired scaling trials](whole-set-scale-results.json), 256 sampled frames
each with profiling disabled, use the actual ArenaEnemy components and five
attachments per enemy. Native p50 before/after is 0.005/0.005 ms at 100,
0.052/0.052 ms at 1,000, 0.569/0.579 ms at 10,000 and 3.758/3.760 ms at 20,000.
The median paired native change at 20,000 is -0.038 ms; peak native process memory
is about 203 MiB. These fixed-position runs exclude physics and rendering.
They do not establish a playable large Scene or a new native scaling gain.

Initial bytecode p50 before/after is 0.481/0.500 ms at 1,000, 4.717/4.824 ms at
10,000 and 12.030/12.445 ms at 20,000. The median paired changes are -0.010,
+0.172 and -0.187 ms respectively, showing why the separate medians alone are
insufficient to attribute these small changes. The 10,000 case prompted a
longer portable repeat.

[Five longer portable pairs](whole-set-scale-repeat-results.json), 512 sampled
frames each, give p50 before/after of 0.461/0.465 ms at 1,000 and 4.940/4.873 ms
at 10,000. Median paired differences are -0.002 and -0.127 ms. The 10,000 p95
changes from 6.504 to 6.018 ms. The initial portable slowdown did not repeat;
these samples establish neither a consistent regression nor a Set-related
Arena speedup.

## Verification

Native `make test` passes with address and undefined-behavior sanitizers,
including the extended container and Tick tests, Scene/editor checks, catalog
coverage and native application headless smoke. The catalog remains 33/33 event
entries, seventy local functions and four library functions across nineteen
assets. The standalone backend smoke registry now includes the Set pack required
by its new fixtures.

`make web` builds the application and executes the shared Set construction,
membership, length, ownership, waits and prepared Tick checks through actual
BYTECODE with SLJIT absent. The native no-JIT core/runtime smokes report
UNAVAILABLE with no interpreter fallback. Targeted formatting, Python syntax
and `git diff --check` pass.

```sh
build/test_suite --whole-array-ops
python3 tools/bench_array_ops.py --count 10000 --trials 5 \
  --output docs/research/whole-set-results.json
```
