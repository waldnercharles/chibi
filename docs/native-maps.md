# Native maps

Gate 7 of the VM deletion (handoffs/vm-deletion.md): `[K: V]` compiles natively. The semantics are the language's
(docs/code-language.md, "Arrays and maps"); this note fixes the native representation, what it takes from Odin,
and the bounds. The compiler uses no map, but tools and games do, and the VM cannot go before they can.

## Odin's map, from base/runtime/dynamic_map_internal.odin

- `Raw_Map { data: uintptr, len: uint, allocator }`. `data` is a tagged pointer: the low six bits are the log2 capacity.
- One allocation holds, in order, the keys, the values and the hashes (one `Map_Hash` per slot), then two scratch keys and
  two scratch values for insertion. Each section is rounded to a 64-byte cache line, and a `Map_Cell` packs as many keys
  or values as fit in a line without straddling it.
- Open addressing with Robin Hood hashing and linear probing: an inserted element swaps with a resident whose probe
  distance is shorter, and a lookup stops once its distance exceeds the slot's.
- Minimum capacity 8 (`MAP_MIN_LOG2_CAPACITY :: 3`), load factor 75 (`MAP_LOAD_FACTOR`), growth doubles.
- Hash `0` is empty; the top bit marks a tombstone; a valid hash is non-zero with the top bit clear. The default hasher is
  derived from FNV-64a with those two modifications.
- Keys and values live in the slots, so iteration visits slots in table order, not insertion order.

## How Chibi's differs

1. **Dense entries in insertion order, with an index over them.** The language promises insertion-order iteration, that
   replacing a value keeps the key's position, and constant-time lookup. The entries are a dense array and the Robin Hood
   table is only an index: each slot names an entry (the VM's `pin_map` layout, src/types/map.c, which already cites Odin).
2. **Delete is amortized O(1), as CPython's compact dict does it** (decided with the coordinator, 2026-11-01; the first
   draft shifted the later entries, which made a loop of deletes O(n^2)). Above 8 entries a delete releases the entry's
   strings and marks the entry dead (its stored hash becomes 0); the index slot keeps pointing at it and acts as a
   tombstone, so the probe distances stay valid. When dead entries outnumber half the live ones, the map compacts in
   place: live entries move down in order. Logical capacity shrinks toward twice the live count, and only the corresponding index prefix is cleared and refilled from stored hashes. The existing buffers stay allocated until growth or drop. Growth compacts as it copies.
   At 8 entries or fewer there is no index and a delete shifts the later entries (at most 7). Insertion order survives
   both.
3. **An entry is `{ key, value, i32 hash }`**, the hash never 0 (a 0 hash becomes 1) and 0 meaning dead. A slot is `{ u32 hash,
   u32 entry + 1 }`, 0 meaning empty, so a zeroed allocation is an empty index. Probes compare the slot's hash first; the
   index is rebuilt from the entries' stored hashes and never hashes a key again. No hash value is reserved (Odin reserves two).
4. **A small map has no index.** Up to `MapScanned = 8` entries (the VM's threshold) a scan comparing stored hashes beats a probe.
   The index exists exactly when `used > 8`.
5. **Hash**, identical on every target: a key's 64-bit value (an integer or bool zero-extended from its width; a string by
   FNV-1a 64 over its bytes, offset basis 0xcbf29ce484222325, prime 0x100000001b3) goes through the splitmix64 finalizer
   (shifts 30, 27, 31; multipliers 0xbf58476d1ce4e5b9, 0x94d049bb133111eb), truncated to 32 bits. There is no per-process
   seed: builds and runs must be reproducible, so a program fed attacker-chosen string keys can be driven into long probe
   chains, as Odin's can. Iteration order is insertion order, so the hash is invisible to programs.
6. **Header and two buffers.** Odin packs everything into one cache-line-padded allocation. A native map's entries and index
   have different growth, so they are separate allocations, freed together at the map's drop.

## Representation

```
%map   = { i32 count, i32 used, i32 capacity, i32 slots, ptr entries, ptr index }   ; 32 bytes, align 8 (abi.MapType)
entry  = { key, value, i32 hash }          ; used entries, in insertion order; hash 0 = dead
slot   = { i32 hash, i32 entry + 1 }       ; 8 bytes; `slots` of them, 0 entry = empty
owner  = { i64 references, [32 x i8] release_scratch } ; 40 bytes immediately before entries
```

`count` is the live entries, `used` the entries array's length including dead ones (`used - count` dead), `capacity` its
room, `slots` a power of two with `capacity * 4 <= slots * 3` (set whenever the index is built; 0 without one). The zero map
is all zero and owns nothing. Capacity starts at 4 and becomes `max(4, 2 * count)` when the entries are full.
The entries owner also owns the index. Native `ValueCopy` shares this owner in O(1); mutation detaches shared entries.
Native `MapCopy` preserves the raw API's independent-entry allocation contract, copying only immediate entries and
sharing their counted children. Its cost is O(n). Authored copies share the entries owner; source mutation and
foreign-loan routes resolve the canonical owning place before detaching.
Nonzero map descriptors must originate from map operations or counted copies. The native API does not import
ownerless external map buffers; map parameters carry this same internal ownership contract.

