# Array operations in whole functions

The array family now compiles creation, mutation, comparison and indexed access
inside whole functions. This extends language coverage beyond the current
authored catalog. Native execution is SLJIT, portable execution is behavior
bytecode, and both consume the same lowered program as the generated-C emitter.

| Operation | Whole-function implementation |
| --- | --- |
| Make Array, Add, Add Unique, Append, Set Array Elem, Insert | Named native C value adapters |
| Remove Index, Remove Item, Clear, Reverse, Swap, Resize | Named native C value adapters |
| Identical and struct Contains/Find | Native C value comparison |
| Array Get | Generated bounds checks and typed element loads |
| Scalar Contains/Find | Existing generated search loop; text equality calls its explicit helper |
| Length, Last Index, Is Empty, Is Not Empty, Is Valid Index | Existing generated reads/comparisons |
| ForEach and ForEachWithBreak | Existing generated iteration and control flow |

An owned-value adapter receives the invocation arena and borrowed prepared
arguments. It produces one complete result header whose dynamic values and names
belong to that arena. The ABI supports fixed or variadic NODE_PURE_CALL inputs,
with non-null struct results on success and stable error messages on failure.
Each static producer has a separate reusable argument/result bank, so nested
calls cannot overwrite an earlier producer's values. SLJIT directly calls the
concrete adapter address. It does not call a generic node evaluator or graph
dispatcher. Bytecode and generated C select the corresponding program binding.

Mutation nodes return a new array and preserve the input. Snapshot helpers own
strings, nested reflected struct fields and nominal type names. Get snapshots
only at required retention boundaries in compiled code. The oracle now owns its
selected value too. Resize allocates typed zero-initialized entries and copies
the retained prefix; it no longer frees borrowed string inputs through temporary
heap storage. Clear avoids copying the discarded elements. Insert is now included
in the array node registry. Equality of registered structs compares reflected
values, so copied records can match in Contains, Find, Add Unique and Remove Item.

At this stage, prepared Tick batches had no invocation arena. Bodies containing an
owned-value call therefore use whole compiled activations. The eligibility check
looks for actual owned-value call instructions rather than a broad managed-value
flag, preserving existing nonallocating host batches. The subsequent
[owned Tick migration](owned-tick-batches.md) adds reusable batch arenas and
retained failure storage. Removing redundant copies and reusing persistent
managed storage remain performance work. Add/Append still use a temporary array before the final owned
snapshot; Remove Index copies elements it later discards. Expanding these helpers
into SLJIT would not remove those costs by itself.

The focused test compares sixteen operation cases over integers, strings, names
and nested managed structs. It covers nested calls, variable stores, return
retention, waits, cancellation, stepping, breakpoints, limited budgets, invalid
indices, empty/null arrays and null element inputs. Snapshots compare state,
source errors, logical call stacks, watches and traversals across oracle,
SLJIT, bytecode and generated C. Default outputs also have independent expected
values. Direct helper tests free the input arena and asset before inspecting the
result, including nested fields and names. An allocating Tick fixture verifies
that unsupported batching is rejected and ordinary compiled execution succeeds
with zero oracle calls.

The generated-C comparison executes 128 emitted functions, covering all operation
and element combinations with and without a wait. Benchmarks run the 64
synchronous variants with reused host banks and VM scratch. They include array
ownership and persistent stores, and exclude publication, instance setup,
scheduling and inspection. They compare the same helper algorithms, not an ideal
handwritten-C implementation that might eliminate copies. Static Scene AOT
bindings remain unfinished.

## Measurements

[Five rotated trials](whole-array-op-results.json), each with 10,000 warmed
invocations per backend, use Clang 23.1.1 Release with sanitizers disabled. All
64 workloads meet the advisory 1.5x native/C target. Their median SLJIT/C ratios
range from 0.968 to 1.057. Representative complete operation bodies:

| Workload | SLJIT ns | Generated C ns | Bytecode ns | SLJIT/C |
| --- | ---: | ---: | ---: | ---: |
| Integer Get | 120.2 | 116.8 | 330.7 | 1.029 |
| Integer Add | 327.3 | 321.7 | 572.2 | 1.017 |
| String Make | 393.5 | 391.0 | 678.2 | 1.006 |
| String Append | 593.6 | 598.4 | 849.8 | 0.992 |
| Managed struct Contains | 697.4 | 692.2 | 1020.6 | 1.008 |
| Managed struct Append | 2017.6 | 2003.3 | 2368.6 | 1.007 |

The inputs contain three elements, with two more in the Append argument. These
are whole body costs including persistent source/result stores, not isolated
helper-call timings. The close native/C results support keeping substantial
ownership operations in C while compiling surrounding graph code. They do not
prove the allocation/copy algorithm is near optimal native performance.

[Seven paired Arena Scene runs](whole-array-op-scene-results.json) retain native
behavior p50 at 0.008 ms and simulation p50 at 0.013 ms. Behavior p95 changes
from 0.022 to 0.021 ms, and simulation p95 from 0.027 to 0.026 ms. Bytecode
behavior p50 remains 0.017 ms, with p95 changing from 0.031 to 0.030 ms. These
small differences do not establish an appreciable Scene speedup. Every run
checks compiled execution, firing/restart behavior and zero oracle calls.

[Five paired scale trials](whole-array-op-scale-results.json) use 256 sampled
frames at each count, with five ArenaEnemy behavior attachments per entity.

| Enemies | Native Tick before ms | Native Tick after ms | Bytecode before ms | Bytecode after ms |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 0.005 | 0.005 | 0.046 | 0.047 |
| 1,000 | 0.053 | 0.053 | 0.463 | 0.469 |
| 10,000 | 0.566 | 0.572 | 4.811 | 4.849 |
| 20,000 | 2.609 | 2.568 | 12.372 | 12.530 |

These are medians of each trial's Tick p50. At 20,000 the native paired changes
range from -1.097 to +0.043 ms; the median paired change is -0.040 ms. The broad
variation does not establish a reliable large-crowd improvement. Native peak
process memory is about 198 MiB at that count. The fixture holds positions fixed
and excludes physics/rendering, so playable large-scene acceptance remains open.

## Verification

Native `make test` passes with address/undefined-behavior sanitizers, including
all 33 authored event entries, 70 local functions and four library functions in
the catalog gate. The native application builds and its headless smoke passes.
The focused generated-C comparison also passes under the sanitized native runner.
`make web` builds the application and runs the nested array-operation wait cases
and allocating Tick fixture through actual BYTECODE with SLJIT absent. The native
no-JIT smoke reports UNAVAILABLE for core execution and runtime capabilities,
with no interpreter fallback. Targeted formatting and `git diff --check` pass.

## Reproduction

```sh
build/test_suite --whole-array-ops
python3 tools/bench_array_ops.py --count 10000 --trials 5 \
  --output docs/research/whole-array-op-results.json
```
