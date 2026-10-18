# Bounded authored package compiler

## Status and dependency order

This M5 increment runs. On macOS arm64 (Debug, ASan/UBSan, 2026-10-18),
`tool language-bounded-compiler-chibi` passes its one case with 36,739
assertions in 15.9 s under the production 60-second supervisor. Stage zero
prepares the compiler closure with 124 bytecode bootstrap entries. The authored
compiler then handles 42 sources on each compiler backend (84 calls, 76 raw
parses, seven bridge rejections), produces 28 accepted payloads, and runs the
generated artifacts 56 times across the bytecode and SLJIT targets. Linux is
unmeasured. It does not complete M5 and is not stage one, stage two or a
self-hosting result.

It builds on the merged raw-syntax ABI and binary handoff (PR445),
snapshot-to-reader composition (PR446), scalar lowering (PR447) and `T(value)`
numeric conversions (PR450). The first run found two source defects:
- `resize` cannot grow an authored record array, so the expression frame stack
  is now filled row by row.
- Decimal parsing compared `uint64` values, which the verifier does not order,
  so it now accumulates in `int64`.

The driver does not extend any existing selector's caps, corpus or aggregate
membership.

## Entry and complete input

`tools/compiler/bounded_compiler` is an ordinary three-file Chibi package. Its
entry is `Compile(project: string, paths: string[], sources: string[],
abi: scalar_lowering.Target) -> Result`.

The caller supplies the complete explicit package source snapshot as owned
strings. There is no disk discovery, imported-source callback or hidden native
source preparation. Paths and sources have equal lengths, from one through
three. Each source is at most 1,024 bytes; project and supplied paths are at most
240 bytes. The existing snapshot validates routing, UTF-8, package headers and
graph consistency. Each retained tree is at most 512 nodes; retained document
keys and package identities are at most 240 bytes. An ingress that accepts raw
bytes must reject embedded NUL before converting them to runtime strings.

The accepted graph contains exactly one import-free package. Every file is
walked before lowering; package-header/comment-only sibling files are accepted.
Exactly one declaration may occur across the complete package: an explicitly
`pure`, nongeneric, identifier-named, zero-argument function returning one
ordinary `int` or `bool`, not itself named `int` or `bool`. Its body contains
exactly one ordinary return. Other
declarations, extra statements and unhandled expressions reject. The driver
does not compile only the selected function while silently dropping other
source contents.

## Supported expression subset

- Boolean literals, parentheses, Boolean `!`/`not`, `&&`/`and`, `||`/`or`, and
  equal/not-equal comparisons.
- Exact decimal I32 literals in direct `int` return or explicit `int(value)`
  context, including signed minimum. Out-of-range, hexadecimal, fractional and
  exponent literals reject in this increment.
- Identity `int(value)` without type arguments, integer unary plus/minus/inversion, add/subtract/multiply,
  bitwise and/or/xor, and ordinary scalar comparisons. Binary integer operands
  must already have explicit `int` types, such as
  `int(19) + int(23)`. Implicit numeric inference is unsupported;
  even `19 + 23` rejects rather than inventing a second inference policy.
- No variables, calls, other numeric conversions, division, remainder, shifts,
  aggregates, tasks, effects, ECS declarations or source imports.

ReadExpression uses explicit frames, at most 64 visits and 24 lowered rows.
There is no recursive expression traversal. It preserves source ranges and
constructs the existing scalar lowering typed tables; it does not provide
precomputed native semantic answers to Chibi. Existing signature_reader owns
the accepted result annotation, lexical_binding owns declaration admission and
stable identities, and scalar_lowering owns instruction/control-flow selection,
including short circuit. Supported integer operations retain the existing
width-wrapping runtime semantics.

## Exact source-to-artifact path

1. Chibi checks the request bounds and constructs source_snapshot inputs.
2. `source_snapshot.Build` uses the existing raw Tree-sitter and JSON primitives
   while Chibi owns paths, package extraction, routing, syntax diagnostics and
   package graph decisions.
3. `analysis_state.DocumentsForPackage` and `TreeForDocument` supply retained
   reader trees. Chibi inspects every package member and rejects unsupported
   declarations before encoding.
4. Existing authored signature_reader and lexical_binding process the accepted
   header. Chibi ReadExpression builds Bool/I32 typed rows from source nodes.
5. `scalar_lowering.Lower` constructs the bounded instruction plan. Unsupported
   target facts and invalid typed rows reject before the payload capability.
6. The private producer primitive receives only that plan, authored IDs, entry
   text and scalar result storage. The native scalar decoder replays it; existing
   builder verification, optimization/finalization and prepared serialization
   produce one inner CHIBIPRE version-15 module.
7. Artifact.chibi constructs the complete one-module CHIBICLO version-1 envelope,
   including magic, root/module identity, counts, lengths, name and payload. All
   envelope words are authored little-endian encoding. The inner payload is
   bounded to 65,536 bytes; the envelope is bounded to 65,832 bytes.
8. `Result.Compiled` returns full-width module/entry IDs and owned bytes. A
   fresh compiler_host API instance can validate/load those resulting bytes and
   invoke the declared zero-argument function on bytecode or SLJIT.

The module identity is the existing lexical `ID("module", package_identity)`;
the function ID comes from the existing package-scope `binding.Declare` path.
Results and diagnostics are ordinary owned invocation data.

## Explicit native boundary and remaining work

`compiler_prepared_scalar(plan, module, function, entry, result_storage)` is a
private producer-only primitive. Its immutable service supplies the trusted
scalar_lowering.Plan.Ready tag from bootstrap metadata. It never accepts source
text, syntax trees, typed semantic IR, package preparation requests or an old
compiler callback. It returns one inner prepared byte array and a diagnostic.
The native core verifies zero arguments/state, one ordinary declared scalar
result and the existing plan schema. It does not select opcodes or assemble
the package envelope.

The native finalizer, module publication and inner serializer remain declared
transitional compiler dependencies. This increment authors the source frontend,
lowering control and outer artifact policy; it does not claim that all compiler
artifact construction has moved to Chibi.

The existing runtime-only chibi-compiler-host is unchanged. Neither the new
producer files nor frontend/writer dependencies are added to its link inputs or
catalog. The compiler closure itself currently requires the private producer
fixture and cannot run in that runtime-only host. Stage zero prepares the
compiler implementation files only; the tests then pass user-source strings to
the authored Compile entry. Only the generated user artifact is handed to the
runtime-only host. A supported isolated producer product, complete compiler
language coverage, authored inner encoding, bootstrap compatibility checks and
stage-one/stage-two compilation of the complete compiler remain open work.

No public command, language grammar, project schema, project version, installed
SDK export or migration changes. No old compiler/recovery path is removed.

## Required verification

The separate C pico_unit suite must prepare only the declared compiler closure,
check successful and rejected complete sources on both compiler backends,
assert zero payload calls for source rejection, compare deterministic artifact
bytes across backends and repeats, retire source/producer ownership, then load
and run every accepted artifact through both target backends. These are fresh
host API instances in the same test process; they do not establish a separate
runtime-only executable-process result. It must also
verify byte limits, precise signed literals, wrapped arithmetic, meaningful
changed-source output, malformed native declarations and incompatible target
facts. Counters report attempted work even on failure.

Finite source counts, request/callback/parser bounds, output storage and target
invocation counts belong to the exact candidate admission report. Build timing
is separate from the single unchanged 60-second test deadline. Source-only
review, formatting or admission does not establish an executed test result.