On the last drop, the owner prefix becomes an existing value-release queue node: next pointer at offset0, context at offset8,
typed release callback at offset16, span at offset24 and index pointer at offset32. Nested teardown therefore uses one iterative drain,
with no release-queue allocation or recursion through child containers.

Byte budget (AGENTS.md "Know the data"): the header is 32 bytes. An entry costs `key + value + 4` bytes rounded to its
alignment. Each allocated entries buffer adds the 40-byte owner prefix. The index costs 8 bytes per slot: between 4/3 and 8/3 slots per unit of capacity, so 10.7 to 21.3 bytes per
entry of capacity, and about 16 bytes per live entry in a map that has just grown.

## Bounds

- At most 2^30 entries (`count`, `used` and slot numbers are 32 bits; the slot count must stay a 32-bit power of two).
  Growth past it ends the call (to do: the `MapFull` failure).
- Insert O(1) amortized (doubling); lookup O(1) expected, with the index at most 75% occupied by live entries and
  tombstones together (`used <= capacity`); replace O(1); delete O(1) amortized above 8 entries and O(8) below; source copy O(1), with O(n) detachment on its first write. Explicit native `MapCopy` is O(n). Iteration visits `used` positions, at most 1.5 times the live entries after any delete.
- Allocation failure aborts, as everywhere.

## Keys and values

Keys: ints of every width, bool, string; floats are not keys (NaN). Authored values include scalars, strings, supported pointer-free arrays and records or authored unions with native value layouts. Aggregate values use the shared value copy/drop helpers.
Nested arrays of maps, map-valued map payloads and maps of arrays of maps use
the ordinary collection identity. A one-field physical record carries each
immediate descriptor, so map type encoding never recursively packs another map
type. The shared value graph selects each reachable record, array and map helper
once. Authored record fields containing maps remain rejected by the ordinary
checker; synthetic collection carriers do not change that rule.
Array payloads use the shared array lifecycle. Reads acquire an owned descriptor; source map copies share entries, and projected writes detach shared entries and array backing after their callback arguments finish. Other nested containers remain gated by their type and ownership proofs. A map owns its strings:
`MapSet` consumes one reference of a string key and of a string value (a repeated key's new key is released, the replaced
value is released), `MapDelete` releases the entry's strings, `MapCopy` retains them, `MapDrop` releases them (D10).
A read of a missing scalar or string key gives the value type's zero; a native int[] read gives an empty array. A missing record key fails with `MapKeyAbsent`.
Reads borrow. Writing through a missing key (`m[a][b] = v`) ends the call.

Native `MapValueAddress` (75) borrows an existing descriptor entry. Target 0 observes an array entry. Target 1 also projects a one-map-field carrier and requires a reference
to the canonical map descriptor, makes shared entries unique and publishes that descriptor before returning the entry
address. Resolve it after argument callbacks and immediately update the array descriptor with `Poke`; replacing,
growing or dropping the map invalidates the address. A missing entry produces an unnamed failure, matching the ordinary
mutation-place failure rather than a typed `MapKeyAbsent`. This address cannot be retained across later foreign arguments.

## Plan and state

Implemented:
- The 32-byte compact header and live-entry hashes, Robin Hood index, amortized constant-time deletion and stable insertion order.
- Native ABI and map instructions, including `MapSpan` and `MapLiveAt` for iteration over live positions. Key/value position reads reject dead entries.
- `scalar_facts.MapRow` and `scalar_llvm` validation/lowering for literals, reads, changes, copies and drops. Scalar and string keys/values have explicit reference ownership; source copies share counted entries and preserve live order. Explicit native `MapCopy` clones the immediate entries.
- C-free behavioral tests in `test/native_aot/maps`. They compile the actual emitter and lower actual fact tables, then run both O0 and O2 programs. Cases cover 64-entry churn against ordered lists, deletion/reinsertion, copies, binary string keys and values, balanced allocations, dead positions and malformed facts.

The authored producer admits typed literals, variable copies, indexing, assignment, `in`/`not in`, `has`, deletion, parameters, results and insertion-order iteration. Read-only parameters borrow; changed by-value parameters receive a private copy. Iteration snapshots a map when the body can change it. `test/native_aot/map_programs` runs these as real source programs, including reference replacement, numeric record values and string reads from temporary returned maps.

Projected array updates support append, extend, remove_index, resize and element assignment. Numeric array entries can also supply writable foreign buffers. A foreign loan captures the evaluated array and immutable key before later arguments run. Before C executes, it compares the captured descriptor with the current entry. Matching entries detach through the canonical map reference; replaced or deleted entries keep an independent historical loan. Missing value-read keys supply an empty array without adding a key. Repeated arguments finish all comparisons before any detachment. String keys remain owned until final lookup completes.

