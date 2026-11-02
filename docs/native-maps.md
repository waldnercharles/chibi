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
```

`count` is the live entries, `used` the entries array's length including dead ones (`used - count` dead), `capacity` its
room, `slots` a power of two with `capacity * 4 <= slots * 3` (set whenever the index is built; 0 without one). The zero map
is all zero and owns nothing. Capacity starts at 4 and becomes `max(4, 2 * count)` when the entries are full.

Byte budget (AGENTS.md "Know the data"): the header is 32 bytes. An entry costs `key + value + 4` bytes rounded to its
alignment. The index costs 8 bytes per slot: between 4/3 and 8/3 slots per unit of capacity, so 10.7 to 21.3 bytes per
entry of capacity, and about 16 bytes per live entry in a map that has just grown.

## Bounds

- At most 2^30 entries (`count`, `used` and slot numbers are 32 bits; the slot count must stay a 32-bit power of two).
  Growth past it ends the call (to do: the `MapFull` failure).
- Insert O(1) amortized (doubling); lookup O(1) expected, with the index at most 75% occupied by live entries and
  tombstones together (`used <= capacity`); replace O(1); delete O(1) amortized above 8 entries and O(8) below; copy and
  equality O(n). Iteration visits `used` positions, at most 1.5 times the live entries after any delete.
- Allocation failure aborts, as everywhere.

## Keys and values

Keys: ints of every width, bool, string; floats are not keys (NaN). Values in this slice: scalars and strings. Records and arrays as values
(`[string: int[]]`) need recursive value drop and copy before the native lane admits them. A map owns its strings:
`MapSet` consumes one reference of a string key and of a string value (a repeated key's new key is released, the replaced
value is released), `MapDelete` releases the entry's strings, `MapCopy` retains them, `MapDrop` releases them (D10).
A read of a missing key gives the value type's zero; reads borrow. Writing through a missing key (`m[a][b] = v`) ends the call.

## Plan and state

Implemented on `codex/native-maps`, still unmerged:
- The 32-byte compact header and live-entry hashes, Robin Hood index, amortized constant-time deletion and stable insertion order.
- Native ABI and map instructions, including `MapSpan` and `MapLiveAt` for iteration over live positions. Key/value position reads reject dead entries.
- `scalar_facts.MapRow` and `scalar_llvm` validation/lowering for literals, reads, changes, copies and drops. Scalar and string keys/values have explicit reference ownership; copies own independent entries and preserve live order.
- C-free behavioral tests in `test/native_aot/maps`. They compile the actual emitter and lower actual fact tables, then run both O0 and O2 programs. Cases cover 64-entry churn against ordered lists, deletion/reinsertion, copies, binary string keys and values, balanced allocations, dead positions and malformed facts.

The authored producer admits typed literals, variable copies, indexing, assignment, `in`/`not in`, `has`, deletion, parameters and results. Read-only parameters borrow; changed by-value parameters receive a private copy. `test/native_aot/map_programs` runs these as real source programs, including reference replacement and a string read from a temporary returned map. Compound writes reject with a diagnostic until their lowering exists.

Gate 7 remains open for authored map iteration, equality, compound writes, inferred map literals, records, nested containers and cross-platform runtime coverage. The VM must remain until those cases have native equivalents.
