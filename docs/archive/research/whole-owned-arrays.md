# Owned arrays in whole functions

Whole-function lowering now supports nonempty array defaults, local and parameter
snapshots, returned arrays and persistent array writes. Element types include
scalars, strings/names and registered structs with nested string/name fields.
SLJIT, portable bytecode and generated C consume the same lowered operations.
This closes an ownership gap beyond the already complete authored catalog.

Copying is an explicit native C operation. The compiler emits array addressing,
length/bounds operations, scalar element access, graph loops and branches
directly. Immutable arguments and generation-owned defaults can share storage
within an invocation. Mutable variable arrays copy when retained by a local or
ForEach. Returned values copy into caller-owned storage; persistent writes use
instance-owned storage. Query results can escape by taking an owned snapshot.

The snapshot helper uses reflection to copy registered struct fields and nominal
type names as well as array items and strings. This is publication-independent
value ownership, with no graph evaluation or recursive interpreter calls.
Persistent managed writes still allocate. Repeated retention and large arrays
can be expensive regardless of backend; this change does not claim copy elision
or reference-counted sharing.

New tests found two existing lifetime defects. Array element type names could
outlive their source instance, and debugger watches could retain borrowed struct
payloads after an array variable was replaced. Runtime argument, function, loop
and watch snapshots now share the deep-copy contract. Watch replacement copies
before releasing its old snapshot. String/name array variable defaults also now
avoid treating their array pointer as a scalar string, and changing a variable's
container resets its default to the new representation.

The focused fixture compares oracle, bytecode, SLJIT and generated C snapshots
through nested calls, waits, cancellation, stepping, breakpoints, step budgets,
null/empty inputs and replacing the source array during iteration. Comparisons
include state, errors, call stacks, watches and traversals. Direct return tests
destroy the source instance and asset and overwrite caller inputs before reading
the returned strings, nested structs and type names. Query tests release the
query/world before checking retained handle arrays.

The generated-C defaults use immutable runtime bindings, not embedded process
addresses. A complete static Scene export still needs its own binding emission.
The subsequent [array-operation migration](whole-array-ops.md) adds creation,
mutation, equality and managed-struct searches. Recursive activations, remaining
container operations and broader hot-apply/pooling acceptance remain unfinished.

## Measurements

[Five rotated trials](whole-owned-array-results.json), 50,000 warmed invocations
per backend, Clang 23.1.1 Release with sanitizers disabled:

| Three-element workload | SLJIT ns | Generated C ns | Bytecode ns | SLJIT/C |
| --- | ---: | ---: | ---: | ---: |
| Integers | 332.260 | 334.060 | 831.020 | 0.995 |
| Strings | 583.360 | 578.220 | 1154.900 | 1.009 |
| Managed structs | 1921.280 | 1903.920 | 2793.160 | 1.009 |

Each invocation stores an input array, passes it through a nested function,
retains returned and variable snapshots, iterates while replacing the source
variable, writes each element and increments a persistent count. It includes
ownership helpers and persistent allocations, but excludes publication,
scheduling, initial instance setup and debugger observation. VM scratch and
host banks are reused after warmup. All backends check equal final counts and
array state, with zero oracle calls.

These bodies meet the advisory 1.5x ratio. Both native implementations call the
same ownership helpers, so this establishes low additional JIT overhead for this
workload, not parity with handwritten C that could avoid the copies altogether.
The roughly 2.0 microseconds for three managed records is a reason to pursue
copy elimination and retained storage reuse before expanding the copy helper
itself into SLJIT.

[The immutable-input regression benchmark](whole-owned-array-view-results.json)
still asserts no non-math calls in its whole body. Summing 64 doubles takes
41.690 ns in SLJIT, 34.565 ns in generated C and 3422.810 ns in bytecode, a
1.206 native/C ratio. The benchmark now prepares the activation/storage ABI for
all correctness fixtures and allows a snapshot helper only in its separate
mutable-variable fixture.

[Seven paired native Scene runs](whole-owned-array-scene-results.json) retain
Arena behavior p50/p95 at 0.008/0.022 ms. Native simulation p95 changes from
0.027 to 0.028 ms; bytecode behavior p95 changes from 0.031 to 0.032 ms.
This ownership change shows no native behavior improvement in the small Scene.

[Five paired scaling trials](whole-owned-array-scale-results.json) cover 100,
1,000, 10,000 and 20,000 enemies with five authored attachments each. Native
median Tick time is unchanged at 100/1,000 and changes from 0.602 to 0.606 ms at
10,000. The short 20,000-entity samples show 2.649 to 3.523 ms, a 33% regression.
[Nine longer paired trials](whole-owned-array-scale-repeat-results.json), 1,024
frames each, produce medians of 3.046 and 2.546 ms, but the median paired delta
is +0.038 ms. Individual paired changes range from -1.040 to +0.422 ms.
The conflicting summaries show considerable run variation. They do not establish
a stable improvement or reproduce the original regression magnitude. Large-crowd
performance acceptance remains open. The fixture holds positions fixed and
excludes physics and rendering; it does not prove a playable 20,000-enemy Scene.

## Verification

Native `make test` passes, including the catalog whole-function gate, gameplay
and editor checks. The native app builds and its headless smoke passes. Generated
C passes the full owned-array and existing array-view correctness comparisons.
`make web` builds the app and runs the new owned-array wait/store/iteration fixture
through actual bytecode with SLJIT absent. The native no-JIT smoke reports
UNAVAILABLE and rejects execution without oracle fallback. `git diff --check`
passes.

```sh
build/test_suite --whole-owned-arrays
python3 tools/bench_owned_arrays.py --output /tmp/owned-array-results.json
```