`managed_values/MapCopies.chibi` checks zero backing allocations for 4,096 readonly source copies, then each of the five mapped-array mutations from a newly shared copy with an unchanged original. `map_programs/ForeignLoans.chibi` covers scoped writes, by-value isolation, repeated buffers, key callbacks, map growth, replacement and deletion with balanced allocations at O0/O2.

Numeric mapped-array compound assignments save the key, index and old leaf before the RHS. Bracket writes check the initial place before the RHS and resolve its current entry again afterward. Both unavailable-place checks stay unclassified; ordinary expression indexing still reports `IndexOutOfRange`. The `array_places` fixtures keep that distinction and the different post-argument lookup order of methods. Other nested containers and cross-platform runtime coverage have separate gates. Map equality and inferred map literal types retain the ordinary authored compiler's limitations. Host and VM retirement still requires migrating their remaining consumers.

`NestedContainers` covers nested literals, value reads, arguments and returns
with poisoned frees and zero outstanding allocations. `NestedMapCopies` requires
4,096 nested copies and payload reads to allocate nothing; changing one leaf
detaches each shared map once. Canonical nested set/delete and numeric compound writes capture their keys and
old leaf before the RHS, then resolve and publish the current owner.
An inner map read from a missing key can be changed independently without adding
an outer key, following the zero-map rule in `docs/code-language.md`. The recovered
ordinary runtime instead fails that mutation with `Cannot retain Map update`;
that discrepancy is recorded rather than used as the expected native result.

`NestedPlaces` checks index/key/RHS order, root replacement by any of them,
missing-owner repair, scoped aliases, deletion and independently owned text,
array, record, optional and map payloads. Its unchanged assertions also pass the
recovered ordinary compiler. Six abrupt-failure controls distinguish RHS
assertions from a missing final owner and preserve the unclassified category.
`NestedMapCopies` additionally requires exactly two detach allocations for the
first shared nested write and zero allocations for 4,096 further unique writes.

Canonical map writes also support deeper mixed index paths rooted at a map or
array variable. Facts 20–23 carry every captured key in source order; validation
follows the existing descriptor/carrier type graph before lowering any reads.
Lowering resolves and publishes each current owner through the existing map or
array address helper, then mutates the leaf map. It copies no whole root and
retains no interior address across authored callbacks. Intermediate old-value
reads close before the RHS, including when an absent owner is repaired by it.

`DeepPlaces` covers three-level map and mixed map/array paths, multiline source,
owned leaves and a 32-level runtime path. `DeepMapCopies` requires exactly four
allocations for the first shared write through four immediate owners, then zero
allocations for 4,096 unique compound writes, for both all-map and mixed paths.
The producer growth check emits 126,910 / 251,046 / 499,786 / 998,374 bytes at map
depths 4 / 8 / 16 / 32, with exactly depth minus one canonical projections.
Malformed variable arities and intermediate non-carrier records remain rejected.

Mapped-array bracket writes also follow deeper typed paths through maps and
arrays. Operator 15 preserves its existing four-operand path and accepts longer
captured-key paths that cross a map and end at an array. The complete initial
place is checked before the RHS; unlike a map leaf, a missing array place cannot
be repaired by that RHS. The final store resolves the current owners again.
`DeepArrays` checks this distinction, key callbacks, scoped aliases and owned
record/map elements. `DeepArrayCopies` requires exactly three detach allocations
for shared map/map/array and array/map/array paths, then zero allocations across
4,096 further unique writes. Bounds and owner failures remain unclassified.

The mapped-array methods append, extend, remove_index and resize use the same
long typed paths. Their existing three-operand forms keep their original path.
Keys are captured once, arguments finish before canonical lookup, and no initial
bounds read is inserted for a method. Extend holds its argument descriptor
through publication, including self-extension; resize publishes its independent
result before releasing the previous leaf. Method path keys are borrowed.
`DeepMethods` covers root replacement, binary keys, owned records and missing
owners. Growing an authored-record array without initialized rows remains a
failure; shrinking is covered. `DeepMethodCopies` checks three detach allocations
for shared append, self-extend and resize, with zero allocations for 4,096
append/remove cycles that stay within the leaf array's capacity.

Map-valued map traversal uses the same per-turn physical carrier projection as
nested array traversal. Keyed loops keep the key ordinal and project only the
value. The logical binding owns one immediate map reference until that turn
exits; the receiver snapshot preserves insertion order when the source changes.
`NestedIteration` checks nested loops, temporary receivers, binary string keys,
source replacement, copied values and break/continue/return cleanup at O0/O2
under poisoned allocation. Direct mutation of the readonly binding remains
rejected. `NestedIterationCopies` requires zero allocations across 4,096 keyed
and unkeyed passes.

The original `TaskMaps` source is now an O0/O2 positive after counted map promises
and collector-copyable record arrays composed. Its direct and observed joins
keep independent array-of-map values. The task-owner audit preserves the real
execution context and checks poisoned frees and zero live allocations.
