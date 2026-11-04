# Bounded scalar typed-body producer

The retained-parser/signature producer and final pure-transform tests passed
in proof10 on 2026-11-04 using the pinned old c5 seed: 25 accepted source calls,
42 rejection probes and 17 verifier mutations. The public preparer reproduces
the exact compiled inputs. That pure-transform proof does not claim backend
integration, current-compiler admission or full-build throughput acceptance.
The later [source-native proof](../../slice_backend/README.md#accepted-bounded-result)
passed Produce/Verify → Pack → Cranelift JIT and linked C execution for this
bounded subset. The public reproduction also passed all five stages with the
separately validated final-Cargo backend library and independent terminal
readback. These later results do not broaden proof10's historical claim.

`Produce(text, document)` parses once with the retained `source_parser`, reads
all scalar headers with `signature_reader.Signature` and its empty named/type
constructor environments, then checks every body. It returns a complete Module
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

The public transform is `Produce(text: string, document: int) -> Result`.
`Verify(module: Module) -> int` returns zero or a named invariant category for
mutation tests. Successful production asserts verification once. There is no
interpreter, secondary parser, prepared representation, ABI packer or backend
in this package.

Supported source: explicitly typed i64/bool headers and initialized locals,
identifier/parameter reads, decimal nonnegative i64 literals, parentheses,
wrapping i64 addition, plain assignment, if/else and scalar return. Other forms
are source diagnostics, not fallback compilation. The first gate intentionally
excludes calls, bool literals/operators and unary negative literals. The
existing reserved cancellation-singleton name `Cancelled` remains rejected for
functions, parameters and locals. Local shadowing of any currently visible
parameter/local is unsupported and diagnosed; disjoint branch-local names are
allowed. Names and
branch values are source-driven, including renamed functions/locals and +7.

Admission limits are explicit experiment limits: 64 KiB source, 8192 parsed
nodes, 32 functions, 32 parameters/function, 64 simultaneously visible bindings
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
Rust views. Its Python checks, native Chibi packing proof and Rust framing tests
pass; the later source-native proof above covers backend integration separately.

## Experimental arithmetic deviation

This isolated producer deliberately assigns wrapping semantics to **every**
admitted typed i64 addition, including literal-only expressions. It does no
compile-time folding/evaluation. Each individual decimal literal must still fit
0 through 9223372036854775807. Thus `9223372036854775807 + 1` is accepted as two
ConstantI64 rows followed by AddI64; the JIT/AOT result is
-9223372036854775808. The pure-transform test pins these operations and full-width
literals. The later source-native proof checks that wrapping result through
both native consumers.

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
proof caller replaces its sole `FIXTURE_SOURCE` marker with the JSON-style
string literal of the exact fixture bytes, then maps `project:vnext_typed` to
the three package files here. The fixture/tests subdirectories are not package
implementation inputs. Tests change +1 to +7, rename exports/locals, change an
assignment, exercise nested and early-return branches, check source rejections
and paired admission boundaries (functions, parameters, visible bindings,
blocks, expression depth and source bytes), and corrupt real
produced tables. The 8192-node and statement-depth boundaries are not exercised
by this template. Document-label cases check 0 and 2147483647 in every emitted
origin and reject -1 without changing the diagnostic label.
These tests do not evaluate an IR or claim generated-code
execution. No generic controller is added here.

To check and prepare the copied source from the repository root:

```sh
python3 -I -B tools/experiments/vnext/frontend/typed/prepare_tests.py -v
work=$(mktemp -d)
python3 -I -B tools/experiments/vnext/frontend/typed/prepare.py "$work/typed"
```

Preparation refuses an existing output directory. It writes the 15 production
and three typed package sources, generated `Main.chibi`, original template and
fixture, and `source-manifest.json` with relative working-tree byte hashes.
It checks exact UTF-8 fixture transport and the package/import closure. It
never compiles, runs native tests or configures a toolchain; source hashes alone
do not establish compiler or tool admission.

For manual Linux x86-64 reproduction, set `SEED` to the verified c5 native
compiler and `LLVM_BIN` to the matching, working LLVM 22 binary directory. The
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
admission limits a conservative bound is fewer than 150,000 values and 11,000
instructions, well below u32 and the high-bit definition tag. These are bounds, not throughput evidence.

Delete later: this template/proof-only fixture path after migration into the
real source-to-JIT/AOT harness; replace the bounded lookup/dominance algorithms
before removing their limits. Measure: closure compilation compatibility,
actual payload/allocation/COW costs, exclusive parser/signature/body/verifier
phase time, and the eventual counted flattening copy. Leave alone: production
parser/type logic, old compiler/prepared emitter and the separate backend library.

## Old-seed compatibility experiment

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
without duplicating all 13 Module table fields into scratch State. Proof05
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
