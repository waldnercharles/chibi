# Reusable managed variable storage

Persistent managed writes now copy once into reusable storage. Each instance
variable has a current arena and a spare arena. A write finishes copying into
the spare, retains type names, publishes the new value and resets the previous
arena. This permits self-assignment and inputs that borrow an array slice or a
substring from the value being replaced. A rejected copy leaves the published
value intact.

The old array path first cloned the array through `pin_value_copy_to_arena` and
then recursively copied the original elements again. Strings were copied in
both passes. The replacement builds the array header and copies each element
once, including nested managed struct fields. Plain struct variables keep their
existing fixed storage and direct compiled stores.

Hot apply moves both arenas by stable variable identity, including when variables
are reordered. Pool release and destruction free both. Active instances retain
the capacities reached by the two arenas; shrinking a value does not return
those backing blocks to the allocator. This trades memory retention for stable
write cost. The first managed write adds the spare descriptors. Instances that
only write scalars or plain structs retain their previous table size. The flag
fits in existing instance padding on the measured ARM64 target.

`arena_block_allocations()` counts backing-block allocations on the current
thread. The owned Tick and benchmark gates require no new blocks after warmup
for the measured synchronous batch and prepared workloads. They also require
zero VM scratch and graph runtime store creation, correct state and zero oracle
calls. These counters do not count every heap allocation or prove that workloads
with growing values will stop allocating.

## Paired measurements

[Five paired trials](managed-reuse-tick-results.json) alternate the saved
before/current Release executables and rotate backend execution order. Each
uses 100 and 1,000 entities, eight warmup frames and 64 measured frames. Generated
C is emitted separately by each executable and compiled with Clang 23.1.1 at
O3 with floating-point contraction disabled. Sanitizers are disabled. Executable
hashes, generated-source hashes, raw samples and paired differences are retained.

Prepared Tick results at 1,000 entities, in ns/entity:

| Workload | Native before | Native after | Native speedup | Bytecode before | Bytecode after | Generated C after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Integer array | 386.078 | 235.766 | 1.638x | 646.391 | 499.828 | 233.719 |
| String array | 647.906 | 351.969 | 1.841x | 942.547 | 625.609 | 347.812 |
| Name array | 630.203 | 348.703 | 1.807x | 903.375 | 611.281 | 343.500 |
| Managed struct array | 2238.562 | 2057.703 | 1.088x | 2580.000 | 2384.500 | 2081.469 |

Every paired native and bytecode trial improves for these four 1,000-entity
workloads. Native time decreases by about 39%, 46%, 45% and 8%, respectively.
The scalar counter and component cases remain controls; their native medians
change from 7.984 to 7.844 and 11.937 to 11.687 ns/entity. That small variation
does not establish a storage improvement for bodies that already bypassed
managed writes.

All twelve current workload/count native/generated-C ratios meet the advisory
1.5x target, ranging from 0.936 to 1.262. Both backends call the same ownership
helpers, so this is not a comparison against an independently optimized,
handwritten C algorithm. The paired before/current measurements establish the
improvement from this storage change independently of that ratio.

The first implementation eagerly enlarged every instance's arena table. Its
[initial measurements](managed-reuse-eager-scale-results.json) and
[longer native repeat](managed-reuse-eager-scale-repeat-results.json) showed
higher median native Tick cost at 20,000 entities. The repeat changed 2.520 to
2.659 ms, with noisy paired differences from -0.384 to +1.194 ms. That prompted
the lazy-table change, avoiding extra descriptors for Arena's scalar-only state.
The [initial helper measurements](managed-reuse-eager-tick-results.json) and
[small Scene runs](managed-reuse-eager-scene-results.json) are retained as
intermediate results, not measurements of the final implementation.

