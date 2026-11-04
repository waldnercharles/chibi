# Bounded scalar typed-body producer

The published version-1 retained-parser/signature producer passed proof10 on
2026-11-04 using the pinned old c5 seed: 25 accepted source calls, 42 rejection
probes and 17 verifier mutations. At that checkpoint the public preparer
reproduced the exact compiled inputs. That pure-transform proof does not claim backend
integration, current-compiler admission or full-build throughput acceptance.
The later [source-native proof](../../slice_backend/README.md#accepted-bounded-result)
passed Produce/Verify → Pack → Cranelift JIT and linked C execution for that
version-1 subset. The public reproduction also passed all five stages with the
separately validated final-Cargo backend library and independent terminal
readback. These later results do not broaden proof10's historical claim.

Version 2 adds the scalar operations and direct calls described below. Its
six-stage pure-source proof passed on 2026-11-04, independently of the preserved
version-1 results. Its pre-package test inventory and readback are recorded
[below](#accepted-version-2-pure-transform-result). The separate
[version-2 native result](../../slice_backend/README.md#version-2-verification-status)
also passed all three bounded JIT/AOT matrices.

`ProduceSources(sources)` parses each supplied document once with the retained
`source_parser`, resolves its package/import graph, reads all scalar headers
with `signature_reader.Signature` and its empty named/type constructor
environments, then checks every body. It returns a complete Module
or one source Diagnostic. It never selects functions by name or ignores an
unsupported declaration/body. The dedicated first fixture is `fixtures/EchoJoin.chibi`.

`Model.chibi` is the schema authority. Table-reference ID zero means absent;
those IDs are dense one-based indices and span starts are zero-based. Origin
document labels preserve the caller's nonnegative signed-32-bit `int` label,
including zero, through 2147483647. Negative labels are rejected before parsing;
diagnostics retain the original int label. Every admitted label fits u32.
Functions and blocks follow source/emission order. All edges point forward within a function. Entry block
parameters match its signature. Merge block parameters carry each differing
visible binding. Values alone own types; backends must never read source to infer
semantics. Instructions include terminators as the final row of each block.
An absent else routes the original false edge directly to the merge with its
pre-branch values; it does not allocate an empty false block and jump.

The public transforms are `ProduceSources(sources: Source[]) -> Result` and
`Produce(text: string, document: int) -> Result`. The latter supplies one
`Main.chibi` source to the same implementation.
`Verify(module: Module) -> int` returns zero or a named invariant category for
mutation tests. Successful production asserts verification once. There is no
interpreter, secondary parser, prepared representation, ABI packer or backend
in this package.

The candidate supports explicitly typed i64/bool headers and initialized
locals, identifier/parameter reads, decimal nonnegative i64 and bool literals,
parentheses, wrapping i64 `+`/`*`, signed i64 `>`, bool `!`, plain assignment,
if/else, scalar return, same-package positional identifier calls and qualified
public calls through explicit `project:` import aliases. Other forms are source
diagnostics, not fallback compilation. Foreign calls, equality, `&&`, inferred
locals, discarded-call statements and unary negative literals remain outside
this checkpoint. Negative registry arguments
enter through the native caller. The existing reserved cancellation-singleton
name `Cancelled` remains rejected for functions, parameters and locals. Local shadowing of any currently visible
parameter/local is unsupported and diagnosed; disjoint branch-local names are
allowed. Names and branch values are source-driven, including renamed
functions/locals and +7.

Version 2 preserves opcodes 1–5 and appends ConstantBool=6, MultiplyI64=7,
GreaterI64=8, NotBool=9 and DirectCall=10. A DirectCall payload names one
`Call { callee, first_argument }` row: eight scalar bytes, with arity/result
type read only from the callee Signature. Calls and edges partition the shared
argument array without gaps or overlaps; zero-arity calls start at zero.
Value remains the sole owner of a value's type. Bool constants are exactly 0/1
and unused instruction fields are zero.

Headers precede bodies, including private/forward/self/mutual callees. A visible
scalar binding shadows a function name and is diagnosed as non-callable. Calls
require a same-package identifier or an imported alias/member and positional
arguments, with at most 32 arguments.
Production reserves the outer argument span before evaluating arguments
left-to-right; nested calls append beyond it, and writes re-fetch by index.
No partial Module is published. Verify checks callee/signature IDs before use,
unique Call ownership, shared argument coverage, scalar types and every
argument's dominance in the caller. Call-graph cycles are not CFG back-edges.

Admission limits are explicit experiment limits: 1..8 source files, 32 imports,
256-byte source paths/import strings, 64 KiB aggregate source, 8192 aggregate
parsed nodes, 32 aggregate functions, 32 parameters/function and 64
simultaneously visible bindings
(including parameters), 64 blocks/function, expression/statement nesting 32.
The producer checks each limit before adding the affected rows or descending. Parser nesting
behavior belongs to the retained parser and is not newly guaranteed here.
Bounded linear name comparison and u64 dominance masks are not scalable compiler
claims; remove those limits/algorithms before a representative compiler workload.

Records are authored Chibi records. The 8-byte Value/24-byte Instruction figures
are scalar payload targets, not asserted native array layout. The current C
foreign ABI cannot pass authored-record arrays. The synchronous versioned
boundary flattens these tables once into a u32 arena plus i64 literals/u8 names,
counting that temporary copy and its bytes explicitly. The [scalar transport
experiment](../bridge/README.md) generates this packing operation and borrowed
Rust views. Its version-1 Python checks, native Chibi packing proof and Rust
framing tests passed; the historical source-native proof covers backend integration separately.
Version-2 packing and seven Rust framing tests have now passed their separate
pure-source proof. The separate version-2 native result covers source-to-backend
execution, without broadening either version's pure-transform receipts.

## Package-aware input and proof status

`Source { path, text, document }` carries original source bytes, a canonical
project-relative `.chibi` path and a distinct nonnegative document label. The
first source selects the entry package. The producer performs no filesystem
access or discovery. It reuses `package_graph.Build` for package membership,
explicit imports and cycle checks, then requires every supplied package to be
reachable from the entry package. Original trees and document labels remain
available for precise diagnostics; the caller supplies no resolved symbols.

All headers precede all bodies in supplied file/declaration order. Unqualified
calls resolve in the current package; an imported member must be public. Only
public entry-package functions are exported. Dependency functions use unique
`.vnext.<FunctionID>` native names and remain internal, including public source
members. Neither Model, Verify, version-2 transport nor the backend ABI changes.

The complete unfiltered package-aware `tests/Main.chibi.in` compiled and exited
zero on 2026-11-04 using the separately admitted current O2 builder with O0,
`jobs=1` output. Its **259 static assert-call sites are not a measured dynamic
check count**. The template SHA-256 is
`eaa0d62bcf5f2a3f3711eeef5fce34fbecbdab6e0bd67f123fdaeb3bf4272185`;
independent pure-transform acceptance SHA-256 is
`691fe542aee0dca5b7b70519f884dd20b9ea0d6d73777d8c41d25ade2a7e8e3c`.
This accepts the producer test, not whole-current-compiler/self-hosting or speed.
The original failed attempt remains separate evidence.

All four package-aware native cases passed their five execution stages and
independent full-chain acceptance. See the
[package checkpoint](../../slice_backend/README.md#package-aware-checkpoint)
and [normalized evidence](../../evidence/package-aware-20261104.json). The
historical pre-package pure-source and native results below retain their exact
inputs and counts; they do not validate later source changes.

## Experimental arithmetic deviation

This isolated producer deliberately assigns wrapping semantics to **every**
admitted typed i64 addition and multiplication, including literal-only
expressions. It does no compile-time folding/evaluation. Each individual decimal literal must still fit
0 through 9223372036854775807. Thus `9223372036854775807 + 1` is accepted as two
ConstantI64 rows followed by AddI64; the JIT/AOT result is
-9223372036854775808. The pure-transform test pins these operations and full-width
literals. The version-1 source-native proof checked that addition result through
both native consumers. The version-2 scalar matrix also checked wrapping
multiplication through both native consumers.

This differs from the current numeric contract's exact, range-checked constant
expressions. It is an explicit experimental rule, **not** a settled language
change or current-language parity claim. Existing code expecting constant
overflow rejection would lose that check on this path; a migration must either
adopt wrapping explicitly or retain checked constant-expression semantics.
Exact checked constant expressions remain an alternative to decide before
wider adoption. Production compiler behavior and its numeric contract are
unchanged. Uniform typed operations avoid introducing an evaluator solely for
this first body producer.

## Caller tests and cost estimate

`tests/Main.chibi.in` is a pure-transform executable template. A copied-source
proof caller replaces four fixture markers with independent JSON-style string
literals of exact Echo/Join, modules Main/Math and private-import source bytes,
then maps `project:vnext_typed` to the three package files here. The fixture/tests
subdirectories are not package implementation inputs. Tests change +1 to +7, rename exports/locals, change an
assignment, exercise nested and early-return branches, check source rejections
and paired admission boundaries (functions, parameters, visible bindings,
blocks, expression depth and source bytes), and corrupt real produced tables.
The version-2 additions include exact 8192/8193-node and 31/32 statement-depth
controls, repeated/nested/zero/32-argument calls, mixed scalar types, source
rename/reorder and branch merges. They add 40 Verify rejection expectations and
seven precise source diagnostic-span checks for call/type/ownership failures.
The full version-2 Chibi template passed against the frozen proof inputs;
historical proof counts still apply only to their frozen inputs. Document-label
cases check 0 and 2147483647
in every emitted origin and reject -1 without changing the diagnostic label.
These tests do not evaluate an IR or claim generated-code
execution. No generic controller is added here.

To check and prepare the copied source from the repository root:

```sh
python3 -I -B tools/experiments/vnext/frontend/typed/prepare_tests.py -v
work=$(mktemp -d)
python3 -I -B tools/experiments/vnext/frontend/typed/prepare.py "$work/typed"
```

Preparation refuses an existing output directory. It writes 15 compiler
sources, `core:strings`, three typed package sources, generated `Main.chibi`,
the original template and four fixtures: 25 recorded files plus
`source-manifest.json` with relative working-tree byte hashes.
It checks exact UTF-8 fixture transport and the package/import closure. It
never compiles, runs native tests or configures a toolchain; source hashes alone
do not establish compiler or tool admission.

The historical manual Linux x86-64 reproduction below uses the verified c5
native compiler. It is not the recorded package-aware pure-transform proof,
which used the current O2 builder. Set `SEED` to the verified c5 native compiler
and `LLVM_BIN` to the matching, working LLVM 22 binary directory. The
seed used here has SHA256
`1d1d289a2429fe2ee2a75a71a959bf21590a02ff3ebf7cd8af1037a11bf1268e`.
Review the prepared inputs and tool identities before launching. With GNU
`timeout`, compilation is limited to 300 seconds and tests to 30 seconds:

```sh
: "${SEED:?set the verified native compiler path}"
: "${LLVM_BIN:?set the working LLVM 22 binary directory}"
timeout --signal=KILL 300s "$SEED" \
  --compiler "$work/typed/project" --core "$work/typed/core" \
  --source "$work/typed/Main.chibi" --entry Main \
  --output "$work/typed/typed-tests" --llvm "$LLVM_BIN" --level 0 --jobs 1 &&
timeout --signal=KILL 30s "$work/typed/typed-tests"
```

The test command runs only after successful compilation. A nonzero exit or
timeout is failure; keep its output and prepare a fresh directory for another
attempt. These manual commands reproduce the test inputs and limits, not the
independently checked tool/dependency fences of the recorded proof.

The 215-byte/eight-line fixture requires seven real annotation reads and emits
2 functions/signatures, 4 blocks, 7 values/origins, 6 instructions, 3 edges,
2 edge arguments, 5 block parameters, 4 signature types, 1 literal and 8 name
bytes. This proof07-checked layout is 532 scalar payload bytes
under the proposed record widths, excluding array/record headers, retained
parser storage, capacity and verifier scratch.
Actual Chibi array layouts and allocation/copy traffic remain unmeasured.

The minimum work is one parse of 215 bytes, seven scalar signatures, a source
body walk, six emitted operations and a linear table verifier. It should be
well below a frame in packed native data; current Chibi representation/runtime
cost is unknown. Parser and signature closure: 15 unchanged files/6,682 physical
lines. An ordinary annotation takes reader.Signature -> reader.Type ->
named.Parsed/BindName -> Builtin -> reader.Modifiers, with empty resolution
catalogs. The retained reader creates temporary strings/arrays even for these
simple types; that cost is included, not replaced by checking storage alone.

Producer name lookups compare source bytes without allocating identifier text,
with checked maxima of 32 function names and 64 visible bindings. Each branch
keeps ordinary COW snapshots of the pre-branch map and the two arm maps, not
explicit sliced copies. Arm snapshots may include scoped-local suffixes; the
merge reads only the pre-branch prefix. When a shared map is subsequently
mutated, COW can copy at most 64 value IDs (256 scalar payload bytes) per detach,
plus any binding-name-node map detach. Snapshot headers, reference counts,
capacity, actual detach counts and record-array boxing remain unmeasured. Module
arrays are mutated through direct scoped output parameters without explicit
per-node module copies in source; confirming that the exact emitted lowering preserves this
intent remains a measurement gate. No unsupported reserve API is assumed. At
admission limits the conservative bounds are 139,264 values, 10,240 instructions,
8,192 calls and 524,288 combined edge/call arguments, below signed array counts
and the high-bit definition tag. The loose whole-module transport ceiling is
4,363,700 bytes, not a reservation size. Table work is linear in nodes, rows and
arguments plus the stated bounded name scans. Actual allocation/COW and native
compilation costs remain unknown; these bounds are not throughput evidence.

Delete later: this template/proof-only fixture path after migration into the
real source-to-JIT/AOT harness; replace the bounded lookup/dominance algorithms
before removing their limits. Measure: closure compilation compatibility,
actual payload/allocation/COW costs, exclusive parser/signature/body/verifier
phase time, and the eventual counted flattening copy. Leave alone: production
parser/type logic, old compiler/prepared emitter and the separate backend library.

## Accepted version-2 pure-transform result

Before package support, all six separately admitted stages passed on
2026-11-04: typed compile/test,
packing compile/test and standalone Rust framing build/test. Independent final
readback checked exact source/tool/runtime inputs, artifact/receipt chains and
clean child-process cleanup. The public typed preparer's 21 files matched the
proof inputs byte-for-byte.

That frozen typed template executed **109 Produce invocations: 42
accepted and 67 rejected**, plus **57 verifier-mutation rejection checks**.
These counts apply to that frozen template, not to the later package-aware
source or a sum of historical and new tests. It includes seven exact
diagnostic-span checks, eight paired admission
limits, 32/33 call arguments and the document-label boundary cases. Its 216
textual assert sites are not a runtime assertion count. The packing template
covers original Echo/Join, renamed/+7 source, nested-call argument packing and call-row
COW isolation, and an empty Module; all seven standalone Rust framing tests
passed unfiltered. These are pure transforms, not generated-code execution.

The typed-template SHA-256 is
`132d49495e35847ea0edf3bbee012130891b2fab21bb66fb7a7289828dfff18b`.
The six-stage independent final readback SHA-256 is
`d3390ad2f98517d8d94d71d66ca0be8d437a00eae33985e36cc3afff2c0a8b4c`;
it binds input fence
`525eb88b4ce9d4a8737ced0219ac82a1c3e593d46487162eff199a871df83c77`
and each build/test receipt. The pinned old c5 seed built the Chibi callers;
this does not admit the current compiler, self-hosting or a throughput claim.

## Historical version-1 old-seed compatibility experiment

Proof04 rejected `OriginID` with
`scalar proof requires known effect-free scalar execution, not effects 1`.
ReadEffect is 1 in the inspected current compiler. The rejected source stored
`State.output: ^Module` and read it into a local alias. The [exact c5 scalar admission source](https://raw.githubusercontent.com/waldnercharles/chibi/c5c0a1ac2e065b5e57feec0de6dd453c12fa267d/tools/compiler/package_compiler/Scalar.chibi)
at lines 3424–3446 permits native print, suspension and scoped written parameters but
lacks the current compiler's managed-pointer/view effect allowances. The
observed failure is consistent with that stored-pointer incompatibility, rather
than evidence that arbitrary writes should be permitted.

The producer now passes `output: ^Module` directly beside `s: ^State`; edge
helpers receive only output. This matches the retained parser's scoped style
without duplicating the then-13 Module table fields into scratch State. Proof05
cleared that rejection, then found old-seed restrictions in test mutation
syntax. The fixture now modifies local arrays/rows before reassigning the
Module fields. No effect check, assertion or semantic oracle was weakened.

Proof06 passed compilation in 7.965652 seconds and all pure-transform tests in
0.046508 seconds, with clean process/source fences. Its manifest SHA256 is
`c0fdf2752d738391ce192f6816e86c4048b9d7082ecd30168110984a95e99347` and
receipt SHA256 is
`686a4cbf62fd34f59e066172a6fb73a8f61a45a41fa3b79e435c13422822826f`.
That proof used Produce SHA256
`a49541556876717bd025126d799493b2c0519ed6f8269eb5fb0c0555374c2ac2`
and test-template SHA256
`d8024299fff1faad7ae0360a5afcebdc995f5647ddb604e93ebae0b7d99621d0`.
These are one compile/test run's elapsed times, not source-to-native output
throughput or phase benchmarks. It exercised real source-produced typed bodies
and verifier mutations, not generated Echo/Join machine code.

Proof07 passed compilation in 7.952043 seconds and 65 Produce calls plus 17
verifier mutation cases in 0.079744 seconds, with clean process/source fences.
Its manifest SHA256 is
`687f9a706614d157a35da7239c664961435f9fa4af2be53496934cb295f049bd` and
receipt SHA256 is
`5051be4d5752ec31020bcfed8c23907293dd0cd81a9fa74fe6b5a1870a0b3d83`.
It used Produce SHA256
`84367944c5c6a745e7f8e6e75804795f1f6534937602abba4b0bc8e83af51835`
and test-template SHA256
`408169405021706fe3ff2265eaf983df90d8d5eb513642784a482e3b6658e166`.
That run exercised the absent-else simplification, which removes one Block,
Jump and Edge from the pair fixture (52 target payload bytes), and stronger
source value-flow/boundary oracles. Its timings remain one compile/test run,
not generated-code or throughput evidence.

Proof10 retained the same producer and passed the final document-label tests:
67 Produce calls plus 17 verifier mutations. Compilation took 8.336857 seconds
and tests took 0.073873 seconds, with clean process/source/tool fences. Its
manifest SHA256 is
`95f8a49ba0930ce95d95e928a7cf397986feb4a12b79c39df78d72c3d515ea34`
and receipt SHA256 is
`bae2d34955a1140593048dc6b5c4e14ae2e37f53c6807dfff5b4c85011bf945e`.
The final test-template SHA256 is
`e8f4cd1b17b7d61a3c1db467fbbfb618ed0555aaca2cb2b4ad34fa73f81909a9`.
The preparation-only script independently reproduced all 19 compile inputs
and both original transport inputs byte-for-byte against that frozen proof.
Its ten Python tests and six independent failure/closure checks passed.
These are functional probe results, not representative compilation benchmarks.

Exact emitted layout, allocation/COW costs and complete-build performance
remain open. The later bounded source-native proof covers JIT/AOT execution
separately; it does not close those measurement gates.
