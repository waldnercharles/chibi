# Chibi: record declarations and array storage

Research checked 15 October 2026. Imported as supporting analysis; record/storage spellings remain unselected. The owner prefers inferred captures and requested array-like storage. Any reload discussion below concerns live authored state and generation safety, not a request to restore native C hot replacement. This note proposes language design; it does not claim the proposed Chibi forms are implemented.

## Short answer

`Row :: struct { ... }` is the Odin/Jai declaration family. The exact spelling differs: Odin uses `u64` and comma-separated fields; public Jai examples use `u64` and semicolon-separated fields. Thus the analogous Odin example is `Row :: struct { key: u64, label: string }`; the analogous public Jai example is `Row :: struct { key: u64; label: string; }`. Jai evidence here is public example code, not an official released language specification. [Odin structs](https://odin-lang.org/docs/overview/#structs), [Odin basic types](https://odin-lang.org/docs/overview/#basic-types), [Jai public examples](https://github.com/Jai-Community/Jai-Community-Library/wiki/Getting-Started).

For Chibi, keep the existing postfix array style. My preferred proposal is `Row[]` for the ordinary collection, with explicit array-level policies `Row[aos]`, `Row[soa]`, and initially `Row[auto]` for compiler-owned layout. Let indexing stay `rows[i].key`. The compiler can choose a good layout in controlled cases, but cannot promise the fastest layout for every workload.

## What existing languages actually establish

- Odin has built-in `#soa[N]T`, `#soa[]T`, and `#soa[dynamic]T`. Users retain element indexing and can also address columns. Taking a whole element's address produces a special SoA pointer, not an ordinary contiguous-struct pointer. This is the strongest directly comparable precedent for ergonomic, explicit array-level layout. The separate `struct #packed` directive removes padding; it is a different feature. [Official overview](https://odin-lang.org/docs/overview/#soa-data-types), [struct directives](https://odin-lang.org/docs/overview/#struct-directives).
- Jai's original creator demonstrated SoA in 2015. Do not present that historical built-in syntax as a current guarantee. The public community's own example now generates `SOA(T, count)` with reflection and `#insert`. I could verify that example exists, but not validate it against a current official compiler/specification. [Jonathan Blow's historical demo](https://www.youtube.com/watch?v=ZHqFrNyLlpA), [public metaprogramming example](https://github.com/Jai-Community/Jai-Community-Library/wiki/Getting-Started#soa).
- ISPC has built-in `soa<8> Point pts[...]`: blocks of eight field values, with ordinary element indexing. This is an AoSoA-style blocking option. Its `uniform`/`varying` qualifiers describe values shared across or distinct within an execution gang; they are not general instructions to discover the fastest storage layout. SoA objects have explicit pointer/reference limitations. [ISPC guide](https://ispc.github.io/ispc.html#structure-of-array-types), [uniform/varying](https://ispc.github.io/ispc.html#uniform-and-varying-qualifiers).
- Futhark automatically lowers arrays of tuples/records to separate component arrays. Its compiler can also choose multidimensional element ordering based on use, sometimes maintaining more than one representation. This demonstrates genuine separation of logical arrays from physical representation, but does not establish a universally optimal AoS/SoA selector. Its documented entry-point conversions also illustrate that boundaries require a representation contract. [Futhark performance guide](https://futhark.readthedocs.io/en/latest/performance.html#value-representation).
- Julia's StructArrays is a library, not special language syntax. It presents an array of logical records over separate field arrays. Its `LazyRow` wrapper demonstrates how row-like access and mutation can be implemented without materializing a complete row. Converting an ordinary AoS array to a StructArray copies the data. [Library documentation](https://juliaarrays.github.io/StructArrays.jl/stable/).

## Chibi's actual syntax baseline

The inspected source is main commit `02f8453211dd4cc57965ba52c645f08a92e53c5e`, grammar blob `ba6b8d5b6bb31dac14c5fa778e3e79a000453531`.

- Type grammar uses postfix `T[]` and `?`, after an optional generic type argument list.
- It already has prefix traits such as `plain` and `ref`.
- Generic arguments currently parse as types, not arbitrary numeric/policy expressions.
- Array value literals use `[a, b]` and `[]`.
- The checked grammar does not yet define the proposed `Name :: struct` declaration. Its `struct_expression` production is a brace-field value expression; that is not proof of native record layout semantics.
- This is syntax evidence only. It does not establish whether current runtime arrays are unboxed, reference-based, contiguous, or layout-independent.

[Type grammar, lines 345–370](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/grammar.js#L345-L370), [literal grammar](https://github.com/waldnercharles/chibi/blob/02f8453211dd4cc57965ba52c645f08a92e53c5e/src/language/grammar.js#L633-L637).

## Three syntax options

All forms below are proposals, not working Chibi examples.

### 1. Layout inside the existing array brackets — preferred

```text
rows: Row[] = []
rows: Row[aos] = []
rows: Row[soa] = []
rows: Row[auto] = []
```

The layout belongs to a particular array dimension. `Row` remains the logical record schema. This gives the requested array-like feel and an explicit place for future policies. Array literal syntax and row access need not change.

Initially preserve the existing meaning of `Row[]` and introduce `Row[auto]` only for new, compiler-owned storage. Making bare `Row[]` automatic later is reasonable if its established semantics expose no physical-layout promises. This avoids accidentally changing object identity or reference behavior under the banner of an optimization.

This requires grammar and semantics work. It is not already accepted merely because empty `[]` is accepted. A future `Row[soa(8)]` or other tiled policy would need a deliberate additional grammar decision, not an assumption that current generic numeric arguments exist.

### 2. Prefix qualifier — closest to Odin and current Chibi traits

```text
rows: aos Row[] = []
rows: soa Row[] = []
rows: auto Row[] = []
```

Readable and consistent with the existing prefix-trait family. The downside is binding: for `soa Row[][]`, which dimension receives the layout? The specification must answer this, while bracket-local policies make it visually explicit. It must also be clear that `soa Row[]` qualifies the collection rather than creating a new scalar record type.

### 3. Generic storage type — conservative surface syntax

```text
rows: Array<Row, SoA> = []
rows: Array<Row, AoS> = []
rows: Array<Row, AutoLayout> = []
```

This uses the shape of Chibi's existing generic type grammar. It does not imply that an `Array` type or these policy types exist today, or that an empty literal will already coerce into them. It is extensible and library-friendly, but heavier than the requested ordinary-array feel. It could be the compiler's internal normalized representation even if the public syntax is option 1.

## Keep four separate decisions separate

1. Logical type: a row has `key` and `label`.
2. Value/reference model: are rows values, handles, or identity-bearing objects?
3. Collection layout: AoS, SoA, AoSoA, or implementation-selected.
4. Byte encoding/alignment: native aligned fields, bit widths, compression, serialization.

Calling all four “packed” would obscure important promises. Dense storage should normally retain suitable alignment. SoA separates the fields of many rows; it does not imply unaligned byte packing. Nor does a SoA string column automatically inline every string's character data: string descriptors/handles and character storage are separate design choices.

## Automatic choice: first prove it is legal, then estimate whether it helps

The following are design recommendations, not claims that Chibi implements these analyses.

### Legality constraints

- Aliasing and escape: all accesses to an automatically laid-out collection must stay consistent. Unknown native code, escaped raw pointers, or independently compiled consumers can invalidate a rewrite. Abstract handles can escape safely only if every consumer respects their abstraction.
- Addressability: a logical SoA row is scattered across columns. Do not return an ordinary `Row*` that implies contiguous fields. Use a row proxy/borrow, prohibit that address operation, or materialize explicitly with defined copy-back rules.
- Identity and mutation: decide whether `x = rows[i]` produces a copy or a mutable row view. `rows[i].key = k`, row replacement, sorting, insertion, deletion, growth, and views must preserve the same logical record. Structural changes must invalidate or update borrows predictably.
- ABI/FFI: lock representation at exported boundaries or perform explicit conversions. Layout changes cannot silently change a C-facing struct, buffer stride, or pointer contract.
- Reflection: expose logical schema separately from physical offsets. If callers can observe physical size, alignment, or field offsets, those observations constrain transformation.
- Serialization: serialize schema-defined values, not an incidental memory dump. Existing raw byte formats require explicit stable encodings or migrations.
- Hot reload: live state cannot simply be reinterpreted with a new layout. Pin storage for the instance's lifetime, or use versioned migration at a safe point while accounting for active borrows and old code.
- Concurrency and observable behavior: preserve ordering, synchronization, atomics, and alias relationships. Layout optimization must not silently change value semantics or failure behavior.

Odin's specialized row pointers and ISPC's restrictions are concrete reminders that row-like source syntax does not make physical representation differences disappear.

### Profitability constraints

The compiler needs to know which fields are used together, scan versus random access, collection size, update frequency, hot versus cold fields, target cache/SIMD behavior, and the cost of creating or converting storage. SoA often helps scans over a subset of fields. Whole-record/random access and frequent whole-row operations can favor AoS. AoSoA may suit bounded vector-width processing.

Benchmark the whole workload: a faster scan can lose overall if construction, mutation, sorting, or conversion dominates. Sparse index access can still require gather/scatter even with SoA. Layout alone neither guarantees vectorization nor removes loop-carried dependencies. LLVM explicitly uses target-aware cost models and may reject gather/scatter vectorization as unprofitable. [LLVM vectorizer](https://llvm.org/docs/Vectorizers.html#scatter-gather), [ISPC performance guide](https://ispc.github.io/perfguide.html#use-structure-of-arrays-layout-when-possible).

Do not assume LLVM will automatically redesign arbitrary heap collections. Its documented SROA pass splits eligible aggregate stack allocations; this is a more restricted optimization. [LLVM SROA](https://llvm.org/docs/Passes.html#sroa-scalar-replacement-of-aggregates).

## Recommended sequence

1. Specify record value semantics, row reads/writes, borrows, and the stable external representation first.
2. Start with internal compiler-owned query intermediates and non-escaping collections, where storage is already hidden. Preserve existing user-visible array behavior.
3. Implement explicit AoS and SoA policies with identical logical operations and differential tests. Keep conversion visible and prohibit surprise conversion on ordinary function calls.
4. Give automatic storage a conservative fallback and compiler diagnostics explaining the selected layout and the reason. Keep choices stable for a live allocation unless an explicit migration boundary exists.
5. Add workload benchmarks and later profile-guided decisions where the maintenance cost is justified. Test scan-only, whole-row, random lookup, append/delete, sorting, small collections, strings/references, and real mixed workloads.

The practical promise should be: “write ordinary row code; the compiler may choose a safe efficient representation, and you can inspect or override it.” It should not be: “the compiler always infers the best layout.”
