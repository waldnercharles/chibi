# Automatic Set storage and membership

Owned scalar Sets now retain their ordered value array and, at 64 or more
members, add an open-addressed index into that array. Small Sets, aggregate
members, nested containers and borrowed host arrays retain flat storage. This
changes runtime representation without changing authored graphs or iteration
order. Ordinary Arrays keep their ordered, duplicate-preserving representation.

The index is built alongside an immutable owned snapshot, in the same arena.
Persistent variables and values retained across waits receive their own index.
Graph/heap copies do not borrow it, and mutations invalidate it. Hashing follows
value equality, including nominal handle/enum types, equal string contents,
signed zero and non-reflexive NaNs. Padding and inactive union bytes are ignored.
Collision resolution still checks full equality.

Make Set uses the same indexing primitive for expected linear-time ordered
deduplication on supported large inputs. It keeps the first occurrence. Set Add
still copies its result; repeatedly growing a Set remains a potential quadratic
workload. This change does not claim to solve persistent-container copying.

Compiled Set Contains selects the index when present, calling one native C
lookup helper. Flat scalar searches retain their typed SLJIT/bytecode loop.
Index construction and collision probing are native C algorithms with no graph
walks or interpreter callbacks. This is an algorithmic change; moving the same
linear search into a larger JIT function would not remove its scaling cost.

`PERF-CONTAINER` diagnostics link to the source graph, node and pin. They explain
Set storage selection, flag Array searches reachable from Tick (including
linked functions), and warn about repeated Set Add copying. Notes do not block
Play. The existing Compiler Results and runtime/MCP performance diagnostics
expose them.

## Measurement

`container-choice-results.json` contains raw samples, compiler/executable hashes,
lookup medians, index construction time and bytes, and Make Set construction
comparisons. All three compiled backends use the same lowered lookup. Alternating
last-member hits and misses are checked against expected results. Flat and
indexed inputs share identical ordered values. Lookup timing excludes
construction. Make Set includes deduplication and the owned result snapshot;
its flat reference reproduces the previous algorithm. This is a container
benchmark, not an Arena frame-time measurement. Current Arena assets have no
Set nodes.

Reproduce with an optimized, unsanitized build:

```sh
CCACHE_DIR=/tmp/cblueprints-ccache cmake --build build-performance --target test_suite -j 6
python3 tools/bench_containers.py --trials 5 --repeats 5000 --output docs/research/container-choice-results.json
```

The index sidecar uses four bytes per bucket, at a power-of-two capacity of at
least twice the input count. At 20,000 members that is 262,144 bytes, in addition
to the ordered values. Make Set also uses a temporary deduplication index until
the result arena resets. A single lookup may not amortize construction; retained
Sets and repeated queries are the intended beneficiaries.

## Results

Five trials. Median lookup nanoseconds (half last-member hits, half misses):

| Members/type | Flat SLJIT | Indexed SLJIT | Indexed generated C | Indexed bytecode |
| --- | ---: | ---: | ---: | ---: |
| 64 integers | 42.6 | 13.4 | 13.0 | 92.2 |
| 4,096 integers | 2,122.0 | 11.4 | 10.2 | 89.7 |
| 20,000 integers | 10,276.2 | 10.2 | 8.6 | 89.7 |
| 64 strings | 218.6 | 39.2 | 38.8 | 111.8 |
| 4,096 strings | 13,726.8 | 14.0 | 12.2 | 89.7 |
| 20,000 strings | 73,650.6 | 15.8 | 14.2 | 89.7 |

At 4,096 unique members, Make Set construction (including the owned snapshot)
falls from 17.33 ms to 51 µs for integers and 41.92 ms to 152 µs for strings.
Eight-member construction remains approximately 0.09 µs and 0.21 µs; these small
Sets do not build an index. Eight-member lookup timings remain close to flat
storage (9.4/9.8 ns for integers, 30.8/29.6 ns for strings in the two modes).

Building just the 20,000-member sidecar takes a median 127 µs for integers and
287 µs for strings. These cold build samples are noisier than the repeated
lookup timings. Strings use short `member N` values; hash cost also depends on
key length. These numbers are workload measurements, not universal ratios.

## Verification

Native ASan/UBSan and portable bytecode smoke tests pass. The independent flat
equality reference covers every supported scalar kind, duplicates, first-match
order, nominal types, independently allocated strings, signed zero and NaNs.
NaNs retain their ordered positions but are omitted from hash buckets because
they cannot match; repeated NaNs therefore do not create a collision chain.
Owned values survive input-arena release and compiled waits, including their
index. Compiler diagnostic checks retain real graph/node/pin identities.
The full native build/test, web application build and no-JIT rejection checks
also pass. [Tick system loop results](tick-system-loops.md) include the separate
rendered Arena combat/restart comparison.
