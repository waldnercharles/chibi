# Typed-module scalar transport bindings

Status: version 3 adds explicit foreign membership and integer equality. The
current complete typed-source, source-to-v3-Pack, eight-test Arena framing and
seven-test Rust backend suites pass, as does fresh non-test library review.
The [original-source foreign proof](../../slice_backend/README.md#version-3-foreign-native-result)
passed compilation, normal JIT, owned environment failure, C link and linked C
execution, followed by independent aggregate acceptance. The four older
native selections are not yet accepted under current version 3. Historical
version-1/2 receipts do not establish full current-v3 corpus or throughput
acceptance.

Historical version-2 scalar/direct-call transport passed the native Chibi packing
and seven standalone Rust framing tests in the [six-stage pure-source proof](../typed/README.md#accepted-version-2-pure-transform-result).
The separate Rust backend proof and three exact-source private native proofs
also passed, including JIT/environment-failure/linked C execution. The later
[package-aware checkpoint](../../slice_backend/README.md#package-aware-checkpoint)
passed all four cases through linked C execution and independent full-chain
acceptance. The historical
version-1
checkpoint passed nine Python generator controls, native Chibi Produce-to-Pack
and six generated Rust view/framing tests. Its separately reviewed
[source-native proof](../../slice_backend/README.md#accepted-bounded-result)
passed 1,080 JIT assertions and 45 linked C checks, followed by the public
five-stage reproduction with the final-Cargo library. Those results do not
validate version 2.

This is a temporary, synchronous, same-process transport for the real
`typed.Module`. It does not parse user programs, evaluate an IR, serialize a
persistent file or supply a backend. The same Module and buffers feed the
separate cdylib's eager JIT and Object consumers.

## Source authority and generation

`../typed/Model.chibi` is the only schema, constant and kind authority. Its four
nonsemantic marker comments identify the transport version and the two enum
groups. `generate.py` recognizes only this deliberately restricted declaration
shape: the exact package/import, ordered public nonnegative u32/int constants
(decimal or an earlier same-type constant plus a decimal), nonempty u32-field
records, one Module of arrays of those records/u32 plus i64 literals/u8 names,
and the exact existing Result union. Each record backs one table. Unsupported
syntax, pointers, nested fields, arrays in rows, aliases, unknown types,
forward references, duplicate names, misplaced/empty groups and other shape
changes fail closed. It never executes source expressions or input code.

The reviewed version-to-layout SHA-256 is a cold build-time tripwire, not a
second schema or a runtime hash. It covers ordered constants, record fields,
Module tables and enum memberships. All constants, including bounded-slice
limits, participate conservatively. Semantic/order changes require bumping the
Model comment and reviewing a new digest in `VERSION_LAYOUTS`; do not replace
the old version's digest to excuse drift. Whitespace and ordinary comments do
not affect artifacts. The initial v1 digest is
`3c80038a06b4ae925e60658a0506a816e8177630b6744367949e1f95d6b6a4bb`.
The version-2 digest is
`9812f63cd2c2d7786cc536d5aed57665c4ab8c75327aaa744e25533039f2a3b1`.
It appends opcodes 6–10 and the two-word Call table without changing the exported
C signatures or literal/name side buffers. Version 3 has digest
`886b15e405bde5a0cb4223fc8986022797bd38d67946efaa3d6fa43c2fcc3ec6`.
It adds EqualI64 and a two-word Foreign table after Functions. Versions 1 and 2
are rejected rather than translated; pair the current Pack with a version-3
backend. Its C compile entry point additionally takes an explicit foreign-binding
pointer/count pair, described by `../../slice_backend/foreign.h`. The descriptors
are separate invocation metadata, not pointers embedded in the scalar frame.

From the repository root:

```sh
python3 -I -B tools/experiments/vnext/frontend/bridge/generate.py
python3 -I -B tools/experiments/vnext/frontend/bridge/generate.py --check
python3 -I -B tools/experiments/vnext/frontend/bridge/generate_tests.py -v
```

The first command writes only `generated/Pack.chibi` and `generated/arena.rs`.
The check command never rewrites stale outputs. No package installation,
compiler invocation, source discovery or user-program interpretation occurs.

## One pack, three synchronous borrows

The ordinary Chibi call is `buffers := bridge.Pack(module)`, returning
`Buffers` directly. The caller passes
`buffers.words`, `buffers.literals`, and `buffers.name_bytes` to its synchronous
C entry point. `generated/Pack.chibi` is package `vnext_bridge` and imports the
existing `project:vnext_typed` package. It does not call `Verify` again. The
producer has already asserted its semantic invariants.

Pack creates one exactly sized logical u32 destination using resize, then
writes each source scalar to it once. The resize initializes storage before
those writes, a deliberate cost of the current array API. This is one explicit
flattening copy. Literals and names preserve the source arrays' COW references;
there are no explicit copies of either side array. Source and returned arrays
remain invocation-owned and reachable for the entire foreign call. The adapter
may borrow them only until the call returns, never retain their pointers in an
IR or result owner. The separate native packing and source-native proofs
exercise scalar-array transport and copy-out/COW isolation; actual native
record layout, allocation/COW costs and conversion traffic remain unmeasured.

All counts are element counts. The word buffer starts with version followed by
13 row counts, in Module declaration order with literals/name_bytes omitted:
functions, foreigns, signatures, parameter_types, blocks, block_parameters, values,
instructions, edges, calls, arguments, origins, names. This is 14 u32 words /
56 bytes. Rows follow in that same order, fields in declaration order. Widths
are 6, 2, 3, 1, 4, 1, 2, 6, 3, 2, 1, 3, 2 words. Call stores callee FunctionID and
first argument offset; its arity comes only from the resolved Signature. Calls
and edges own disjoint spans of the same arguments table. Literal and name
lengths come from their independent C pointer/count pairs. Native byte order is intentional: these are
same-process scalar arrays, not an endian-neutral on-disk representation.

The Chibi length domain is signed 32-bit. Pack checks each count against the
remaining word capacity before multiplication/addition, calculates in i64 and
narrows only after the bounds are proved. These are internal assertions, not
source-input validation: Pack accepts only produced/verified Modules. The
current conservative admission bound is 1,058,286 words, below the signed
32-bit limit. A violated capacity invariant stops before allocation; allocation
failure remains the runtime's abort contract. An all-zero empty Module packs to
version 3 plus thirteen zero counts and empty side arrays; zero and versions 1/2
are not accepted transport versions.

## Rust consumer contract

Include `generated/arena.rs`; do not copy it into another hand-maintained schema.
`Arena::from_parts(words, literals, name_bytes)` checks version, minimum header,
count limits, checked multiplication/addition and **exact** word length before
slicing. It borrows fixed-width row chunks and scalar slices without allocating,
transmuting, relying on authored-record memory layout, or copying type facts.
Field offsets such as `VALUE_TYPE` and `INSTRUCTION_OP` and all Model constants
are generated. Value remains the only owner of a value's type.

`ScalarType::decode` and `Opcode::decode` reject unknown numbers and return
closed Rust enums. Consumers must match their variants exhaustively so a
reviewed new kind makes missing lowering explicit. `span(start, count, total)`
checks bounded indexing and canonical empty spans: count zero requires start
zero, including in a nonempty table. IDs remain one-based with zero absent.

These safe views cannot prove arbitrary raw C pointers are valid. The C wrapper
must first check null/nonzero-count pairs, alignment, element/byte limits and
synchronous lifetime before making Rust slices. Null with zero count must
become an empty slice without calling `slice::from_raw_parts` on null. Any
failure in external framing/counts is a diagnostic/status, not a panic across
C. Pointer validity/accessible allocation is still the caller's unsafe contract.
Framing does not establish semantic IR validity. The backend accepts only an
already-verified same-process Module whose producer ran Chibi Verify, not arbitrary
unverified frames or external serialized IR. Lowering retains checked ID/span
access, admission bounds, kind decoding and local definition assertions where
used. It does not repeat semantic verification or reconstruct its scratch
arrays. Internal producer invariants remain assertions; source errors remain
producer diagnostics.

## Tests and accounting

The pre-package version-2 Python controls passed all 93 tests: 21 native-preparation,
21 runner (including all four real-signal controls), 10 generator, 29 frontend
and 12 typed-preparation tests. This is separate from the historical 54-test
version-1 control run below. Generator freshness also passed. Package changes
separately passed 25 native-preparation tests and the four affected runner
source-admission controls, including the diagnostic correction. This does not
claim a fresh whole-run or full runner/signal-control campaign.

Python checks cover malformed schema and markers, same-version kind/field/table
order drift, stale artifacts, deterministic generation, an independent fixture
layout oracle, scalar round trips with full-width values, empty frames and
bad version/count/length/arithmetic cases. Their reference pack/view are
**test-only scalar transforms**, not an alternative compiler or runtime reader.
They do not prove the generated languages compile or execute.

`tests/Main.chibi.in` is a prepared real-producer caller: substitute the exact
UTF-8 `../typed/fixtures/EchoJoin.chibi` at its single `FIXTURE_SOURCE` marker and
map `project:vnext_bridge` to `generated/Pack.chibi` alongside the normal typed
producer closure. It compares every scalar in every row against an independent,
handwritten order, all literals/names, fixed fixture counts/offsets, repeat
packing, an all-empty Module, and a real renamed/+7 source variant. Before the
first Pack it freezes an independent word oracle and copies each literal/name
value into separate arrays. Both the source and every packed result are checked
against that frozen oracle. It then mutates an ordinary local copy of the first
result's literal and name arrays separately, checks the source and both retained
results after each mutation, and repacks the unchanged source. These reference
copies and COW mutation checks are test-only costs. `tests/arena_tests.rs` is a standalone Rust test
caller for zero-copy row/side-array views, empty and malformed frames, canonical
spans and exhaustive kind decoding; it has no backend dependencies. Its bounded
replay now covers all thirteen count positions with zero, one, MAX_ELEMENTS and
MAX_ELEMENTS+1, every short prefix of small frames, and extra words. Large-count
rejections use actual short headers with safe Arena::from_parts calls, never
large allocations or fabricated pointers. All seven version-2 Rust tests passed
unfiltered, and the version-2 Chibi template passed compilation and execution.
The Chibi template also covers nested direct calls, outer argument reservation
and mutation of a copied Module call row while the source and packed result
remain live. Its 32 textual assert sites are not a runtime assertion count.
The [pure-source result](../typed/README.md#accepted-version-2-pure-transform-result)
records the shared six-stage readback; no backend is loaded by these tests.

### Historical version-1 packing and framing proofs

All six Rust tests passed without filtering. The Rust framing proof used the
official Rust 1.99.0 toolchain, GCC 14 and LLVM 22.1.8 lld. Its fresh input fence SHA-256 is
`6e58a2ff592b8dbb5e7c00c378306a2e3f9e3b663d32d454f82deb7f9f22e90f`;
build receipt `a1d008819305436f8f8eacfa05a2f8b4000e1ef3391f9bdd766861da5cee5ac7`;
test receipt `5151e6df03646917e4aabb90fb6f8900092b481bb2733e82e631483be1279e63`.
The test executable SHA-256 is
`c11ea036ed8081360b08a30ef3f9b6b591efadff56bbd213daf95e2fce8f8d94`.
The exact linker inputs and recursive runtime dependencies were independently
reviewed before execution; source/tool and child cleanup checks passed.
An earlier build is retained but was not executed because its prelaunch fence
omitted the PIE startup object. The fresh proof explicitly includes that input.
These are framing tests, not Chibi/Rust interoperability or backend acceptance.

The 2026-11-04 native proof compiled and ran the exact retained parser,
signature reader, typed producer and generated Pack with the pinned c5 seed
(`1d1d289a2429fe2ee2a75a71a959bf21590a02ff3ebf7cd8af1037a11bf1268e`)
and LLVM 22.1.8. Every native assertion passed with empty stdout/stderr and
clean source/tool and child-process checks. Its manifest SHA-256 is
`c4425edbfc7e4ea9fcee2356f94068c073fbbee19b3a383f12d3785b7b3594ee`;
receipt `646c6e3b3228369882b81feaffbdc21280b2f939041375cec8bbe8846d464b5a`;
executable `3d08e042c7e9715db66afab8218a93cf3bd0b1b3229b8eae305d71d83fe5cb7b`.
Two earlier compile failures remain recorded: mutation of a read-only pattern
binding, then unqualified generated union constructors. The final fixture uses
a writable local copy and generated constructors explicitly name Result.
No current compiler admission, Rust interoperability, Cranelift execution or
compilation-throughput result is claimed by this packing proof.

## Source-native correctness reproduction

`tests/NativeCases.chibi` is actual source input to Produce, not a handwritten
IR recipe. `tests/NativeMain.chibi.in` calls Produce/Verify, Pack and the backend,
then checks JIT results, owner lifetime, copy-out and an owned environment
failure. `tests/native_aot_caller.c` independently checks the copied object.
The [backend README](../../slice_backend/README.md) records current foreign-call
work, the historical version-1/2 results, the ABI contract
and remaining gates.

`prepare_native.py --case nativecases` remains the default historical regression.
The historical scalar/direct-call/package selections use the same `../../slice_cases.json` registry:

- `--case scalars`: the original 15 scalar calls and nine applicable source
  rejections, each a separate Produce invocation with category/location checks.
- `--case direct_calls`: 23 supplementary call-ABI/nesting/recursion oracles,
  including source-produced true/false literals and opposite-sign/equal comparisons, without repeating the registry
  rejections. These are tiny terminating
  recursive tests, not recursion-depth or stack-exhaustion qualification.
- `--case modules`: the two original Apply expectations and the separate
  original `private_import` rejection, with exact source location checks.

`registry_callers.py` generates Chibi signature checks and independent C
declarations/assertions from exact JSON integer/bool expectations. It never
parses source or evaluates Chibi. Preparation snapshots the registry, exact
source bytes, generated callers and generator identity. Each selection is a
separate produced Module. The caller builds ordered `typed.Source[]` values
with original project-relative paths and separate exact UTF-8 strings; it
never concatenates documents or supplies resolved semantic facts. Eight peer
cycles
with three CheckCalls each specify 1,080/360/552 JIT assertions and 45/15/23 AOT
checks for nativecases/scalars/direct_calls respectively. All three matrices
passed in the version-2 exact-source private proofs, including the nine scalar
registry rejections. See the [native result](../../slice_backend/README.md#version-2-verification-status)
for the independent terminal readbacks.
The package selection now transports the original modules Main/Math with
document labels 19/20 and, independently, private_import/Math with labels
100/101. Its native stages and final independent admission passed. The eight
original foreign calls and `missing_symbol` have now passed the separate
[version-3 native stages](../../slice_backend/README.md#version-3-foreign-native-result),
including independent aggregate acceptance. `--case foreign` binds the exact
independently reviewed archive containing the six original C functions and descriptor getters.
The same archive must be used for both JIT caller linkage and final AOT linkage;
there is no process-symbol fallback or fabricated replacement implementation. The
original 25-call/11-rejection
inventory is unchanged; 23 supplemental calls make 48 total expected calls.
The runner takes the exact AOT success record from the prepared case.

The implementation source map contains 29 files; `package_graph/Graph.chibi`
is the sole added compiler source, retaining its existing `core:strings`
dependency. The exact nativecases/scalars/direct_calls/modules snapshots
contain 33/44/35/37 recorded files. Both normal and PERF callers use the same
ProduceSources/Pack path. A shared Math source is copied once per snapshot but
remains a distinct input in each independent source-array invocation.

The backend and foreign fixture archive must use plain foreign-library paths:
no quote, backslash, C0 control byte or DEL. Spaces and UTF-8 are supported.
The preparer rejects unsupported spelling before creating output, and the runner
checks it independently. Ordinary source literals and output paths retain their
separate string-escaping support.

The public `prepare_native.py` snapshots the exact working-tree source closure,
templates, C caller and controller into a new directory. It checks generated
bindings and records the identity of an existing non-symlink backend shared
library. It neither builds nor loads that library and never runs a compiler.
The frozen `controller/run_native.py` runs five stages once: Chibi compilation,
normal JIT, expected environment failure, C link and AOT execution. Any failure
stops advancement; existing attempts and outputs cannot be overwritten.

From the repository root, using an already built and separately reviewed
backend library:

```sh
: "${NATIVE_PROOF:?set a new absolute output directory}"
: "${BACKEND:?set the absolute existing reviewed backend shared library}"
python3 -I -B -S tools/experiments/vnext/frontend/bridge/prepare_native.py \
  "$NATIVE_PROOF" --backend "$BACKEND"
```

Add `--case scalars`, `--case direct_calls` or `--case modules` for its separate
fresh snapshot.
Every selection needs a separately reviewed version-3 backend; historical
version-1/2 libraries cannot consume the current frame. The version-2 result
used this public preparer's exact output with a private, separately admitted
five-stage controller. The later package-aware proof also uses exact public-
preparer bytes with a separately admitted private controller and the unchanged
c5 seed/backend identities. The public `run_native.py` whole-run workflow was
not rerun for either checkpoint; passing controls do not create a whole-run
receipt or change its seed pin.

The current version-3 typed-source, packing and foreign native proofs use the
separately admitted current compiler. The foreign proof uses exact public-
preparer output with a separately admitted private five-stage controller;
the public whole-run workflow was not rerun. The public runner now has two
explicit, mutually exclusive compiler routes: `--seed` retains the exact c5
pin, while `--current-compiler` admits the exact current artifact below.
Current-v3 compatibility with c5 remains unverified. The new current route
also needs fresh whole-run execution evidence; selection controls alone do
not establish native acceptance.

After the earlier three-case native proofs closed, a generator docstring-only
cleanup changed no behavior. All three preparations were regenerated and their compiled source
bytes matched the tested snapshots after substituting only each owned object
output path. This was a data-only comparison, not an execution rerun. Its
composition-check SHA-256 is
`71d5a8237ceb5ad57cd1fd864deb88cd626169b7eb6a680dfb964ecc281c59a8`.

Then configure the existing native tools and run the frozen controller:

```sh
: "${SEED:?set the absolute existing verified c5 compiler}"
: "${LLVM_BIN:?set the existing reviewed LLVM 22 binary directory}"
: "${CC:?set the absolute existing reviewed C compiler}"
python3 -I -B -S "$NATIVE_PROOF/controller/run_native.py" "$NATIVE_PROOF" \
  --seed "$SEED" --llvm-bin "$LLVM_BIN" --cc "$CC"
```

For an already-held exact current artifact, replace `--seed` with
`--current-compiler` in a fresh prepared directory:

```sh
: "${CURRENT_COMPILER:?set the absolute existing exact current compiler}"
python3 -I -B -S "$NATIVE_PROOF/controller/run_native.py" "$NATIVE_PROOF" \
  --current-compiler "$CURRENT_COMPILER" --llvm-bin "$LLVM_BIN" --cc "$CC"
```

This Linux x86-64 artifact is 11,576,992 bytes, SHA-256
`7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266`,
with historical source attribution to commit
`02c86287430def964445ec90185961e20319536d`, tree
`780ba87ea9f29308f2f20a94381cdce8cc5d26ca`; see the
[current compiler baseline](../../../../../docs/compiler-current-o2-baseline.md).
The runner rejects crossed or unknown compiler identities before reserving
outputs. `attempt/attempt.json` records the selected route and resolved path,
plus historical source attribution for the current route; actual file facts
remain in `inputs[path]`. This does not repeat source-build admission or
change any release/bootstrap pin. A verified public download or source-rebuild
route for these exact bytes remains open. These instructions are for readers
who already possess the artifact, not a self-contained public replay bundle.

Use repeated `--library-dir /absolute/reviewed/directory` options when the
already installed tools/backend require explicit runtime library search paths.
The `--seed` route must match SHA-256
`1d1d289a2429fe2ee2a75a71a959bf21590a02ff3ebf7cd8af1037a11bf1268e`.
Use existing authorized tools and cached dependencies only; no installation or
dependency fetching belongs to this path. The runner records source/backend
and named-tool identities, stage receipts and owned-process cleanup, but does
not establish a complete compiler/linker/loader dependency admission fence.

### Historical version-1 public reproduction

The normal public workflow passed all five stages once on 2026-11-04 with
exit zero, unchanged recorded inputs and clean owned-process cleanup.
Independent terminal readback checked all 43 recorded inputs, launch plans,
receipts, artifact identities, budgets and cleanup. The copied object and
linked C executable matched the earlier proof's bytes; the AOT caller emitted
the exact 45-check success record. The actual C link trace and 32-header set
were checked, and the harness ELF names the exact final backend library.

The source-manifest SHA-256 is
`cd9a3ba8cf8f0693f1bb669d32335b830a8267723337f26d5c28008c356af9be`;
the runner SHA-256 is
`a273ed0e9764044909cbbfa6e342da776eec60dbb5a2c127019e4fb33e3dd06f`;
the terminal result SHA-256 is
`15fbf6d95b745cc2c0c89f077791ecf67346c64f3f3421514a584fe9d9cc0d6b`.
All 54 Python controls passed: 12 typed-preparation, nine generator,
13 native-preparation and 20 native-runner tests.

A separate old/fixed cancellation control each performed one genuine first
compilation, then delivered SIGTERM at the second stage-selection call. The old
runner attempted a successor, which an audit hook blocked before process
creation; the fixed runner made no successor attempt. Both preserved failed
cancellation receipts and closed child ownership cleanly. Neither executed the
generated program. This machine-bound diagnostic is separate from the default
Python controls and normal five-stage reproduction, not general signal coverage.
Its old and fixed outer receipt SHA-256 values are
`a7befddcad093ebd5080e1e9ae507c4d0ba1eaa57dc578ab76ccc981f463e02e`
and `e6c890fb294b93e4c569d5df17583e1867a5055aa70ad1a7b92333bcd8f04cfc`.
Both diagnostic controls exited normally with successful independent readback.

This public correctness result is separate from the earlier private admission
proof and does not inherit its full tool/loader fences. It is not current-
compiler or throughput acceptance. Preserve historical receipts and use fresh
outputs for new work.

## Transport accounting

Echo/Join has 129 u32 payload words = 516 bytes, one i64 = 8 bytes and eight
name bytes. The existing scalar payload is **532 bytes**. The transport adds
56 header bytes in version 3, totaling **588 bytes**. Pack's logical temporary
word buffer is 143 words / 572 scalar bytes, including the header. The version-2
frame was 584 bytes with a 52-byte header and 142-word / 568-scalar-byte buffer. The
historical version-1 frame was 580 bytes with a 48-byte header and
141-word / 564-scalar-byte buffer; its receipts retain that accounting. Only the 516-byte u32 payload
is explicitly flattened; the 16 side-array bytes are borrowed. These are scalar
accounting figures, not measured allocator/GC/FFI memory or speed claims.

The hot transform is linear in scalar fields with thirteen constant-count length
assertions and one sequential write per scalar, plus destination initialization.
For this fixture the minimum copying work is 129 scalar reads/writes and fourteen
header writes, estimated well under a frame on ordinary hardware; actual Chibi
lowering, allocation/COW and native-boundary costs are unmeasured. Rust framing is O(13)
with no row traversal; checked lowering is a separate backend cost. The Chibi
producer alone owns semantic verification.
The cold generator scans a roughly 4 KiB schema and emits two small fixed-source
artifacts. No per-node parsing, hashing, dispatch or allocation is added.

Deletion gate: remove the one-time flattening layer if authored scalar-record
arrays later cross this boundary directly and differential/lifetime tests prove
the replacement. Do not add a second serializer or per-op FFI. Leave the existing
producer, parser, verifier, old compiler, backend cdylib contract and parallel
native recovery untouched.
