# Typed-module scalar transport bindings

Status: nine Python generator controls, the native Chibi Produce-to-Pack proof,
and all six generated Rust view/framing tests pass. The later, separately
reviewed [source-native proof](../../slice_backend/README.md#accepted-bounded-result)
also passed 1,080 JIT value assertions and 45 linked C checks. The public
five-stage reproduction below has now passed using the separately validated
final-Cargo backend library.

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

The ordinary Chibi call is `bridge.Pack(module)`, returning
`Packed(buffers)` or `TooLarge(table)`. The caller handles one result and passes
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
11 row counts, in Module declaration order with literals/name_bytes omitted:
functions, signatures, parameter_types, blocks, block_parameters, values,
instructions, edges, arguments, origins, names. This is 12 u32 words / 48 bytes.
Rows follow in that same order, fields in declaration order. Widths are
6, 3, 1, 4, 1, 2, 6, 3, 1, 3, 2 words. Literal and name lengths come from their
independent C pointer/count pairs. Native byte order is intentional: these are
same-process scalar arrays, not an endian-neutral on-disk representation.

The Chibi length domain is signed 32-bit. Pack checks each count against the
remaining word capacity before multiplication/addition, calculates in i64 and
narrows only after the bounds are proved. It returns the overflowing table
before allocation. Allocation failure remains the runtime's abort contract.
An all-zero empty Module packs to version 1 plus eleven zero counts and empty
side arrays; a zero version is not an accepted transport frame.

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
replay covers all eleven count positions with zero, one, MAX_ELEMENTS and
MAX_ELEMENTS+1, every short prefix of small frames, and extra words. Large-count
rejections use actual short headers with safe Arena::from_parts calls, never
large allocations or fabricated pointers. All six tests pass without filtering.

The Rust framing proof used the official Rust 1.99.0 toolchain, GCC 14 and
LLVM 22.1.8 lld. Its fresh input fence SHA-256 is
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
The [backend README](../../slice_backend/README.md) records the accepted bounded
result, ABI contract and remaining language/platform gates.

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

Then configure the existing native tools and run the frozen controller:

```sh
: "${SEED:?set the absolute existing verified c5 compiler}"
: "${LLVM_BIN:?set the existing reviewed LLVM 22 binary directory}"
: "${CC:?set the absolute existing reviewed C compiler}"
python3 -I -B -S "$NATIVE_PROOF/controller/run_native.py" "$NATIVE_PROOF" \
  --seed "$SEED" --llvm-bin "$LLVM_BIN" --cc "$CC"
```

Use repeated `--library-dir /absolute/reviewed/directory` options when the
already installed tools/backend require explicit runtime library search paths.
The seed must match SHA-256
`1d1d289a2429fe2ee2a75a71a959bf21590a02ff3ebf7cd8af1037a11bf1268e`.
Use existing authorized tools and cached dependencies only; no installation or
dependency fetching belongs to this path. The runner records source/backend
and named-tool identities, stage receipts and owned-process cleanup, but does
not establish a complete compiler/linker/loader dependency admission fence.

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
48 header bytes, totaling **580 bytes**. Pack's logical temporary word buffer is
141 words / 564 scalar bytes, including the header. Only the 516-byte u32 payload
is explicitly flattened; the 16 side-array bytes are borrowed. These are scalar
accounting figures, not measured allocator/GC/FFI memory or speed claims.

The hot transform is linear in scalar fields with eleven constant-count length
checks and one sequential write per scalar, plus destination initialization.
For this fixture the minimum copying work is 129 scalar reads/writes and twelve
header writes, well under a frame on ordinary hardware; actual Chibi lowering,
allocation/COW and native-boundary costs are unmeasured. Rust framing is O(11)
with no row traversal; checked lowering is a separate backend cost. The Chibi
producer alone owns semantic verification.
The cold generator scans a roughly 3 KiB schema and emits two small fixed-source
artifacts. No per-node parsing, hashing, dispatch or allocation is added.

Deletion gate: remove the one-time flattening layer if authored scalar-record
arrays later cross this boundary directly and differential/lifetime tests prove
the replacement. Do not add a second serializer or per-op FFI. Leave the existing
producer, parser, verifier, old compiler, backend cdylib contract and parallel
native recovery untouched.