[Five final paired Scene runs](managed-reuse-scene-results.json) keep native
simulation p50 at 0.013 ms and change p95 from 0.027 to 0.026 ms. Native behavior
p50 changes from 0.007 to 0.008 ms, with p95 unchanged at 0.021 ms. Bytecode
simulation p50/p95 stays at 0.023/0.037 ms. These are hidden native Scene runs
with firing and restart, without presentation. There is no appreciable small
Arena simulation improvement from this managed-storage change.

[Seven final paired scaling trials](managed-reuse-scale-results.json) use
512 sampled frames and the real ArenaEnemy Tick bodies with all five attachments.
Profiling is disabled. Tick medians, in ms:

| Enemies | Native before | Native after | Bytecode before | Bytecode after |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 0.005 | 0.005 | 0.049 | 0.049 |
| 1,000 | 0.054 | 0.053 | 0.495 | 0.484 |
| 10,000 | 0.586 | 0.580 | 5.203 | 5.128 |
| 20,000 | 3.269 | 3.063 | 13.698 | 13.151 |

At 20,000, native paired changes range from -0.206 to +0.073 ms, with median
-0.061 ms. Native p95 changes from 4.617 to 4.000 ms. At 10,000, the native
paired p50 median is +0.001 ms and p95 worsens from 0.602 to 0.972 ms. These
variable tail measurements do not establish a broad Arena performance win;
tail attribution remains open. The fixture holds positions fixed and excludes
physics and rendering, so it does not prove a playable 20,000-enemy Scene.

The lazy table removes the eager descriptor growth in this scalar workload.
Native median peak-memory growth during setup is 88.234/88.250 MiB at 10,000
before/after and 178.750/178.719 MiB at 20,000. Final native process peak memory
is about 200 MiB at 20,000, and cold setup changes from 115.375 to 114.266 ms.
These process measurements do not quantify retained capacities for arbitrary
managed workloads; managed values still retain two arenas until pool release.

## Native helper boundaries

Substantial C operations are a valid route to native performance. The graph
compiler can emit control flow and small scalar/field operations directly while
calling compiled C for ownership, container algorithms, complex geometry and
host capabilities. Such calls require an explicit argument/result, effect and
lifetime contract; they must not evaluate graph nodes or retain borrowed inputs.
Reflection already generates adapters for eligible pure native functions.

Opaque C calls limit optimization across their bodies. Our SLJIT backend cannot
inline their machine code or propagate graph constants into their implementation.
Effect metadata permits analysis around a call but does not reveal its internal
instructions. Small calls inside hot loops therefore remain candidates for direct
IR expansion; substantial operations can amortize argument packing and call
overhead, particularly when one helper processes a span of entities or elements.
This remains subject to ordering, ownership and host-effect rules.

For AOT, generated C with static helper bindings and helper definitions available
to the compiler can enable cross-module optimization through
[LLVM LTO](https://llvm.org/docs/LinkTimeOptimization.html). Current generated-C
benchmarks use runtime bindings; a complete statically bound Scene export remains
unfinished. Helper reuse does not remove that export work or the remaining
recursive, debugger and large playable Scene acceptance.

## Verification

The final native `make test` passes with address and undefined-behavior sanitizers,
including managed writes, borrowed slices/substrings, rejected partial copies,
scalar/managed transitions, compatible variable reordering, pooling and retained
snapshots. The shared owned Tick tests require allocation reuse and preserve
SLJIT/bytecode/oracle state and debugger comparisons. The complete authored
catalog gate passes, as do the native application build and headless smoke.
The final `make web` builds the application and passes actual BYTECODE owned-Tick
and runtime smoke checks with SLJIT absent. Native `CHIBI_ENABLE_SJLIT=OFF`
smokes report UNAVAILABLE for core execution and runtime capabilities, with no
interpreter fallback. Targeted formatting, Python syntax and `git diff --check`
pass.

```sh
build/test_suite --managed-reuse
python3 tools/bench_ticks.py \
  --before build-performance/test_suite-before-managed-reuse \
  --entities 100 1000 --frames 64 --trials 5 \
  --output docs/research/managed-reuse-tick-results.json
```
